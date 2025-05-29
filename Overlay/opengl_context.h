#pragma once
#ifdef _WIN32
    #include <Windows.h>
#else
    #include <EGL/egl.h>
#endif

class OpenglContext {
    #ifdef _WIN32
    HWND m_hWnd = nullptr;
    HDC m_hDC = nullptr;
    HGLRC m_hRC = nullptr;
    #else
    EGLDisplay m_eglDisplay = EGL_NO_DISPLAY;
    EGLContext m_eglContext = EGL_NO_CONTEXT;
    #endif
public:
	inline OpenglContext() {}
	~OpenglContext();
	bool Initialize();
	bool MakeCurrent();
};
