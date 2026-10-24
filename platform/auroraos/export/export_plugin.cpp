/**************************************************************************/
/*  export_plugin.cpp                                                     */
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

#include "export_plugin.h"

#include "logo_svg.gen.h"
#include "remote_deploy_state.h"
#include "resources.gen.h"
#include "sdk_executor.h"

#include "core/config/project_settings.h"
#include "core/crypto/crypto_core.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "core/os/shared_object.h"
#include "editor/editor_node.h"
#include "editor/export/editor_export.h"
#include "editor/export/editor_export_preset.h"
#include "editor/file_system/editor_paths.h"
#include "editor/settings/editor_settings.h"

#include "modules/svg/image_loader_svg.h"

namespace {
Error write_text(const String &p_path, const char *p_text) {
	Error error;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &error);
	if (error == OK) {
		file->store_string(String::utf8(p_text));
	}
	return error;
}
struct StagingDirectory {
	String path;
	bool preserve = false;
	~StagingDirectory() {
		if (preserve) {
			return;
		}
		Ref<DirAccess> directory = DirAccess::open(path);
		if (directory.is_valid()) {
			directory->erase_contents_recursive();
			DirAccess::remove_absolute(path);
		}
	}
};
} //namespace

void EditorExportPlatformAuroraOS::initialize() {
	Ref<Image> image;
	image.instantiate();
	ImageLoaderSVG::create_image_from_string(image, _auroraos_logo_svg, 1.0, false, {});
	logo = ImageTexture::create_from_image(image);
}

void EditorExportPlatformAuroraOS::get_export_options(List<ExportOption> *r_options) const {
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "binary_format/architecture", PROPERTY_HINT_ENUM, "arm64,arm32"), "arm64"));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "custom_template/debug", PROPERTY_HINT_GLOBAL_FILE), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "custom_template/release", PROPERTY_HINT_GLOBAL_FILE), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "package/unique_name"), "org.godotengine.game"));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "package/name"), "Godot Game"));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "package/icon", PROPERTY_HINT_FILE, "*.png,*.svg"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "package/version"), "1.0.0"));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "package/release"), "1"));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "package/license"), "Proprietary"));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "package/permissions"), "Audio;UserDirs"));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "sdk/sfdk", PROPERTY_HINT_GLOBAL_FILE), "sfdk"));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "sdk/target"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "sdk/python"), "python3"));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "sdk/patchelf", PROPERTY_HINT_GLOBAL_FILE), "patchelf"));
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "signing/enabled"), false));
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "remote_deploy/enabled"), false));
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "textures/etc2_astc"), true));
}

void EditorExportPlatformAuroraOS::get_platform_features(List<String> *r_features) const {
	r_features->push_back("auroraos");
	r_features->push_back("mobile");
}

void EditorExportPlatformAuroraOS::get_preset_features(const Ref<EditorExportPreset> &p_preset, List<String> *r_features) const {
	r_features->push_back(p_preset->get("binary_format/architecture"));
	if (bool(p_preset->get("textures/etc2_astc"))) {
		r_features->push_back("etc2");
		r_features->push_back("astc");
	}
}

List<String> EditorExportPlatformAuroraOS::get_binary_extensions(const Ref<EditorExportPreset> &p_preset) const {
	List<String> extensions;
	extensions.push_back("rpm");
	return extensions;
}

String EditorExportPlatformAuroraOS::template_path(const Ref<EditorExportPreset> &p_preset, bool p_debug) const {
	String custom = p_preset->get(p_debug ? "custom_template/debug" : "custom_template/release");
	if (!custom.is_empty()) {
		return ProjectSettings::get_singleton()->globalize_path(custom);
	}
	String arch = p_preset->get("binary_format/architecture");
	return find_export_template("auroraos_" + String(p_debug ? "debug" : "release") + "." + arch);
}

bool EditorExportPlatformAuroraOS::has_valid_export_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error, bool &r_missing_templates, bool p_debug) const {
	r_missing_templates = !FileAccess::exists(template_path(p_preset, p_debug));
	if (r_missing_templates) {
		r_error += "Select an Aurora export template for the chosen architecture and build type.\n";
	}
	String target = p_preset->get("sdk/target");
	String arch = p_preset->get("binary_format/architecture");
	if (target.ends_with(".default")) {
		target = target.trim_suffix(".default");
	}
	if (!target.ends_with(arch == "arm32" ? "-armv7hl" : "-aarch64")) {
		r_error += "Select an Aurora SDK target matching the export architecture.\n";
	}
	if (OS::get_singleton()->get_name() != "Linux") {
		r_error += "RPM export currently requires the Linux Aurora SDK frontend.\n";
	}
	return r_error.is_empty();
}

bool EditorExportPlatformAuroraOS::has_valid_project_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error) const {
	String name = p_preset->get("package/unique_name");
	Vector<String> parts = name.split(".");
	bool valid = parts.size() >= 3;
	for (const String &part : parts) {
		valid = valid && part.is_valid_ascii_identifier();
	}
	if (!valid) {
		r_error += "Package identifier must have at least three components, such as org.example.game.\n";
	}
	return valid;
}

Error EditorExportPlatformAuroraOS::export_project(const Ref<EditorExportPreset> &p_preset, bool p_debug, const String &p_path, BitField<DebugFlags> p_flags, bool p_notify) {
	return export_project_internal(p_preset, p_debug, p_path, p_flags, p_notify, p_preset->get("package/release"));
}

Error EditorExportPlatformAuroraOS::export_project_internal(const Ref<EditorExportPreset> &p_preset, bool p_debug, const String &p_path, BitField<DebugFlags> p_flags, bool p_notify, const String &p_release) {
	ExportNotifier notifier(*this, p_preset, p_debug, p_path, p_flags, p_notify);
	String configuration_error;
	bool missing_templates = false;
	if (!has_valid_export_configuration(p_preset, configuration_error, missing_templates, p_debug) || !has_valid_project_configuration(p_preset, configuration_error)) {
		add_message(EXPORT_MESSAGE_ERROR, "Aurora export", configuration_error);
		return ERR_UNCONFIGURED;
	}
	if (p_flags != 0) {
		add_message(EXPORT_MESSAGE_ERROR, "Aurora export", "Remote debugging launch flags are not supported yet.");
		return ERR_UNAVAILABLE;
	}
	String destination = ProjectSettings::get_singleton()->globalize_path(p_path);
	String stage_path = destination.get_base_dir().path_join(".godot-aurora-" + itos(OS::get_singleton()->get_process_id()) + "-" + uitos(OS::get_singleton()->get_ticks_usec()));
	Error error = DirAccess::make_dir_absolute(stage_path);
	ERR_FAIL_COND_V(error != OK, error);
	StagingDirectory staging{ stage_path };
	error = write_text(stage_path.path_join("package.py"), aurora_package_script);
	ERR_FAIL_COND_V(error != OK, error);
	error = write_text(stage_path.path_join("LICENSE.txt"), aurora_engine_license);
	ERR_FAIL_COND_V(error != OK, error);
	error = write_text(stage_path.path_join("COPYRIGHT.txt"), aurora_engine_copyright);
	ERR_FAIL_COND_V(error != OK, error);
	error = DirAccess::make_dir_absolute(stage_path.path_join("icons"));
	ERR_FAIL_COND_V(error != OK, error);
	String icon_path = p_preset->get("package/icon");
	if (icon_path.is_empty()) {
		icon_path = ProjectSettings::get_singleton()->get("application/config/icon");
	}
	Ref<Image> custom_icon;
	if (!icon_path.is_empty()) {
		custom_icon = Image::load_from_file(ProjectSettings::get_singleton()->globalize_path(icon_path));
		if (custom_icon.is_null() || custom_icon->is_empty()) {
			add_message(EXPORT_MESSAGE_ERROR, "Aurora icon", "Could not load application icon: " + icon_path);
			return ERR_FILE_CANT_READ;
		}
	}
	for (int size : { 86, 108, 128, 172 }) {
		Ref<Image> image;
		image.instantiate();
		if (custom_icon.is_valid()) {
			image = custom_icon->duplicate();
			image->resize(size, size, Image::INTERPOLATE_LANCZOS);
		} else {
			ImageLoaderSVG::create_image_from_string(image, _auroraos_logo_svg, size / 64.0, false, {});
		}
		error = image->save_png(stage_path.path_join("icons").path_join(itos(size) + ".png"));
		ERR_FAIL_COND_V(error != OK, error);
	}
	Vector<SharedObject> shared_objects;
	String pack_path = stage_path.path_join("game.pck");
	error = save_pack(p_preset, p_debug, pack_path, &shared_objects);
	ERR_FAIL_COND_V(error != OK, error);

	String arch = p_preset->get("binary_format/architecture");
	String rpm_arch = arch == "arm32" ? "armv7hl" : "aarch64";
	String name = p_preset->get("package/unique_name");
	String version = p_preset->get("package/version");
	String release = p_release;
	List<String> arguments;
	for (const String &argument : { String("engine"), String("exec"), String("sb2"), String("-t"), String(p_preset->get("sdk/target")), String("-m"), String("sdk-build"), String(p_preset->get("sdk/python")), stage_path.path_join("package.py") }) {
		arguments.push_back(argument);
	}
	auto option = [&](const String &p_name, const String &p_value) {
		arguments.push_back(p_name);
		arguments.push_back(p_value);
	};
	option("--binary", template_path(p_preset, p_debug));
	option("--pck", pack_path);
	option("--pck-sha256", FileAccess::get_sha256(pack_path));
	option("--arch", arch);
	option("--name", name);
	option("--title", p_preset->get("package/name"));
	option("--summary", p_preset->get("package/name"));
	option("--version", version);
	option("--release", release);
	option("--rpm-license", p_preset->get("package/license"));
	option("--permissions", p_preset->get("package/permissions"));
	option("--patchelf", p_preset->get("sdk/patchelf"));
	option("--engine-license", stage_path.path_join("LICENSE.txt"));
	option("--engine-copyright", stage_path.path_join("COPYRIGHT.txt"));
	option("--output", stage_path.path_join("rpms"));
	arguments.push_back("--bundle-xkbcommon");
	Array native_libraries;
	HashSet<String> seen_paths;
	for (const SharedObject &object : shared_objects) {
		String path = ProjectSettings::get_singleton()->globalize_path(object.path);
		String project = ProjectSettings::get_singleton()->localize_path(object.path).simplify_path();
		if (!project.begins_with("res://")) {
			add_message(EXPORT_MESSAGE_ERROR, "Aurora native library", "Native libraries must be inside the project: " + object.path);
			return ERR_INVALID_DATA;
		}
		String identity = project + "\n" + object.target;
		if (seen_paths.has(identity)) {
			continue;
		}
		seen_paths.insert(identity);
		Dictionary library;
		library["source"] = path;
		library["project"] = project;
		library["target"] = object.target;
		library["sha256"] = FileAccess::get_sha256(path);
		native_libraries.push_back(library);
	}
	Dictionary native_map;
	native_map["schema"] = 1;
	native_map["libraries"] = native_libraries;
	String native_map_path = stage_path.path_join("native-input.json");
	Ref<FileAccess> native_map_file = FileAccess::open(native_map_path, FileAccess::WRITE, &error);
	ERR_FAIL_COND_V(error != OK, error);
	native_map_file->store_string(JSON::stringify(native_map));
	native_map_file.unref();
	option("--native-map", native_map_path);
	String output;
	int exit_code = -1;
	error = AuroraSDK::execute(p_preset->get("sdk/sfdk"), arguments, "Packaging Aurora RPM", &output, &exit_code);
	if (error == ERR_SKIP) {
		return error;
	}
	if (error != OK || exit_code != 0) {
		add_message(EXPORT_MESSAGE_ERROR, "Aurora RPM", "SDK packaging failed. Templates and output directory must be visible in the SDK shared workspace.\n" + output);
		return error != OK ? error : ERR_CANT_CREATE;
	}
	String rpm = stage_path.path_join("rpms").path_join(rpm_arch).path_join(name + "-" + version + "-" + release + "." + rpm_arch + ".rpm");
	List<String> validation_arguments;
	for (const String &argument : { String("engine"), String("exec"), String("rpm-validator"), String("-p"), String("regular"), rpm }) {
		validation_arguments.push_back(argument);
	}
	output = String();
	exit_code = -1;
	error = AuroraSDK::execute(p_preset->get("sdk/sfdk"), validation_arguments, "Validating Aurora RPM", &output, &exit_code);
	if (error == ERR_SKIP) {
		return error;
	}
	if (error != OK || (exit_code != 0 && exit_code != 2)) {
		add_message(EXPORT_MESSAGE_ERROR, "Aurora validation", output);
		return error != OK ? error : ERR_INVALID_DATA;
	}
	if (exit_code == 2 || output.contains("WARNING")) {
		add_message(EXPORT_MESSAGE_WARNING, "Aurora validation", output);
	}
	if (bool(p_preset->get("signing/enabled"))) {
		String key = EDITOR_GET("export/auroraos/signing_key");
		String cert = EDITOR_GET("export/auroraos/signing_certificate");
		if (!FileAccess::exists(key) || !FileAccess::exists(cert)) {
			add_message(EXPORT_MESSAGE_ERROR, "Aurora signing", "Set the signing key and certificate in Editor Settings > Export > AuroraOS.");
			return ERR_UNCONFIGURED;
		}
		const bool protected_key = FileAccess::get_file_as_string(key).contains("ENCRYPTED");
		String sfdk = p_preset->get("sdk/sfdk");
		error = sdk_command(sfdk, { "engine", "exec", "rpmsign-external", "info", cert }, "Aurora certificate", &output);
		if (error != OK) {
			return error;
		}
		if (!output.contains("Subgroup: regular")) {
			add_message(EXPORT_MESSAGE_ERROR, "Aurora signing", "This exporter validates the Regular profile; select a Regular developer certificate.");
			return ERR_INVALID_DATA;
		}
		error = sdk_command(sfdk, { "engine", "exec", "rpmsign-external", "dump", rpm }, "Aurora signature inspection");
		if (error != OK) {
			return error;
		}
		// A freshly built staged RPM is signed without --force. Existing output
		// packages are not modified if signing fails.
		Dictionary secret_environment;
		if (protected_key) {
			String passphrase;
			error = AuroraSDK::request_passphrase(passphrase);
			if (error != OK) {
				return error;
			}
			secret_environment["KEY_PASSPHRASE"] = passphrase;
		}
		error = sdk_command(sfdk, { "engine", "exec", "rpmsign-external", "sign", "--key", key, "--cert", cert, rpm }, "Aurora signing", nullptr, secret_environment);
		secret_environment.clear();
		if (error != OK) {
			return error;
		}
		error = sdk_command(sfdk, { "engine", "exec", "rpmsign-external", "dump", rpm }, "Aurora signature", &output);
		if (error != OK) {
			return error;
		}
		add_message(EXPORT_MESSAGE_INFO, "Aurora signature", output + "\nRoot-CA trust verification was not performed.");
	}
	error = DirAccess::copy_absolute(rpm, destination);
	if (error != OK) {
		add_message(EXPORT_MESSAGE_ERROR, "Aurora RPM", "Could not copy the completed RPM to " + destination);
	} else {
		add_message(EXPORT_MESSAGE_INFO, "Aurora RPM", bool(p_preset->get("signing/enabled")) ? "Signed RPM created and validated for the Regular profile." : "Unsigned RPM created and validated for the Regular profile. Sign it with a matching certificate before installation.");
	}
	return error;
}

Error EditorExportPlatformAuroraOS::sdk_command(const String &p_sfdk, const List<String> &p_arguments, const String &p_stage, String *r_output, const Dictionary &p_secret_environment) {
	String output;
	int exit_code = -1;
	Error error = AuroraSDK::execute(p_sfdk, p_arguments, p_stage, &output, &exit_code, p_secret_environment);
	if (r_output) {
		*r_output = output;
	}
	if (error == ERR_SKIP) {
		return error;
	}
	if (error != OK || exit_code != 0) {
		if (!p_secret_environment.is_empty()) {
			output += "Protected-key signing failed. Check the passphrase and selected key/certificate pair.\n";
		}
		add_message(EXPORT_MESSAGE_ERROR, p_stage, "SDK command failed (exit " + itos(exit_code) + ").\n" + output);
		return error != OK ? error : FAILED;
	}
	return OK;
}

Error EditorExportPlatformAuroraOS::load_session() {
	if (session_loaded) {
		return session_error;
	}
	if (!EditorPaths::get_singleton()) {
		return ERR_UNCONFIGURED;
	}
	session_path = ProjectSettings::get_singleton()->globalize_path(EditorPaths::get_singleton()->get_project_settings_dir()).path_join("aurora-remote-session.json");
	session_error = AuroraDeploy::load_record(session_path, remote_session);
	session_loaded = true;
	return session_error;
}

Error EditorExportPlatformAuroraOS::save_session(const Dictionary &p_record) {
	Error error = load_session();
	if (error == OK) {
		error = AuroraDeploy::save_record(session_path, p_record);
	}
	if (error == OK) {
		remote_session = p_record.duplicate(true);
	} else {
		add_message(EXPORT_MESSAGE_ERROR, "Aurora session", "Could not save the remote operation record: " + session_path);
	}
	return error;
}

Error EditorExportPlatformAuroraOS::remote_action(const String &p_action, Dictionary &r_status, const List<String> &p_launch_arguments) {
	ERR_FAIL_COND_V(remote_session.is_empty(), ERR_UNCONFIGURED);
	List<String> arguments{ "device", "exec", remote_session["device"], "--", "sh", "-c", aurora_remote_session_script, "--", p_action, remote_session["token"] };
	for (const String &argument : p_launch_arguments) {
		arguments.push_back(argument);
	}
	String output;
	Error error = sdk_command(remote_session["sfdk"], arguments, "Aurora " + p_action, &output);
	if (error != OK) {
		add_message(EXPORT_MESSAGE_WARNING, "Aurora session", "Remote operation was not confirmed. Its session record is retained. Reconnect and use Stop Aurora project before replacing the launch.");
		return error;
	}
	error = AuroraDeploy::parse_status(output, remote_session["token"], r_status);
	if (error != OK) {
		add_message(EXPORT_MESSAGE_ERROR, "Aurora session", "SDK returned an invalid session response. The operation record is retained for recovery.");
	}
	return error;
}

bool EditorExportPlatformAuroraOS::poll_export() {
	load_session(); // Local file only; polling never contacts the device.
	Ref<EditorExportPreset> preset = EditorExport::get_singleton()->get_runnable_preset_for_platform(this);
	int previous = menu_options;
	menu_options = (preset.is_valid() && bool(preset->get("remote_deploy/enabled"))) ? 1 : 0;
	if (!remote_session.is_empty()) {
		menu_options = 2;
	}
	return previous != menu_options;
}

String EditorExportPlatformAuroraOS::get_option_label(int p_index) const {
	return p_index == 0 ? "Run on Aurora SDK device" : "Stop Aurora project";
}

String EditorExportPlatformAuroraOS::get_option_tooltip(int p_index) const {
	return p_index == 0 ? "Export, sign, install and launch on the device selected in Editor Settings > Export > AuroraOS" : "Cancel the saved launch and stop its owned process; keep the installed package and user data";
}

Error EditorExportPlatformAuroraOS::stop_remote() {
	Error error = load_session();
	if (error != OK) {
		add_message(EXPORT_MESSAGE_ERROR, "Aurora session", "Cannot read the saved session record: " + session_path);
		return error;
	}
	if (remote_session.is_empty()) {
		return OK;
	}
	Dictionary pending = remote_session.duplicate(true);
	pending["phase"] = "stopping";
	error = save_session(pending);
	if (error != OK) {
		return error;
	}
	Dictionary status;
	error = remote_action("stop", status);
	if (error != OK) {
		return error;
	}
	String state = status["state"];
	if (state != "stopped" && state != "exited" && state != "failed") {
		add_message(EXPORT_MESSAGE_ERROR, "Aurora stop", "Process cleanup is not confirmed (" + state + "). The saved session remains available for another Stop attempt.");
		return ERR_BUSY;
	}
	if (bool(remote_session.get("install_unconfirmed", false))) {
		String archive = session_path.get_base_dir().path_join("aurora-install-uncertain-" + String(remote_session["token"]) + ".json");
		error = AuroraDeploy::save_record(archive, remote_session);
		if (error != OK) {
			return error;
		}
		add_message(EXPORT_MESSAGE_WARNING, "Aurora installation", "Game launch is cancelled. SDK installation outcome is still unconfirmed; its package and operation record are retained at " + archive + ". Stop does not roll back an installation.");
	}
	error = DirAccess::remove_absolute(session_path);
	if (error == OK) {
		remote_session.clear();
	}
	return error;
}

Error EditorExportPlatformAuroraOS::run(const Ref<EditorExportPreset> &p_preset, int p_device, BitField<DebugFlags> p_debug_flags) {
	ERR_FAIL_COND_V(p_device < 0 || p_device > 1, ERR_INVALID_PARAMETER);
	if (p_device == 1) {
		return stop_remote();
	}
	Error error = load_session();
	if (error != OK) {
		add_message(EXPORT_MESSAGE_ERROR, "Aurora session", "Cannot read the saved session record: " + session_path);
		return error;
	}
	if (!remote_session.is_empty() && String(remote_session["phase"]) != "running") {
		if (bool(remote_session.get("install_unconfirmed", false))) {
			add_message(EXPORT_MESSAGE_ERROR, "Aurora installation", "The previous SDK installation was not confirmed. Use Stop Aurora project to fence its pending launch before retrying; the installation itself may still be in progress.");
			return ERR_BUSY;
		}
		if (String(remote_session["phase"]) == "launching") {
			Dictionary status;
			error = remote_action("status", status);
			if (error != OK) {
				return error;
			}
			if (String(status["state"]) == "running") {
				Dictionary recovered = remote_session.duplicate(true);
				recovered["phase"] = "running";
				recovered["status"] = status;
				error = save_session(recovered);
				if (error == OK) {
					add_message(EXPORT_MESSAGE_INFO, "Aurora launch", "Recovered the existing launch on " + String(remote_session["device"]) + "; no second installation or game was submitted.");
				}
				return error;
			}
		}
		// Even a missing reply must be fenced: its request could arrive later.
		error = stop_remote();
		if (error != OK) {
			return error;
		}
	}
	String device = EDITOR_GET("export/auroraos/device");
	if (device.is_empty() || !bool(p_preset->get("signing/enabled"))) {
		add_message(EXPORT_MESSAGE_ERROR, "Aurora deploy", "Select an SDK device in Editor Settings and enable signing in the export preset.");
		return ERR_UNCONFIGURED;
	}
	if (p_debug_flags.has_flag(DEBUG_FLAG_DUMB_CLIENT) || p_debug_flags.has_flag(DEBUG_FLAG_REMOTE_DEBUG_LOCALHOST)) {
		add_message(EXPORT_MESSAGE_ERROR, "Aurora deploy", "Remote filesystem and localhost forwarding are not supported. Use an embedded project and a PC debug address reachable from the device.");
		return ERR_UNAVAILABLE;
	}
	String arch = p_preset->get("binary_format/architecture");
	if (!AuroraDeploy::supports_session(FileAccess::open(template_path(p_preset, true), FileAccess::READ), arch)) {
		add_message(EXPORT_MESSAGE_ERROR, "Aurora deploy", "Rebuild the Aurora debug export template with native remote-session support. Older templates remain usable for ordinary RPM export.");
		return ERR_UNAVAILABLE;
	}
	EditorProgress progress("aurora_run", "Running on Aurora OS", 4);
	String sfdk = p_preset->get("sdk/sfdk");
	String machine;
	error = sdk_command(sfdk, { "device", "exec", device, "--", "uname", "-m" }, "Aurora device", &machine);
	if (error != OK) {
		return error;
	}
	if (machine.strip_edges() != (arch == "arm32" ? "armv7l" : "aarch64")) {
		add_message(EXPORT_MESSAGE_ERROR, "Aurora device", "Device architecture does not match the export preset: " + machine);
		return ERR_INVALID_DATA;
	}
	String stage = ProjectSettings::get_singleton()->globalize_path("res://.godot/aurora-deploy").path_join(itos(OS::get_singleton()->get_process_id()) + "-" + uitos(OS::get_singleton()->get_ticks_usec()));
	error = DirAccess::make_dir_recursive_absolute(stage);
	ERR_FAIL_COND_V(error != OK, error);
	StagingDirectory staging{ stage };
	String deploy_release = String(p_preset->get("package/release")) + ".godot." + uitos(uint64_t(OS::get_singleton()->get_unix_time() * 1000000.0));
	String rpm_name = String(p_preset->get("package/unique_name")) + "-" + String(p_preset->get("package/version")) + "-" + deploy_release + "." + (arch == "arm32" ? "armv7hl" : "aarch64") + ".rpm";
	ERR_FAIL_COND_V(rpm_name != rpm_name.validate_filename(), ERR_INVALID_PARAMETER);
	String rpm = stage.path_join(rpm_name);
	if (progress.step("Exporting and signing RPM", 0)) {
		return ERR_SKIP;
	}
	error = export_project_internal(p_preset, true, rpm, 0, true, deploy_release);
	if (error != OK) {
		return error;
	}
	if (progress.step("Stopping previous project", 1)) {
		return ERR_SKIP;
	}
	error = stop_remote();
	if (error != OK) {
		return error;
	}
	if (progress.step("Installing RPM through SDK", 2)) {
		return ERR_SKIP;
	}
	CryptoCore::RandomGenerator random;
	uint8_t bytes[16];
	error = random.init();
	if (error == OK) {
		error = random.get_random_bytes(bytes, sizeof(bytes));
	}
	ERR_FAIL_COND_V(error != OK, error);
	Dictionary record;
	record["schema"] = 1;
	record["token"] = String::hex_encode_buffer(bytes, sizeof(bytes));
	record["sfdk"] = sfdk;
	record["device"] = device;
	record["package"] = p_preset->get("package/unique_name");
	record["rpm"] = rpm_name;
	record["staging"] = stage;
	record["phase"] = "installing";
	record["install_unconfirmed"] = true;
	error = save_session(record);
	if (error != OK) {
		return error;
	}
	error = sdk_command(sfdk, { "-c", "device=" + device, "deploy", "--sdk", "--keepUserData", "--", rpm }, "Aurora installation");
	if (error != OK) {
		staging.preserve = true;
		add_message(EXPORT_MESSAGE_WARNING, "Aurora installation", "Installation was not confirmed. The signed RPM is retained at " + rpm + "; no game launch was submitted.");
		return error;
	}
	record["phase"] = "launching";
	record["install_unconfirmed"] = false;
	error = save_session(record);
	if (error != OK) {
		staging.preserve = true;
		return error;
	}
	if (progress.step("Launching debug project", 3)) {
		return ERR_SKIP;
	}
	List<String> launch_arguments{ "/usr/bin/" + String(p_preset->get("package/unique_name")) };
	for (const String &argument : gen_export_flags(p_debug_flags)) {
		launch_arguments.push_back(argument);
	}
	Dictionary status;
	error = remote_action("launch", status, launch_arguments);
	if (error != OK) {
		return error;
	}
	if (String(status["state"]) != "running") {
		add_message(EXPORT_MESSAGE_ERROR, "Aurora launch", "Game startup is not confirmed (" + String(status["state"]) + "). Its saved session remains available for Run recovery or Stop.");
		return ERR_BUSY;
	}
	record["phase"] = "running";
	record["status"] = status;
	error = save_session(record);
	if (error == OK) {
		add_message(EXPORT_MESSAGE_INFO, "Aurora launch", "Started PID " + String(status["pid"]) + " on " + device + ". Log: ~/.cache/godot-aurora/" + String(record["token"]) + ".log");
	}
	return error;
}
