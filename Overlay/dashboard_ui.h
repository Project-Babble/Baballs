#pragma once
#include "opengl_context.h"
#include "stb_truetype.h"
#include <openvr/openvr.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <GL/gl.h>

// Simple button class for dashboard UI
struct DashboardButton {
    std::string label;
    float x, y, width, height;
    bool isHovered;
    bool wasPressed;
    std::function<void()> callback;
    
    DashboardButton(const std::string& lbl, float posX, float posY, float w, float h, std::function<void()> cb)
        : label(lbl), x(posX), y(posY), width(w), height(h), isHovered(false), wasPressed(false), callback(cb) {}
};

// Text display for status information
struct TextDisplay {
    std::string text;
    float x, y;
    
    TextDisplay(const std::string& txt, float posX, float posY)
        : text(txt), x(posX), y(posY) {}
};

class DashboardUI {
public:
    DashboardUI();
    ~DashboardUI();
    
    bool Initialize();
    void Shutdown();
    void Update();
    
    // Add UI elements
    void AddButton(const std::string& label, float x, float y, float width, float height, std::function<void()> callback);
    void SetStatusText(const std::string& text);
    
private:
    vr::VROverlayHandle_t m_dashboardHandle;
    GLuint m_glTextureId;
    int m_textureWidth;
    int m_textureHeight;
    uint8_t* m_textureData;
    
    std::vector<DashboardButton> m_buttons;
    TextDisplay m_statusDisplay;
    
    OpenglContext m_glContext;
    
    // Helper methods
    bool CreateDashboardTexture();
    void UpdateOverlayTexture();
    void RenderUI();
    void ProcessDashboardEvents();
    
    // Input handling
    void HandleMouseInput(float x, float y, bool mouseDown);
    bool HitTestButton(const DashboardButton& button, float x, float y);

    // Font rendering methods - now as class methods
    bool InitializeFont(const char* fontPath, float fontSize);
    void RenderText(const std::string& text, int x, int y, uint32_t color);
    int MeasureTextWidth(const std::string& text);

    // Font data
    unsigned char* m_fontBuffer;
    stbtt_fontinfo m_font;
    float m_fontSize;
    
    // Glyph cache
    struct CachedGlyph {
        int width, height;
        int xoff, yoff;
        unsigned char* bitmap;
    };
    std::unordered_map<char, CachedGlyph> m_glyphCache;
};