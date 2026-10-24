/**************************************************************************/
/*  test_boot_splash_gpu.cpp                                              */
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

#include "servers/rendering/rendering_server_types.h"

#include "platform/auroraos/orientation.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <unistd.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Pixel = std::array<unsigned char, 4>;
constexpr int IMAGE_WIDTH = 127;
constexpr int IMAGE_HEIGHT = 73;

void require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

struct Context {
	EGLDisplay display = EGL_NO_DISPLAY;
	EGLSurface surface = EGL_NO_SURFACE;
	EGLContext context = EGL_NO_CONTEXT;

	void initialize() {
		display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
		require(display != EGL_NO_DISPLAY && eglInitialize(display, nullptr, nullptr), "EGL display unavailable");
		require(eglBindAPI(EGL_OPENGL_ES_API), "Cannot select GLES");
		const EGLint attributes[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
			EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE };
		EGLConfig config;
		EGLint count = 0;
		require(eglChooseConfig(display, attributes, &config, 1, &count) && count == 1, "No RGBA8 GLES3 pbuffer configuration");
		const EGLint size[] = { EGL_WIDTH, 640, EGL_HEIGHT, 640, EGL_NONE };
		surface = eglCreatePbufferSurface(display, config, size);
		const EGLint version[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
		context = eglCreateContext(display, config, EGL_NO_CONTEXT, version);
		require(surface != EGL_NO_SURFACE && context != EGL_NO_CONTEXT, "Cannot create GLES3 pbuffer/context");
		require(eglMakeCurrent(display, surface, surface, context), "Cannot bind GLES context");
		std::cout << "GL_RENDERER:" << glGetString(GL_RENDERER) << '\n';
		std::cout << "GL_VERSION:" << glGetString(GL_VERSION) << '\n';
	}

	~Context() {
		if (display != EGL_NO_DISPLAY) {
			eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
			if (context != EGL_NO_CONTEXT) {
				eglDestroyContext(display, context);
			}
			if (surface != EGL_NO_SURFACE) {
				eglDestroySurface(display, surface);
			}
			eglTerminate(display);
		}
	}
};

GLuint shader(GLenum type, const std::string &body, bool rotated) {
	const std::string source = std::string("#version 300 es\nprecision highp float;\n#define MODE_SIMPLE_COPY\n#define USE_COPY_SECTION\n") +
			(rotated ? "#define ROTATE_COPY\n" : "") + body;
	const char *text = source.c_str();
	GLuint result = glCreateShader(type);
	glShaderSource(result, 1, &text, nullptr);
	glCompileShader(result);
	GLint ok = 0;
	glGetShaderiv(result, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char message[16384] = {};
		glGetShaderInfoLog(result, sizeof(message), nullptr, message);
		throw std::runtime_error(std::string("Production GLSL compilation failed: ") + message);
	}
	return result;
}

GLuint program(const std::string &vertex, const std::string &fragment, bool rotated) {
	GLuint vs = shader(GL_VERTEX_SHADER, vertex, rotated);
	GLuint fs = shader(GL_FRAGMENT_SHADER, fragment, rotated);
	GLuint result = glCreateProgram();
	glAttachShader(result, vs);
	glAttachShader(result, fs);
	glLinkProgram(result);
	glDeleteShader(vs);
	glDeleteShader(fs);
	GLint ok = 0;
	glGetProgramiv(result, GL_LINK_STATUS, &ok);
	require(ok, "Production GLSL link failed");
	return result;
}

struct Renderer {
	GLuint plain = 0;
	GLuint rotated = 0;
	GLuint texture = 0;
	GLuint vertices = 0;
	GLuint array = 0;

	void initialize(const std::filesystem::path &path) {
		std::ifstream stream(path);
		require(bool(stream), "Cannot read production copy.glsl");
		const std::string source((std::istreambuf_iterator<char>(stream)), {});
		const std::string vertex_marker = "#[vertex]\n";
		const std::string fragment_marker = "#[fragment]\n";
		size_t start = source.find(vertex_marker);
		size_t end = source.find(fragment_marker);
		require(start != std::string::npos && end != std::string::npos && end > start, "Invalid production shader sections");
		start += vertex_marker.size();
		const std::string vertex = source.substr(start, end - start);
		const std::string fragment = source.substr(end + fragment_marker.size());
		plain = program(vertex, fragment, false);
		rotated = program(vertex, fragment, true);

		std::vector<Pixel> pixels(IMAGE_WIDTH * IMAGE_HEIGHT);
		const Pixel colors[] = { { 255, 0, 0, 255 }, { 0, 255, 0, 255 }, { 0, 0, 255, 128 }, { 255, 255, 0, 255 } };
		for (int y = 0; y < IMAGE_HEIGHT; ++y) {
			for (int x = 0; x < IMAGE_WIDTH; ++x) {
				pixels[y * IMAGE_WIDTH + x] = colors[(y >= IMAGE_HEIGHT / 2 ? 2 : 0) + (x >= IMAGE_WIDTH / 2 ? 1 : 0)];
			}
		}
		pixels[0] = { 255, 255, 255, 255 }; // An asymmetric origin marker.
		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, IMAGE_WIDTH, IMAGE_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		const GLfloat quad[] = { -1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1 };
		glGenVertexArrays(1, &array);
		glBindVertexArray(array);
		glGenBuffers(1, &vertices);
		glBindBuffer(GL_ARRAY_BUFFER, vertices);
		glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), nullptr);
		glEnableVertexAttribArray(0);
	}

	std::vector<Pixel> draw(const AuroraOrientation::Presentation &layout, RSE::SplashStretchMode mode, bool filter, const std::string &control) {
		Size2i size = layout.get_logical_size();
		Rect2 rect = RenderingServerTypes::get_splash_stretched_screen_rect(Size2(IMAGE_WIDTH, IMAGE_HEIGHT), size, mode);
		rect.position.y = size.y - rect.position.y;
		rect.size.y = -rect.size.y;
		rect.position /= size;
		rect.size /= size;
		const Size2i viewport = control == "wrong-viewport" ? size : layout.native_size;
		glViewport(0, 0, viewport.x, viewport.y);
		glClearColor(13.0f / 255, 17.0f / 255, 23.0f / 255, 1);
		glClear(GL_COLOR_BUFFER_BIT);
		glEnable(GL_BLEND);
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE);
		int turn = control == "wrong-rotation" ? 0 : layout.transform;
		GLuint selected = turn ? rotated : plain;
		glUseProgram(selected);
		glUniform4f(glGetUniformLocation(selected, "copy_section"), rect.position.x, rect.position.y, rect.size.x, rect.size.y);
		const GLfloat rotations[4][2] = { { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 } };
		glUniform2f(glGetUniformLocation(selected, "copy_rotation"), rotations[turn][0], rotations[turn][1]);
		glUniform1i(glGetUniformLocation(selected, "source"), 0);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter ? GL_LINEAR : GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter ? GL_LINEAR : GL_NEAREST);
		glBindVertexArray(array);
		glDrawArrays(GL_TRIANGLES, 0, 6);
		std::vector<Pixel> pixels(layout.native_size.x * layout.native_size.y);
		glReadPixels(0, 0, layout.native_size.x, layout.native_size.y, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
		require(glGetError() == GL_NO_ERROR, "GLES draw/readback failed");
		return pixels;
	}

	~Renderer() {
		glDeleteProgram(plain);
		glDeleteProgram(rotated);
		glDeleteTextures(1, &texture);
		glDeleteBuffers(1, &vertices);
		glDeleteVertexArrays(1, &array);
	}
};

void save_ppm(const std::filesystem::path &path, const std::vector<Pixel> &pixels, Size2i size) {
	std::ofstream file(path, std::ios::binary);
	file << "P6\n"
		 << size.x << ' ' << size.y << "\n255\n";
	for (int y = size.y - 1; y >= 0; --y) {
		for (int x = 0; x < size.x; ++x) {
			file.write(reinterpret_cast<const char *>(pixels[y * size.x + x].data()), 3);
		}
	}
	require(bool(file), "Cannot save readback image");
}
} // namespace

int main(int argc, char **argv) {
	try {
		require(argc == 3 || argc == 4, "Usage: test_boot_splash_gpu COPY_GLSL OUTPUT_DIRECTORY [wrong-rotation|wrong-viewport]");
		const std::string control = argc == 4 ? argv[3] : "";
		require(control.empty() || control == "wrong-rotation" || control == "wrong-viewport", "Unknown negative control");
		alarm(30);
		Context context;
		context.initialize();
		Renderer renderer;
		renderer.initialize(argv[1]);
		std::filesystem::create_directories(argv[2]);
		size_t cases = 0;
		size_t checked = 0;
		size_t boundary_differences = 0;
		for (Size2i native : { Size2i(384, 640), Size2i(640, 384), Size2i(321, 479) }) {
			for (int transform = 0; transform < 4; ++transform) {
				AuroraOrientation::set_presentation(native, transform);
				const auto layout = AuroraOrientation::get_presentation(native);
				const Size2i logical = transform % 2 ? Size2i(native.y, native.x) : native;
				require(layout.get_logical_size() == logical, "Logical/native size mismatch");
				for (int mode = 0; mode < 6; ++mode) {
					for (bool filter : { false, true }) {
						auto reference = renderer.draw({ logical, 0 }, RSE::SplashStretchMode(mode), filter, "");
						auto actual = renderer.draw(layout, RSE::SplashStretchMode(mode), filter, control);
						size_t stable = 0;
						for (int y = 0; y < native.y; ++y) {
							for (int x = 0; x < native.x; ++x) {
								int rx = x;
								int ry = y;
								if (transform == 1) {
									rx = y;
									ry = native.x - 1 - x;
								}
								if (transform == 2) {
									rx = native.x - 1 - x;
									ry = native.y - 1 - y;
								}
								if (transform == 3) {
									rx = native.y - 1 - y;
									ry = x;
								}
								const Pixel expected = reference[ry * logical.x + rx];
								bool interior = rx > 0 && ry > 0 && rx < logical.x - 1 && ry < logical.y - 1;
								for (int dy = -1; interior && dy <= 1; ++dy) {
									for (int dx = -1; dx <= 1; ++dx) {
										interior = interior && reference[(ry + dy) * logical.x + rx + dx] == expected;
									}
								}
								if (interior) {
									++stable;
									require(actual[y * native.x + x] == expected, "Rotated GPU pixels differ: transform=" + std::to_string(transform) + " mode=" + std::to_string(mode) + " filter=" + std::to_string(filter));
								} else if (actual[y * native.x + x] != expected) {
									++boundary_differences;
								}
							}
						}
						require(stable > size_t(native.x * native.y) / 2, "Insufficient interior coverage");
						checked += stable;
						++cases;
						if (native == Size2i(384, 640) && mode == 1 && filter) {
							save_ppm(std::filesystem::path(argv[2]) / ("transform-" + std::to_string(transform) + ".ppm"), actual, native);
						}
					}
				}
			}
		}
		// Before a legacy-shell snapshot exists, preserve the ordinary boot path.
		for (Size2i invalid : { Size2i(), Size2i(0, 640), Size2i(384, 0) }) {
			AuroraOrientation::set_presentation(invalid, 3);
			const auto layout = AuroraOrientation::get_presentation(Size2i(384, 640));
			require(layout.native_size == Size2i(384, 640) && layout.transform == 0, "Invalid snapshot did not use default presentation");
			require(renderer.draw(layout, RSE::SPLASH_STRETCH_MODE_KEEP, true, "") == renderer.draw({ Size2i(384, 640), 0 }, RSE::SPLASH_STRETCH_MODE_KEEP, true, ""), "Fallback GPU pixels differ");
			++cases;
		}
		AuroraOrientation::set_presentation({}, 0);
		std::cout << "AURORA_BOOT_GPU_PASS:cases=" << cases << ":interior_pixels=" << checked << ":boundary_differences=" << boundary_differences << '\n';
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "AURORA_BOOT_GPU_FAIL:" << error.what() << '\n';
		return 1;
	}
}
