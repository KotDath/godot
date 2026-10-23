/**************************************************************************/
/*  os_auroraos.h                                                         */
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

#include "drivers/unix/os_unix.h"

#ifdef PULSEAUDIO_ENABLED
#include "drivers/pulseaudio/audio_driver_pulseaudio.h"
#endif

class OS_AuroraOS : public OS_Unix {
	MainLoop *main_loop = nullptr;
#ifdef PULSEAUDIO_ENABLED
	AudioDriverPulseAudio audio_driver;
#endif

protected:
	void initialize() override;
	void initialize_joypads() override {}
	void finalize() override;
	void set_main_loop(MainLoop *p_main_loop) override;
	void delete_main_loop() override;
	bool _check_internal_feature_support(const String &p_feature) override;

public:
	String get_identifier() const override { return "auroraos"; }
	String get_name() const override { return "AuroraOS"; }
	MainLoop *get_main_loop() const override { return main_loop; }
	String get_config_path() const override;
	String get_data_path() const override;
	String get_cache_path() const override;
	Error open_dynamic_library(const String &p_path, void *&p_library_handle, GDExtensionData *p_data = nullptr) override;
	void run();
	OS_AuroraOS();
};
