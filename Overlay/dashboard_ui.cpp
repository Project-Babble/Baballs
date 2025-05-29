// dashboard_ui.cpp

#include "dashboard_ui.h"

#include "stb_truetype.h"

#include <GL/gl.h>
#include <GL/glu.h>
#ifdef _WIN32
#include <Windows.h>
#else
#include <linux/limits.h>
#endif
#include <cstring>
#include <iostream>

// Dashboard texture size
#define DASHBOARD_WIDTH 1024
#define DASHBOARD_HEIGHT 512

// Colors
#define COLOR_FRAME_BACKGROUND 0x2D2D2D
#define COLOR_BUTTON 0x4D4D4D
#define COLOR_BUTTON_HOVER 0x6D6D6D
#define COLOR_TEXT 0xFFFFFF
#define COLOR_STATUS_TEXT 0xAAAAAA

DashboardUI::DashboardUI() :
    m_dashboardHandle(vr::k_ulOverlayHandleInvalid),
    m_glTextureId(0),
    m_textureWidth(DASHBOARD_WIDTH),
    m_textureHeight(DASHBOARD_HEIGHT),
    m_textureData(nullptr),
    m_statusDisplay("Ready", 20, DASHBOARD_HEIGHT - 40),
    m_glContext(),
    m_fontBuffer(nullptr),
    m_fontSize(32.0f)  // Default font size
{
    // Initialize font struct to zero
    memset(&m_font, 0, sizeof(m_font));
}

bool DashboardUI::InitializeFont(const char* fontPath, float fontSize) {
    // Load font file
    FILE* fontFile = fopen(fontPath, "rb");
    if (!fontFile) return false;
    
    fseek(fontFile, 0, SEEK_END);
    long size = ftell(fontFile);
    fseek(fontFile, 0, SEEK_SET);
    
    m_fontBuffer = new unsigned char[size];
    fread(m_fontBuffer, 1, size, fontFile);
    fclose(fontFile);
    
    // Initialize font
    if (stbtt_InitFont(&m_font, m_fontBuffer, 0) == 0) {
        delete[] m_fontBuffer;
        m_fontBuffer = nullptr;
        return false;
    }
    
    m_fontSize = fontSize;
    return true;
}

void DashboardUI::RenderText(const std::string& text, int x, int y, uint32_t color) {
    if (!m_fontBuffer) return;
    
    float scale = stbtt_ScaleForPixelHeight(&m_font, m_fontSize);
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&m_font, &ascent, &descent, &lineGap);
    
    int baseline = (int)(y + (ascent * scale));
    int cursorX = x;
    
    for (char c : text) {
        // Check if glyph is already cached
        auto it = m_glyphCache.find(c);
        CachedGlyph glyph;
        
        if (it == m_glyphCache.end()) {
            // Render the glyph
            int width, height, xoff, yoff;
            unsigned char* bitmap = stbtt_GetCodepointBitmap(
                &m_font, 0, scale, c, &width, &height, &xoff, &yoff);
            
            glyph = {width, height, xoff, yoff, bitmap};
            m_glyphCache[c] = glyph;
        } else {
            glyph = it->second;
        }
        
        // Get horizontal advance
        int advance, lsb;
        stbtt_GetCodepointHMetrics(&m_font, c, &advance, &lsb);
        
        // Draw the glyph
        for (int j = 0; j < glyph.height; ++j) {
            for (int i = 0; i < glyph.width; ++i) {
                int pixelX = cursorX + i + glyph.xoff;
                int pixelY = baseline + (glyph.height - 1 - j) + glyph.yoff;
                
                if (pixelX >= 0 && pixelX < m_textureWidth && 
                    pixelY >= 0 && pixelY < m_textureHeight) {
                    
                    unsigned char alpha = glyph.bitmap[j * glyph.width + i];
                    if (alpha > 0) {
                        int index = (pixelY * m_textureWidth + pixelX) * 4;
                        
                        // Apply alpha blending
                        float a = alpha / 255.0f;
                        m_textureData[index + 0] = (uint8_t) (((color >> 16) & 0xFF) * a + 
                                                 m_textureData[index + 0] * (1 - a));
                        m_textureData[index + 1] = (uint8_t) (((color >> 8) & 0xFF) * a + 
                                                 m_textureData[index + 1] * (1 - a));
                        m_textureData[index + 2] = (uint8_t) ((color & 0xFF) * a + 
                                                 m_textureData[index + 2] * (1 - a));
                        m_textureData[index + 3] = 255;
                    }
                }
            }
        }
        
        // Advance cursor
        cursorX += (int)(advance * scale);
        
        // Apply kerning if there's a next character
        if (text.length() > 1) {
            int kern = stbtt_GetCodepointKernAdvance(&m_font, c, text[1]);
            cursorX += (int)(kern * scale);
        }
    }
}

int DashboardUI::MeasureTextWidth(const std::string& text) {
    if (!m_fontBuffer) return (int) (text.length() * 8); // Fallback
    
    float scale = stbtt_ScaleForPixelHeight(&m_font, m_fontSize);
    int width = 0;
    
    for (size_t i = 0; i < text.length(); ++i) {
        int advance, lsb;
        stbtt_GetCodepointHMetrics(&m_font, text[i], &advance, &lsb);
        width += (int)(advance * scale);
        
        // Add kerning
        if (i < text.length() - 1) {
            width += (int)(stbtt_GetCodepointKernAdvance(&m_font, text[i], text[i+1]) * scale);
        }
    }
    
    return width;
}

DashboardUI::~DashboardUI() {
    // Clear glyph cache
    for (auto& pair : m_glyphCache) {
        free(pair.second.bitmap);
    }
    m_glyphCache.clear();
    
    // Free font buffer
    if (m_fontBuffer) {
        delete[] m_fontBuffer;
    }
    Shutdown();
}

bool DashboardUI::Initialize() {
    char iconPath[PATH_MAX] = {};

    // Initialize OpenGL first
    if (!m_glContext.Initialize()) {
        std::cout << "Failed to initialize OpenGL for dashboard" << std::endl;
        return false;
    }

    // Initialize font
    if (!InitializeFont("./font.ttf", 16.0f)) {
        printf("Failed to load font\n");
        // Continue anyway, we'll use fallback rendering
    }


    vr::VROverlayHandle_t neighborHandle = vr::k_ulOverlayHandleInvalid;
    
    // Create the dashboard overlay
    vr::VROverlayError overlayError = vr::VROverlay()->CreateDashboardOverlay(
        "peripheral_vision_dashboard", 
        "Eye Tracking Calibration", 
        &m_dashboardHandle, 
        &neighborHandle);  // Change this from &vr::k_ulOverlayHandleInvalid to nullptr

    #ifdef _WIN32
    // Get absolute path to the icon file
    if (GetFullPathNameA("./icon.png", PATH_MAX, iconPath, nullptr) == 0) {
        std::cout << "Failed to get full path for icon" << std::endl;
        // Continue anyway - we'll try with the relative path
        strncpy(iconPath, "./icon.png", sizeof(iconPath) - 1);
    }
    #else
    strncpy(iconPath, "./icon.png", sizeof(iconPath) - 1);
    #endif
    
    // Set thumbnail image - this is the method available in most OpenVR versions
    vr::VROverlayError iconError = vr::VROverlay()->SetOverlayFromFile(m_dashboardHandle, iconPath);
    if (iconError != vr::VROverlayError_None) {
        std::cout << "Failed to set overlay thumbnail: " 
                  << vr::VROverlay()->GetOverlayErrorNameFromEnum(iconError) << std::endl;
    }

    if (overlayError != vr::VROverlayError_None) {
        std::cout << "Failed to create dashboard overlay: " 
                  << vr::VROverlay()->GetOverlayErrorNameFromEnum(overlayError) << std::endl;
        return false;
    }
    
    // Set the dashboard width
    vr::VROverlay()->SetOverlayWidthInMeters(m_dashboardHandle, 2.0f);
    
    // Enable mouse input
    vr::VROverlay()->SetOverlayInputMethod(m_dashboardHandle, vr::VROverlayInputMethod_Mouse);
    
    // Create the texture for the dashboard
    if (!CreateDashboardTexture()) {
        Shutdown();
        return false;
    }
    
    // Add default buttons
    AddButton("Start", 20, 20, 200, 60, []() { 
        std::cout << "Start button pressed" << std::endl; 
    });
    
    AddButton("Reset", 20, 100, 200, 60, []() { 
        std::cout << "Reset button pressed" << std::endl; 
    });
    
    AddButton("Stop", 20, 180, 200, 60, []() { 
        std::cout << "Stop button pressed" << std::endl; 
    });
    
    return true;
}

void DashboardUI::Shutdown() {
    // Destroy the overlay
    if (m_dashboardHandle != vr::k_ulOverlayHandleInvalid) {
        vr::VROverlay()->DestroyOverlay(m_dashboardHandle);
        m_dashboardHandle = vr::k_ulOverlayHandleInvalid;
    }
    
    // Delete OpenGL texture
    if (m_glTextureId != 0) {
        glDeleteTextures(1, &m_glTextureId);
        m_glTextureId = 0;
    }
    
    // Free texture data memory
    if (m_textureData != nullptr) {
        delete[] m_textureData;
        m_textureData = nullptr;
    }
}

void DashboardUI::Update() {
    // Process any dashboard events
    ProcessDashboardEvents();
    
    // Render the UI and update the texture
    RenderUI();
    UpdateOverlayTexture();
}

void DashboardUI::AddButton(const std::string& label, float x, float y, float width, float height, std::function<void()> callback) {
    m_buttons.emplace_back(label, x, y, width, height, callback);
}

void DashboardUI::SetStatusText(const std::string& text) {
    m_statusDisplay.text = text;
}

bool DashboardUI::CreateDashboardTexture() {
    // Make sure OpenGL context is current
    if (!m_glContext.MakeCurrent())
        return false;
    
    // Allocate memory for the texture data
    m_textureData = new uint8_t[m_textureWidth * m_textureHeight * 4];
    if (!m_textureData) {
        std::cout << "Failed to allocate texture data memory" << std::endl;
        return false;
    }
    
    // Clear the texture to dark gray
    for (int i = 0; i < m_textureWidth * m_textureHeight; i++) {
        int index = i * 4;
        m_textureData[index + 0] = (COLOR_FRAME_BACKGROUND >> 16) & 0xFF; // R
        m_textureData[index + 1] = (COLOR_FRAME_BACKGROUND >> 8) & 0xFF;  // G
        m_textureData[index + 2] = COLOR_FRAME_BACKGROUND & 0xFF;         // B
        m_textureData[index + 3] = 255;                             // A
    }
    
    // Generate OpenGL texture
    glGenTextures(1, &m_glTextureId);
    if (m_glTextureId == 0) {
        std::cout << "Failed to generate OpenGL texture" << std::endl;
        delete[] m_textureData;
        m_textureData = nullptr;
        return false;
    }
    
    // Bind and set up the OpenGL texture
    glBindTexture(GL_TEXTURE_2D, m_glTextureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    
    // Upload the texture data
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_textureWidth, m_textureHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, m_textureData);
    
    return true;
}

void DashboardUI::UpdateOverlayTexture() {
    // Make sure OpenGL context is current
    if (!m_glContext.MakeCurrent())
        return;
    
    // Only update if the texture exists
    if (m_glTextureId != 0) {
        // Update the texture with the current UI state
        glBindTexture(GL_TEXTURE_2D, m_glTextureId);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_textureWidth, m_textureHeight, GL_RGBA, GL_UNSIGNED_BYTE, m_textureData);
        
        // Set up the texture for the overlay
        vr::Texture_t texture = {};
        texture.handle = (void*)(uintptr_t)m_glTextureId;
        texture.eType = vr::TextureType_OpenGL;
        texture.eColorSpace = vr::ColorSpace_Auto;
        
        // Set the overlay texture
        vr::VROverlay()->SetOverlayTexture(m_dashboardHandle, &texture);
    }
}

void DashboardUI::RenderUI() {
    // Clear the texture to background color
    for (int i = 0; i < m_textureWidth * m_textureHeight; i++) {
        int index = i * 4;
        m_textureData[index + 0] = (COLOR_FRAME_BACKGROUND >> 16) & 0xFF; // R
        m_textureData[index + 1] = (COLOR_FRAME_BACKGROUND >> 8) & 0xFF;  // G
        m_textureData[index + 2] = COLOR_FRAME_BACKGROUND & 0xFF;         // B
        m_textureData[index + 3] = 255;                             // A
    }
    
    // Draw each button
    for (const auto& button : m_buttons) {
        // Draw button background
        uint32_t buttonColor = button.isHovered ? COLOR_BUTTON_HOVER : COLOR_BUTTON;
        
        // Draw rectangle
        for (int y = (int)button.y; y < button.y + button.height; y++) {
            for (int x = (int)button.x; x < button.x + button.width; x++) {
                if (x >= 0 && x < m_textureWidth && y >= 0 && y < m_textureHeight) {
                    int index = (y * m_textureWidth + x) * 4;
                    m_textureData[index + 0] = (buttonColor >> 16) & 0xFF; // R
                    m_textureData[index + 1] = (buttonColor >> 8) & 0xFF;  // G
                    m_textureData[index + 2] = buttonColor & 0xFF;         // B
                    m_textureData[index + 3] = 255;                        // A
                }
            }
        }
        
        // Draw button text (simple centered text)
        int textX = (int) (button.x + (button.width - button.label.length() * 8) / 2);
        int textY = (int) (button.y + (button.height - 16) / 2);
        
        // Very simple text rendering
        RenderText(
            button.label,
            (int)(button.x + (button.width - MeasureTextWidth(button.label)) / 2),
            (int)(button.y + (button.height - m_fontSize) / 2),
            COLOR_TEXT
        );
    }
    
    // Draw status text
    int statusTextX = (int)m_statusDisplay.x;
    int statusTextY = (int)m_statusDisplay.y;
    
    // Simple text rendering for status
    RenderText(
        m_statusDisplay.text,
        statusTextX,
        statusTextY,
        COLOR_STATUS_TEXT
    );
}

void DashboardUI::ProcessDashboardEvents() {
    // Process overlay events
    vr::VREvent_t event;
    while (vr::VROverlay()->PollNextOverlayEvent(m_dashboardHandle, &event, sizeof(event))) {
        printf("DashboardEvent: %s (%d)\n", 
               vr::VRSystem()->GetEventTypeNameFromEnum((vr::EVREventType)event.eventType), 
               event.eventType);
        switch (event.eventType) {
            case vr::VREvent_MouseMove: {
                // Mouse move event
                float mouseX = event.data.mouse.x;
                float mouseY = event.data.mouse.y;
                HandleMouseInput(mouseX, mouseY, false);
                break;
            }
            
            case vr::VREvent_MouseButtonDown: {
                // Mouse button down event
                float mouseX = event.data.mouse.x;
                float mouseY = event.data.mouse.y;
                HandleMouseInput(mouseX, mouseY, true);
                break;
            }
            
            case vr::VREvent_MouseButtonUp: {
                // Mouse button up event - check for button clicks
                for (auto& button : m_buttons) {
                    if (button.isHovered && button.wasPressed) {
                        // Button was clicked, execute its callback
                        if (button.callback) {
                            button.callback();
                        }
                        button.wasPressed = false;
                    }
                }
                break;
            }
        }
    }
}

void DashboardUI::HandleMouseInput(float x, float y, bool mouseDown) {
    // Convert normalized coordinates to texture coordinates
    float textureX = x * m_textureWidth;
    float textureY = y * m_textureHeight;
    
    // Check button hover states
    for (auto& button : m_buttons) {
        bool wasHovered = button.isHovered;
        button.isHovered = HitTestButton(button, textureX, textureY);
        
        // Set button press state if mouse is down and hovering
        if (mouseDown && button.isHovered) {
            button.wasPressed = true;
        }
        
        // If hover state changed, we need to update the UI
        if (wasHovered != button.isHovered) {
            RenderUI();
            UpdateOverlayTexture();
        }
    }
}

bool DashboardUI::HitTestButton(const DashboardButton& button, float x, float y) {
    return (x >= button.x && x <= button.x + button.width &&
            y >= button.y && y <= button.y + button.height);
}