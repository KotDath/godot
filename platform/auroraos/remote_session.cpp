/**************************************************************************/
/*  remote_session.cpp                                                    */
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

#include "remote_session.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
const char session_protocol[] = "GODOT_AURORA_REMOTE_SESSION_V1";
volatile sig_atomic_t stop_requested = 0;

void on_stop(int) {
	stop_requested = 1;
}

bool exists(const std::string &p_path) {
	struct stat info;
	return stat(p_path.c_str(), &info) == 0;
}

std::string read_line(const std::string &p_path) {
	std::ifstream file(p_path);
	std::string line;
	std::getline(file, line);
	return line;
}

std::string process_start(pid_t p_pid) {
	std::string stat = read_line("/proc/" + std::to_string(p_pid) + "/stat");
	const size_t end = stat.rfind(") ");
	if (end == std::string::npos) {
		return {};
	}
	std::istringstream fields(stat.substr(end + 2));
	std::string value;
	for (int field = 0; field < 20; field++) {
		if (!(fields >> value)) {
			return {};
		}
	}
	return value;
}

bool write_atomic(const std::string &p_path, const std::string &p_value) {
	const std::string temporary = p_path + ".tmp." + std::to_string(getpid());
	int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0) {
		return false;
	}
	size_t offset = 0;
	while (offset < p_value.size()) {
		ssize_t count = write(fd, p_value.data() + offset, p_value.size() - offset);
		if (count < 0 && errno == EINTR) {
			continue;
		}
		if (count <= 0) {
			close(fd);
			unlink(temporary.c_str());
			return false;
		}
		offset += count;
	}
	bool success = fsync(fd) == 0;
	if (close(fd) != 0) {
		success = false;
	}
	if (success) {
		success = rename(temporary.c_str(), p_path.c_str()) == 0;
	}
	if (!success) {
		unlink(temporary.c_str());
	}
	return success;
}

void pause_tick() {
	timespec duration{ 0, 100000000 };
	nanosleep(&duration, nullptr);
}

bool child_exited(pid_t p_pid) {
	siginfo_t info{};
	int result = waitid(P_PID, p_pid, &info, WEXITED | WNOHANG | WNOWAIT);
	return result == 0 && info.si_pid == p_pid;
}

int supervise(const std::string &p_session, const std::string &p_boot, char **p_arguments) {
	// The worker, rather than the SSH request, reserves the session. A late
	// duplicate cannot pass this mkdir, including after Stop created a tombstone.
	if (mkdir(p_session.c_str(), 0700) != 0) {
		return errno == EEXIST ? 0 : 73;
	}
	signal(SIGHUP, SIG_IGN);
	signal(SIGTERM, on_stop);
	signal(SIGINT, on_stop);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGCHLD, SIG_DFL);
	const std::string manager_start = process_start(getpid());
	if (manager_start.empty() || !write_atomic(p_session + "/manager", p_boot + " " + std::to_string(getpid()) + " " + manager_start + "\n")) {
		write_atomic(p_session + "/error", "manager\n");
		return 74;
	}
	int gate[2];
	if (pipe(gate) != 0) {
		write_atomic(p_session + "/error", "pipe\n");
		return 71;
	}
	pid_t child = fork();
	if (child < 0) {
		close(gate[0]);
		close(gate[1]);
		write_atomic(p_session + "/error", "fork\n");
		return 71;
	}
	if (child == 0) {
		close(gate[1]);
		signal(SIGTERM, SIG_DFL);
		signal(SIGINT, SIG_DFL);
		signal(SIGPIPE, SIG_DFL);
		if (setsid() < 0) {
			_exit(71);
		}
		char ready = 0;
		ssize_t count;
		do {
			count = read(gate[0], &ready, 1);
		} while (count < 0 && errno == EINTR);
		close(gate[0]);
		if (count != 1 || ready != 'G') {
			_exit(0);
		}
		execv("/proc/self/exe", p_arguments);
		_exit(127);
	}
	close(gate[0]);
	const std::string start = process_start(child);
	bool published = !start.empty() && write_atomic(p_session + "/process", p_boot + " " + std::to_string(child) + " " + start + "\n");
	if (published && !stop_requested && !exists(p_session + "/stop")) {
		const char ready = 'G';
		published = write(gate[1], &ready, 1) == 1;
	}
	close(gate[1]);
	if (!published) {
		write_atomic(p_session + "/error", "publish\n");
	}
	while (published && !stop_requested && !exists(p_session + "/stop") && !child_exited(child)) {
		pause_tick();
	}
	// Never poll/waitpid-reap the leader before sending the final group signal.
	// Its unreaped PID prevents the group ID from being reused by another job.
	kill(-child, SIGTERM);
	// A child stopped before setsid() might not have its own group yet. Its
	// individual PID is still ours and unreaped, so this fallback is also safe.
	kill(child, SIGTERM);
	for (int tick = 0; tick < 20; tick++) {
		pause_tick();
	}
	kill(-child, SIGKILL);
	kill(child, SIGKILL);
	int status = 0;
	while (waitpid(child, &status, 0) < 0) {
		if (errno != EINTR) {
			write_atomic(p_session + "/error", "wait\n");
			return 71;
		}
	}
	int code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
	write_atomic(p_session + "/exit", std::to_string(code) + "\n");
	return 0;
}
} // namespace

bool aurora_remote_session_main(int p_argc, char **p_argv, int &r_exit_code) {
	if (p_argc == 2 && strcmp(p_argv[1], "--aurora-remote-session-version") == 0) {
		puts(session_protocol);
		r_exit_code = 0;
		return true;
	}
	if (p_argc < 2 || strcmp(p_argv[1], "--aurora-remote-session") != 0) {
		return false;
	}
	r_exit_code = 64;
	if (p_argc < 3 || strlen(p_argv[2]) != 32 || strspn(p_argv[2], "0123456789abcdef") != 32) {
		return true;
	}
	const char *home = getenv("HOME");
	if (!home || home[0] != '/') {
		return true;
	}
	umask(077);
	const std::string cache = std::string(home) + "/.cache";
	const std::string base = cache + "/godot-aurora";
	const std::string sessions = base + "/sessions";
	for (const std::string &path : { cache, base, sessions }) {
		if (mkdir(path.c_str(), 0700) != 0 && errno != EEXIST) {
			r_exit_code = 73;
			return true;
		}
	}
	const std::string boot = read_line("/proc/sys/kernel/random/boot_id");
	if (boot.empty()) {
		r_exit_code = 69;
		return true;
	}
	const std::string token = p_argv[2];
	const std::string log = base + "/" + token + ".log";
	int input = open("/dev/null", O_RDONLY | O_CLOEXEC);
	int output = open(log.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (input < 0 || output < 0) {
		if (input >= 0) {
			close(input);
		}
		if (output >= 0) {
			close(output);
		}
		r_exit_code = 74;
		return true;
	}
	std::vector<char *> arguments{ p_argv[0] };
	for (int argument = 3; argument < p_argc; argument++) {
		arguments.push_back(p_argv[argument]);
	}
	arguments.push_back(nullptr);
	pid_t manager = fork();
	if (manager == 0) {
		if (setsid() < 0 || dup2(input, STDIN_FILENO) < 0 || dup2(output, STDOUT_FILENO) < 0 || dup2(output, STDERR_FILENO) < 0) {
			_exit(71);
		}
		close(input);
		close(output);
		_exit(supervise(sessions + "/" + token, boot, arguments.data()));
	}
	close(input);
	close(output);
	r_exit_code = manager < 0 ? 71 : 0;
	return true;
}
