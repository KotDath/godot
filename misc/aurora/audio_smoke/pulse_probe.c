/**************************************************************************/
/*  pulse_probe.c                                                         */
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

#include <math.h>
#include <pulse/error.h>
#include <pulse/simple.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(int argc, char **argv) {
	if (argc != 2 && argc != 3) {
		return 64;
	}
	int hold = 0;
	if (argc == 3) {
		char *end;
		long value = strtol(argv[2], &end, 10);
		if (!*argv[2] || *end || value < 0 || value > 20) {
			return 64;
		}
		hold = (int)value;
	}
	alarm(10 + hold);
	if (!strcmp(argv[1], "unset")) {
		unsetenv("PULSE_PROP_media.role");
	} else if (setenv("PULSE_PROP_media.role", argv[1], 1)) {
		return 65;
	}
	const pa_sample_spec spec = { PA_SAMPLE_S16LE, 48000, 2 };
	int error = 0;
	pa_simple *stream = pa_simple_new(NULL, "GodotAuroraPolicyProbe", PA_STREAM_PLAYBACK,
			getenv("AURORA_AUDIO_PROBE_SINK"), "Godot Aurora policy probe", &spec, NULL, NULL, &error);
	if (!stream) {
		fprintf(stderr, "connect: %s\n", pa_strerror(error));
		return 2;
	}
	printf("AURORA_AUDIO_POLICY:pid=%ld:requested=%s\n", (long)getpid(), argv[1]);
	fflush(stdout);
	short silence[4800 * 2] = { 0 };
	if (getenv("AURORA_AUDIO_PROBE_TONE")) {
		const double frequency = !strcmp(argv[1], "notification") ? 880.0 : 440.0;
		for (int sample = 0; sample < 4800; ++sample) {
			silence[sample * 2] = silence[sample * 2 + 1] = (short)(4096.0 * sin(6.283185307179586 * frequency * sample / 48000.0));
		}
	}
	for (int frame = 0; frame < 50 + hold * 10; ++frame) {
		if (pa_simple_write(stream, silence, sizeof(silence), &error) < 0) {
			fprintf(stderr, "write: %s\n", pa_strerror(error));
			pa_simple_free(stream);
			return 3;
		}
	}
	pa_simple_free(stream);
	puts("AURORA_AUDIO_POLICY:DONE");
	return 0;
}
