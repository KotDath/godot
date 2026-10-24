/**************************************************************************/
/*  sdk_executor.cpp                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "sdk_executor.h"

#include "resources.gen.h"

#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/os/mutex.h"
#include "core/os/thread.h"
#include "core/templates/safe_refcount.h"
#include "editor/editor_node.h"
#include "editor/gui/progress_dialog.h"
#include "main/main.h"
#include "scene/gui/box_container.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/main/scene_tree.h"
#include "servers/display/display_server.h"

namespace AuroraSDK {
namespace {
struct Job {
	String path;
	List<String> arguments;
	Dictionary spec;
	String stage;
	SafeFlag done;
	SafeFlag cancel;
	SafeFlag user_cancel;
	Mutex process_mutex;
	ProcessID pid = 0;
	SafeFlag killed;
	bool helper = false;
	bool secret = false;
	bool ready = false;
	bool received_result = false;
	Vector<uint8_t> pending;
	Vector<uint8_t> raw_out;
	Vector<uint8_t> raw_err;
	String output;
	int exit_code = -1;
	Error error = OK;

	void append(const String &p_text) {
		output += p_text;
		if (output.length() > 8 * 1024 * 1024) {
			output = "[Earlier SDK output truncated]\n" + output.right(4 * 1024 * 1024);
		}
	}
	void line(const String &p_line) {
		JSON json;
		// Do not use parse_string(): non-protocol SDK diagnostics are ordinary text.
		if (json.parse(p_line) != OK || json.get_data().get_type() != Variant::DICTIONARY || int(Dictionary(json.get_data()).get("aurora_job", 0)) != 1) {
			if (!secret) {
				append(p_line + "\n");
			}
			return;
		}
		const Dictionary event = json.get_data();
		const String type = event.get("event", "");
		if (type == "ready") {
			ready = true;
		} else if (type == "output" && !secret) {
			append(event.get("text", ""));
		} else if (type == "error") {
			append("SDK supervisor error: " + String(event.get("kind", "unknown")) + "\n");
			error = FAILED;
		} else if (type == "result") {
			received_result = true;
			exit_code = event.get("exit_code", -1);
			if (bool(event.get("cancelled", false))) {
				if (error == OK) {
					error = user_cancel.is_set() ? ERR_SKIP : ERR_CONNECTION_ERROR;
				}
				append("SDK job stopped: " + String(event.get("reason", "unknown")) + "\n");
			}
		}
	}
	void bytes(Vector<uint8_t> &p_buffer, const uint8_t *p_bytes, int p_length) {
		const int64_t offset = p_buffer.size();
		if (offset + p_length <= 8 * 1024 * 1024) {
			p_buffer.resize(offset + p_length);
			memcpy(p_buffer.ptrw() + offset, p_bytes, p_length);
		}
	}
	void consume(const uint8_t *p_bytes, int p_length) {
		if (!helper) {
			bytes(raw_out, p_bytes, p_length);
			return;
		}
		for (int index = 0; index < p_length; index++) {
			if (p_bytes[index] == '\n') {
				line(String::utf8((const char *)pending.ptr(), pending.size()));
				pending.clear();
			} else {
				pending.push_back(p_bytes[index]);
			}
		}
		if (pending.size() > 2 * 1024 * 1024) {
			error = ERR_INVALID_DATA;
			cancel.set();
		}
	}
};
struct Completion {
	SafeFlag &flag;
	~Completion() { flag.set(); }
};
void kill_transport(Job &p_job) {
	MutexLock lock(p_job.process_mutex);
	if (p_job.pid > 0) {
		if (OS::get_singleton()->is_process_running(p_job.pid)) {
			OS::get_singleton()->kill(p_job.pid);
			p_job.killed.set();
		}
		p_job.pid = 0;
	}
}
void run_job(void *p_data) {
	Job &job = *static_cast<Job *>(p_data);
	Completion completion{ job.done };
	// All pipe access belongs to this worker. Blocking writes cannot freeze the
	// GUI; reads are bounded by available bytes and never wait for another pipe.
	Dictionary process = OS::get_singleton()->execute_with_pipe(job.path, job.arguments, true);
	if (process.is_empty()) {
		job.error = ERR_CANT_FORK;
		return;
	}
	const ProcessID pid = process["pid"];
	{
		MutexLock lock(job.process_mutex);
		job.pid = pid;
	}
	Ref<FileAccess> pipe = process["stdio"];
	Ref<FileAccess> errors = process["stderr"];
	bool sent = false;
	bool cancel_sent = false;
	uint64_t heartbeat = 0;
	while (true) {
		bool running;
		{
			MutexLock lock(job.process_mutex);
			running = job.pid != 0 && OS::get_singleton()->is_process_running(pid);
			if (!running) {
				job.pid = 0;
			}
		}
		int received = 0;
		for (int stream = 0; stream < 2; stream++) {
			Ref<FileAccess> source = stream ? errors : pipe;
			const uint64_t available = MIN(source->get_length(), uint64_t(4096));
			if (!available) {
				continue;
			}
			uint8_t buffer[4096];
			const uint64_t count = source->get_buffer(buffer, available);
			received += count;
			if (!stream) {
				job.consume(buffer, count);
			} else if (!job.secret) {
				job.bytes(job.raw_err, buffer, count);
			}
		}
		if (!running && received == 0) {
			break;
		}
		if (running && job.helper && job.ready && !sent && !job.cancel.is_set()) {
			CharString data = (JSON::stringify(job.spec) + "\n").utf8();
			if (!pipe->store_buffer((const uint8_t *)data.ptr(), data.length())) {
				job.error = ERR_FILE_CANT_WRITE;
				kill_transport(job);
				break;
			}
			sent = true;
			job.spec.clear();
		}
		const uint64_t now = OS::get_singleton()->get_ticks_msec();
		if (running && job.cancel.is_set() && !cancel_sent) {
			cancel_sent = true;
			if (job.helper && sent) {
				const char cancel[] = "{\"event\":\"cancel\"}\n";
				pipe->store_buffer((const uint8_t *)cancel, sizeof(cancel) - 1);
			} else {
				kill_transport(job);
			}
		} else if (running && job.helper && sent && !cancel_sent && now - heartbeat > 1000) {
			heartbeat = now;
			const char ping[] = "{\"event\":\"ping\"}\n";
			if (!pipe->store_buffer((const uint8_t *)ping, sizeof(ping) - 1)) {
				job.error = ERR_CONNECTION_ERROR;
				kill_transport(job);
				break;
			}
		}
		OS::get_singleton()->delay_usec(1000);
	}
	if (!job.pending.is_empty()) {
		job.line(String::utf8((const char *)job.pending.ptr(), job.pending.size()));
	}
	if (!job.secret) {
		job.append(String::utf8((const char *)job.raw_out.ptr(), job.raw_out.size()));
		job.append(String::utf8((const char *)job.raw_err.ptr(), job.raw_err.size()));
	}
	if (!job.killed.is_set()) {
		const int transport_code = OS::get_singleton()->get_process_exit_code(pid);
		if (!job.helper) {
			job.exit_code = transport_code;
		} else if (!job.received_result && job.error == OK) {
			job.error = ERR_CONNECTION_ERROR;
		} else if (job.received_result && job.exit_code == 0 && transport_code != 0 && job.error == OK) {
			job.error = ERR_CONNECTION_ERROR;
		}
	}
	if (job.user_cancel.is_set()) {
		job.error = ERR_SKIP;
	}
}
class PassphraseResponse : public RefCounted {
public:
	bool done = false;
	bool accepted = false;
	void confirm() {
		accepted = true;
		done = true;
	}
	void cancel() { done = true; }
};
class JobProgress {
	String task;
	ProgressDialog *dialog = nullptr;

public:
	JobProgress(const String &p_stage) {
		task = "aurora_sdk_" + uitos(OS::get_singleton()->get_ticks_usec());
		// EditorNode caches command-line mode during construction, when a
		// Wayland window may not yet be drawable. The active export UI needs
		// a real cancellable dialog regardless of that startup observation.
		if (String(DisplayServer::get_singleton()->get_name()) != "headless") {
			dialog = ProgressDialog::get_singleton();
		}
		if (dialog) {
			dialog->add_task(task, p_stage, 1, true);
		} else {
			EditorNode::progress_add_task(task, p_stage, 1, true);
		}
	}
	bool step(const String &p_stage) {
		return dialog ? dialog->task_step(task, p_stage, 0, false) : EditorNode::progress_task_step(task, p_stage, 0, false);
	}
	~JobProgress() {
		if (dialog) {
			dialog->end_task(task);
		} else {
			EditorNode::progress_end_task(task);
		}
	}
};
} // namespace

Error execute(const String &p_path, const List<String> &p_arguments, const String &p_stage, String *r_output, int *r_exit_code, const Dictionary &p_secret_environment) {
	Job job;
	job.path = p_path;
	job.stage = p_stage;
	job.secret = !p_secret_environment.is_empty();
	if (p_arguments.size() >= 2 && p_arguments.front()->get() == "engine" && p_arguments.front()->next()->get() == "exec") {
		job.helper = true;
		job.arguments = { "engine", "exec", "python3", "-u", "-c", aurora_sdk_job_script };
		Array argv;
		int position = 0;
		for (const String &argument : p_arguments) {
			if (position++ >= 2) {
				argv.push_back(argument);
			}
		}
		job.spec["argv"] = argv;
		job.spec["secret_environment"] = p_secret_environment;
	} else {
		ERR_FAIL_COND_V(!p_secret_environment.is_empty(), ERR_INVALID_PARAMETER);
		job.arguments = p_arguments;
	}
	Thread thread;
	thread.start(run_job, &job);
	ERR_FAIL_COND_V(!thread.is_started(), ERR_CANT_CREATE);
	JobProgress progress(p_stage);
	uint64_t cancelled_at = 0;
	while (!job.done.is_set()) {
		const bool cancelled = progress.step(p_stage);
		if (cancelled && !job.cancel.is_set()) {
			job.user_cancel.set();
			job.cancel.set();
			cancelled_at = OS::get_singleton()->get_ticks_msec();
		}
		if (job.cancel.is_set() && !cancelled_at) {
			cancelled_at = OS::get_singleton()->get_ticks_msec();
		}
		if (job.cancel.is_set() && cancelled_at && OS::get_singleton()->get_ticks_msec() - cancelled_at > 6000) {
			kill_transport(job);
		}
		OS::get_singleton()->delay_usec(10000);
	}
	thread.wait_to_finish();
	if (r_output) {
		*r_output = job.output;
	}
	if (r_exit_code) {
		*r_exit_code = job.exit_code;
	}
	return job.error;
}

Error request_passphrase(String &r_passphrase) {
	// Headless callers can supply an ephemeral environment value; it is forwarded
	// through stdin, never inserted in the SDK command line or export preset.
	if (OS::get_singleton()->has_environment("GODOT_AURORA_KEY_PASSPHRASE")) {
		r_passphrase = OS::get_singleton()->get_environment("GODOT_AURORA_KEY_PASSPHRASE");
		return OK;
	}
	ERR_FAIL_COND_V_MSG(DisplayServer::get_singleton()->get_name() == "headless", ERR_UNCONFIGURED, "Set GODOT_AURORA_KEY_PASSPHRASE for protected-key headless signing.");
	ERR_FAIL_NULL_V(EditorNode::get_singleton(), ERR_UNCONFIGURED);
	ConfirmationDialog *dialog = memnew(ConfirmationDialog);
	dialog->set_name("AuroraSigningPassphrase");
	dialog->set_title(TTR("Signing key passphrase"));
	dialog->get_label()->hide();
	VBoxContainer *content = memnew(VBoxContainer);
	dialog->add_child(content);
	Label *description = memnew(Label);
	description->set_text(TTR("Enter the passphrase for the selected signing key."));
	content->add_child(description);
	LineEdit *password = memnew(LineEdit);
	password->set_secret(true);
	content->add_child(password);
	dialog->register_text_enter(password);
	Ref<PassphraseResponse> response;
	response.instantiate();
	dialog->connect("confirmed", callable_mp(response.ptr(), &PassphraseResponse::confirm));
	dialog->connect("canceled", callable_mp(response.ptr(), &PassphraseResponse::cancel));
	// Export can be invoked from an exclusive export dialog. Make the prompt
	// transient to the innermost modal window so it can receive real input.
	Window *parent = SceneTree::get_singleton()->get_root();
	while (parent->get_exclusive_child()) {
		parent = parent->get_exclusive_child();
	}
	parent->add_child(dialog);
	dialog->popup_centered(Size2i(480, 130));
	password->grab_focus();
	while (!response->done) {
		DisplayServer::get_singleton()->process_events();
		Main::iteration();
		OS::get_singleton()->delay_usec(10000);
	}
	if (response->accepted) {
		r_passphrase = password->get_text();
	}
	password->set_text("");
	dialog->hide();
	dialog->queue_free();
	return response->accepted ? OK : ERR_SKIP;
}
} // namespace AuroraSDK
