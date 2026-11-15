#if defined(__linux__)

/**
 * Offscreen EGL context (surfaceless + pbuffer) for the Linux build.
 *
 * The plugin used to create a hidden GLFW/X11 window.  That does not work in
 * a pure Wayland session (no DISPLAY) nor inside the OBS Flatpak sandbox, so
 * on Linux we render into an offscreen pbuffer surface instead.  GLEW must be
 * compiled with GLEW_EGL so that it resolves the GL entry points through
 * eglGetProcAddress() rather than GLX.
 */

#include "EglContext.hpp"

#include <obs-module.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <cstddef>

namespace {

EGLDisplay s_display = EGL_NO_DISPLAY;
EGLContext s_context = EGL_NO_CONTEXT;
EGLSurface s_surface = EGL_NO_SURFACE;
int s_width = 0;
int s_height = 0;

} // namespace

namespace EglContext {

bool Create(int width, int height)
{
	if (width <= 0)
		width = 1;
	if (height <= 0)
		height = 1;

	// A previous context should not be leaked.
	Destroy();

	// ---- Display ------------------------------------------------------
	// Prefer the surfaceless platform: it needs no X11/Wayland connection.
	PFNEGLGETPLATFORMDISPLAYEXTPROC getPlatformDisplay =
		reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
			eglGetProcAddress("eglGetPlatformDisplayEXT"));
	if (getPlatformDisplay != nullptr) {
		s_display = getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
					       EGL_DEFAULT_DISPLAY, nullptr);
	}

	if (s_display == EGL_NO_DISPLAY) {
		// Fallback: let the EGL implementation pick its default display.
		s_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	}

	if (s_display == EGL_NO_DISPLAY) {
		blog(LOG_ERROR,
		     "[bongobs-cat] EGL: nenhum display disponível "
		     "(eglGetError=0x%x).",
		     (unsigned int)eglGetError());
		return false;
	}

	// ---- Initialize ---------------------------------------------------
	EGLint major = 0;
	EGLint minor = 0;
	if (eglInitialize(s_display, &major, &minor) != EGL_TRUE) {
		blog(LOG_ERROR,
		     "[bongobs-cat] EGL: eglInitialize falhou "
		     "(eglGetError=0x%x).",
		     (unsigned int)eglGetError());
		Destroy();
		return false;
	}

	if (eglBindAPI(EGL_OPENGL_API) != EGL_TRUE) {
		blog(LOG_ERROR,
		     "[bongobs-cat] EGL: eglBindAPI(EGL_OPENGL_API) falhou "
		     "(eglGetError=0x%x).",
		     (unsigned int)eglGetError());
		Destroy();
		return false;
	}

	// ---- Choose a pbuffer-capable OpenGL config -----------------------
	const EGLint configAttribs[] = {
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_DEPTH_SIZE, 16,
		EGL_NONE
	};

	EGLConfig config = nullptr;
	EGLint numConfigs = 0;
	if (eglChooseConfig(s_display, configAttribs, &config, 1,
			    &numConfigs) != EGL_TRUE ||
	    numConfigs < 1) {
		blog(LOG_ERROR,
		     "[bongobs-cat] EGL: eglChooseConfig falhou "
		     "(eglGetError=0x%x).",
		     (unsigned int)eglGetError());
		Destroy();
		return false;
	}

	// ---- Create the OpenGL context ------------------------------------
	// Cubism renders with GLSL 1.20, which needs a compatibility profile.
	const EGLint context33Attribs[] = {
		EGL_CONTEXT_MAJOR_VERSION, 3,
		EGL_CONTEXT_MINOR_VERSION, 3,
		EGL_CONTEXT_OPENGL_PROFILE_MASK,
			EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT,
		EGL_NONE
	};
	const EGLint context21Attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};
	const EGLint contextNoAttribs[] = {
		EGL_NONE
	};

	s_context = eglCreateContext(s_display, config, EGL_NO_CONTEXT,
				     context33Attribs);
	if (s_context == EGL_NO_CONTEXT) {
		s_context = eglCreateContext(s_display, config, EGL_NO_CONTEXT,
					     context21Attribs);
	}
	if (s_context == EGL_NO_CONTEXT) {
		s_context = eglCreateContext(s_display, config, EGL_NO_CONTEXT,
					     contextNoAttribs);
	}
	if (s_context == EGL_NO_CONTEXT) {
		blog(LOG_ERROR,
		     "[bongobs-cat] EGL: eglCreateContext falhou "
		     "(eglGetError=0x%x).",
		     (unsigned int)eglGetError());
		Destroy();
		return false;
	}

	// ---- Create the offscreen pbuffer --------------------------------
	const EGLint surfaceAttribs[] = {
		EGL_WIDTH, width,
		EGL_HEIGHT, height,
		EGL_NONE
	};
	s_surface = eglCreatePbufferSurface(s_display, config, surfaceAttribs);
	if (s_surface == EGL_NO_SURFACE) {
		blog(LOG_ERROR,
		     "[bongobs-cat] EGL: eglCreatePbufferSurface falhou "
		     "(eglGetError=0x%x).",
		     (unsigned int)eglGetError());
		Destroy();
		return false;
	}

	if (eglMakeCurrent(s_display, s_surface, s_surface, s_context) !=
	    EGL_TRUE) {
		blog(LOG_ERROR,
		     "[bongobs-cat] EGL: eglMakeCurrent falhou "
		     "(eglGetError=0x%x).",
		     (unsigned int)eglGetError());
		Destroy();
		return false;
	}

	s_width = width;
	s_height = height;

	blog(LOG_INFO,
	     "[bongobs-cat] EGL: contexto offscreen criado (%dx%d, EGL %d.%d).",
	     s_width, s_height, major, minor);
	return true;
}

void Destroy()
{
	// Safe to call even if Create() failed or was never called.
	if (s_display != EGL_NO_DISPLAY) {
		eglMakeCurrent(s_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
			       EGL_NO_CONTEXT);
	}
	if (s_display != EGL_NO_DISPLAY && s_surface != EGL_NO_SURFACE) {
		eglDestroySurface(s_display, s_surface);
	}
	if (s_display != EGL_NO_DISPLAY && s_context != EGL_NO_CONTEXT) {
		eglDestroyContext(s_display, s_context);
	}
	if (s_display != EGL_NO_DISPLAY) {
		eglTerminate(s_display);
	}

	s_surface = EGL_NO_SURFACE;
	s_context = EGL_NO_CONTEXT;
	s_display = EGL_NO_DISPLAY;
	s_width = 0;
	s_height = 0;
}

int Width()
{
	return s_width;
}

int Height()
{
	return s_height;
}

} // namespace EglContext

#endif // __linux__
