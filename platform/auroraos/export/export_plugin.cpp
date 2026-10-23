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
#include "resources.gen.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "core/os/shared_object.h"
#include "editor/export/editor_export_preset.h"

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
	~StagingDirectory() {
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
	String release = p_preset->get("package/release");
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
	HashSet<String> seen_paths;
	for (const SharedObject &object : shared_objects) {
		String path = ProjectSettings::get_singleton()->globalize_path(object.path);
		if (seen_paths.has(path)) {
			continue;
		}
		seen_paths.insert(path);
		option("--private-library", path);
	}
	String output;
	int exit_code = -1;
	error = OS::get_singleton()->execute(p_preset->get("sdk/sfdk"), arguments, &output, &exit_code, true);
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
	error = OS::get_singleton()->execute(p_preset->get("sdk/sfdk"), validation_arguments, &output, &exit_code, true);
	if (error != OK || (exit_code != 0 && exit_code != 2)) {
		add_message(EXPORT_MESSAGE_ERROR, "Aurora validation", output);
		return error != OK ? error : ERR_INVALID_DATA;
	}
	if (exit_code == 2 || output.contains("WARNING")) {
		add_message(EXPORT_MESSAGE_WARNING, "Aurora validation", output);
	}
	error = DirAccess::copy_absolute(rpm, destination);
	if (error != OK) {
		add_message(EXPORT_MESSAGE_ERROR, "Aurora RPM", "Could not copy the completed RPM to " + destination);
	} else {
		add_message(EXPORT_MESSAGE_INFO, "Aurora RPM", "Unsigned RPM created and validated for the Regular profile. Sign it with a matching certificate before installation.");
	}
	return error;
}
