#include "opengl_context.h"
#include <iostream>

#ifdef _WIN32

~OpenglContext::OpenglContext() {
    if (m_hRC) {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(m_hRC);
        m_hRC = NULL;
    }
    
    if (m_hDC && m_hWnd) {
        ReleaseDC(m_hWnd, m_hDC);
        m_hDC = NULL;
    }
    
    if (m_hWnd) {
        DestroyWindow(m_hWnd);
        m_hWnd = NULL;
    }
}

bool OpenglContext::Initialize() {
    // Create a dummy window for OpenGL context
    WNDCLASS wc = {0};
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "DashboardGLClass";
    
    if (!RegisterClass(&wc)) {
        std::cout << "Failed to register window class" << std::endl;
        return false;
    }
    
    m_hWnd = CreateWindow("DashboardGLClass", "Dashboard GL Window", 0, 0, 0, 1, 1, NULL, NULL, GetModuleHandle(NULL), NULL);
    if (!m_hWnd) {
        std::cout << "Failed to create dummy window" << std::endl;
        return false;
    }
    
    m_hDC = GetDC(m_hWnd);
    if (!m_hDC) {
        std::cout << "Failed to get device context" << std::endl;
        DestroyWindow(m_hWnd);
        m_hWnd = NULL;
        return false;
    }
    
    PIXELFORMATDESCRIPTOR pfd = {0};
    pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    
    int pixelFormat = ChoosePixelFormat(m_hDC, &pfd);
    if (!pixelFormat) {
        std::cout << "Failed to choose pixel format" << std::endl;
        ReleaseDC(m_hWnd, m_hDC);
        DestroyWindow(m_hWnd);
        m_hDC = NULL;
        m_hWnd = NULL;
        return false;
    }
    
    if (!SetPixelFormat(m_hDC, pixelFormat, &pfd)) {
        std::cout << "Failed to set pixel format" << std::endl;
        ReleaseDC(m_hWnd, m_hDC);
        DestroyWindow(m_hWnd);
        m_hDC = NULL;
        m_hWnd = NULL;
        return false;
    }
    
    m_hRC = wglCreateContext(m_hDC);
    if (!m_hRC) {
        std::cout << "Failed to create OpenGL rendering context" << std::endl;
        ReleaseDC(m_hWnd, m_hDC);
        DestroyWindow(m_hWnd);
        m_hDC = NULL;
        m_hWnd = NULL;
        return false;
    }
    
    if (!wglMakeCurrent(m_hDC, m_hRC)) {
        std::cout << "Failed to make OpenGL context current" << std::endl;
        wglDeleteContext(m_hRC);
        ReleaseDC(m_hWnd, m_hDC);
        DestroyWindow(m_hWnd);
        m_hRC = NULL;
        m_hDC = NULL;
        m_hWnd = NULL;
        return false;
    }

    return true;
}

bool OpenglContext::MakeCurrent() {
    return wglMakeCurrent(m_hDC, m_hRC);
}

#else // _WIN32

OpenglContext::~OpenglContext() {
	if (m_eglContext != EGL_NO_CONTEXT) {
		eglMakeCurrent(EGL_NO_DISPLAY, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		eglDestroyContext(m_eglDisplay, m_eglContext);
		m_eglContext = EGL_NO_CONTEXT;
	}

	m_eglDisplay = EGL_NO_DISPLAY;
}

bool OpenglContext::Initialize() {
    m_eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (m_eglDisplay == EGL_NO_DISPLAY) {
        std::cout << "Failed to get EGL display" << std::endl;
        return false;
    }

	EGLint eglMajor = 0, eglMinor = 0;
    if (!eglInitialize(m_eglDisplay, &eglMajor, &eglMinor) || eglMajor < 1 || (eglMajor == 1 && eglMinor < 5)) {
        std::cout << "Failed to initialize EGL" << std::endl;
        m_eglDisplay = EGL_NO_DISPLAY;
        return false;
    }

    if (!eglBindAPI(EGL_OPENGL_API)) {
        std::cout << "Failed to bind OpenGL API" << std::endl;
        m_eglDisplay = EGL_NO_DISPLAY;
        return false;
    }

    static const EGLint configParams[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE,
    };
    EGLint configCount = 0;
    EGLConfig config = nullptr;
    if (!eglChooseConfig(m_eglDisplay, configParams, &config, 1, &configCount) || configCount == 0) {
        std::cout << "Failed to resolve EGL config" << std::endl;
        m_eglDisplay = EGL_NO_DISPLAY;
        return false;
    }

    const EGLint contextParams[] = {
    	EGL_CONTEXT_MAJOR_VERSION, 2, EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE,
    };
    m_eglContext = eglCreateContext(m_eglDisplay, config, EGL_NO_CONTEXT, contextParams);
    if (m_eglContext == EGL_NO_CONTEXT) {
        std::cout << "Failed to create OpenGL rendering context" << std::endl;
        m_eglDisplay = EGL_NO_DISPLAY;
        return false;
    }

    if (eglMakeCurrent(m_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, m_eglContext) == EGL_FALSE) {
        std::cout << "Failed to make OpenGL context current" << std::endl;
        eglDestroyContext(m_eglDisplay, m_eglContext);
        m_eglContext = EGL_NO_CONTEXT;
        m_eglDisplay = EGL_NO_DISPLAY;
        return false;
    }
    
    return true;
}

bool OpenglContext::MakeCurrent() {
    return eglMakeCurrent(m_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, m_eglContext);
}

#endif // !_WIN32
