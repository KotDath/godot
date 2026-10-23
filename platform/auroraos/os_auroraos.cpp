/**************************************************************************/
/*  os_auroraos.cpp                                                       */
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

#include "os_auroraos.h"

#include "native_library_map.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/main_loop.h"
#include "main/main.h"
#include "servers/display/display_server.h"

#ifdef WAYLAND_ENABLED
#include "platform/linuxbsd/wayland/display_server_wayland.h"
#endif

void OS_AuroraOS::initialize() {
	OS_Unix::initialize_core();
}

void OS_AuroraOS::finalize() {
	delete_main_loop();
}

void OS_AuroraOS::set_main_loop(MainLoop *p_main_loop) {
	main_loop = p_main_loop;
}

void OS_AuroraOS::delete_main_loop() {
	if (main_loop) {
		memdelete(main_loop);
		main_loop = nullptr;
	}
}

bool OS_AuroraOS::_check_internal_feature_support(const String &p_feature) {
	return p_feature == "auroraos" || p_feature == "mobile";
}

static String xdg_path(const char *p_variable, const char *p_fallback) {
	OS *os = OS::get_singleton();
	String value = os->get_environment(p_variable);
	if (!value.is_absolute_path()) {
		value = os->get_environment("HOME").path_join(p_fallback);
	}
	// Aurora's launcher grants persistent storage to the RPM application ID,
	// not the shared Godot directory. Keep standalone development binaries on
	// ordinary XDG paths; installed editors and games use their own namespace.
	String application_id = os->get_executable_path().get_file();
	if (FileAccess::exists(String("/usr/share/applications").path_join(application_id + ".desktop"))) {
		value = value.path_join(application_id);
	}
	return value;
}

String OS_AuroraOS::get_config_path() const {
	return xdg_path("XDG_CONFIG_HOME", ".config");
}

String OS_AuroraOS::get_data_path() const {
	return xdg_path("XDG_DATA_HOME", ".local/share");
}

String OS_AuroraOS::get_cache_path() const {
	return xdg_path("XDG_CACHE_HOME", ".cache");
}

Error OS_AuroraOS::open_dynamic_library(const String &p_path, void *&p_library_handle, GDExtensionData *p_data) {
	// Regular-profile RPMs keep private DSOs in the application's data tree.
	// Real project-local files take precedence while editing. Exported resources
	// use an exact project-path map, rather than guessing from a basename.
	if (!FileAccess::exists(p_path)) {
		const String data_dir = String("/usr/share").path_join(get_executable_path().get_file());
		const String manifest_path = data_dir.path_join("native-manifest.json");
		Dictionary project_libraries;
		if (FileAccess::exists(manifest_path)) {
			JSON manifest;
			Error error = manifest.parse(FileAccess::get_file_as_string(manifest_path));
			ERR_FAIL_COND_V_MSG(error != OK || manifest.get_data().get_type() != Variant::DICTIONARY, ERR_INVALID_DATA, "Invalid Aurora native library manifest.");
			const Dictionary document = manifest.get_data();
			ERR_FAIL_COND_V_MSG(document.has("schema") && int(document["schema"]) != 2, ERR_INVALID_DATA, "Unsupported Aurora native library manifest schema.");
			if (int(document.get("schema", 0)) == 2) {
				ERR_FAIL_COND_V_MSG(document.get("project_libraries", Variant()).get_type() != Variant::DICTIONARY, ERR_INVALID_DATA, "Missing Aurora project library map.");
				project_libraries = document["project_libraries"];
			}
		}
		if (!project_libraries.is_empty()) {
			String project_path = p_data && p_data->r_resolved_path ? *p_data->r_resolved_path : p_path;
			project_path = ProjectSettings::get_singleton()->localize_path(project_path);
			if (project_path.is_relative_path()) {
				project_path = "res://" + project_path;
			}
			const String relative = AuroraNativeLibrary::mapped_path(project_path, project_libraries);
			ERR_FAIL_COND_V_MSG(relative.is_empty(), ERR_FILE_NOT_FOUND, "No installed Aurora library mapping for " + project_path);
			return OS_Unix::open_dynamic_library(data_dir.path_join(relative), p_library_handle, p_data);
		}
		// Compatibility for earlier RPMs and manually packaged editor libraries.
		String installed = data_dir.path_join("lib").path_join(p_path.get_file());
		if (FileAccess::exists(installed)) {
			return OS_Unix::open_dynamic_library(installed, p_library_handle, p_data);
		}
	}
	return OS_Unix::open_dynamic_library(p_path, p_library_handle, p_data);
}

void OS_AuroraOS::run() {
	if (!main_loop) {
		return;
	}
	main_loop->initialize();
	while (true) {
		DisplayServer::get_singleton()->process_events();
		if (Main::iteration()) {
			break;
		}
	}
	main_loop->finalize();
}

OS_AuroraOS::OS_AuroraOS() {
#ifdef PULSEAUDIO_ENABLED
	AudioDriverManager::add_driver(&audio_driver);
#endif
#ifdef WAYLAND_ENABLED
	DisplayServerWayland::register_wayland_driver();
#endif
}
