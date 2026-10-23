/**************************************************************************/
/*  export_plugin.h                                                       */
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

#pragma once

#include "editor/export/editor_export_platform.h"
#include "scene/resources/image_texture.h"

class EditorExportPlatformAuroraOS : public EditorExportPlatform {
	GDCLASS(EditorExportPlatformAuroraOS, EditorExportPlatform);
	Ref<ImageTexture> logo;
	int menu_options = 0;
	String running_sfdk;
	String running_device;
	String running_pid;
	String running_start;
	Error sdk_command(const String &p_sfdk, const List<String> &p_arguments, const String &p_stage, String *r_output = nullptr);
	Error stop_remote();
	Error export_project_internal(const Ref<EditorExportPreset> &p_preset, bool p_debug, const String &p_path, BitField<DebugFlags> p_flags, bool p_notify, const String &p_release);
	String template_path(const Ref<EditorExportPreset> &p_preset, bool p_debug) const;

public:
	String get_name() const override { return "Aurora OS"; }
	String get_os_name() const override { return "AuroraOS"; }
	Ref<Texture2D> get_logo() const override { return logo; }
	void initialize() override;
	bool poll_export() override;
	int get_options_count() const override { return menu_options; }
	bool is_option_runnable(int p_index) const override { return p_index == 0; }
	String get_option_label(int p_index) const override;
	String get_option_tooltip(int p_index) const override;
	Error run(const Ref<EditorExportPreset> &p_preset, int p_device, BitField<DebugFlags> p_debug_flags) override;
	void get_export_options(List<ExportOption> *r_options) const override;
	void get_preset_features(const Ref<EditorExportPreset> &p_preset, List<String> *r_features) const override;
	void get_platform_features(List<String> *r_features) const override;
	List<String> get_binary_extensions(const Ref<EditorExportPreset> &p_preset) const override;
	bool has_valid_export_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error, bool &r_missing_templates, bool p_debug = false) const override;
	bool has_valid_project_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error) const override;
	Error export_project(const Ref<EditorExportPreset> &p_preset, bool p_debug, const String &p_path, BitField<DebugFlags> p_flags = 0, bool p_notify = true) override;
};
