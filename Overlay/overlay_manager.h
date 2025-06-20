#pragma once

#include <openvr/openvr.h>
#ifdef _WIN32
    #include <windows.h>
#endif
#include <GL/gl.h>
#include <string>
#include <map>
#include "math_utils.h"
#include "config.h"
#include "routine.h"
#include "stb_truetype.h"
#include "trainer_wrapper.h"
// Forward declaration for stb_truetype
typedef struct stbtt_fontinfo stbtt_fontinfo;

class OverlayManager {

public:
    struct ViewingAngles {
        float yaw;   // Horizontal angle (left/right)
        float pitch; // Vertical angle (up/down)
        float roll;  // Tilt angle (rotation around forward axis)
        float total; // Total angular difference
    };
    
    // Struct for cached glyphs
    struct CachedGlyph {
        int width, height;
        int xoff, yoff;
        unsigned char* bitmap;
    };
    
    OverlayManager();
    ~OverlayManager();

    // Initialize the overlay system
    bool Initialize();
    
    // Shutdown and cleanup
    void Shutdown();
    
    // Update overlay position based on HMD position
    void Update();
    
    // Add these methods instead
    void SetFixedTargetPosition(float yawAngle, float pitchAngle);
    void SetPreviewTargetPosition(float yawAngle, float pitchAngle);
    
    // Get the current angles
    float GetCurrentYawAngle() const;
    float GetCurrentPitchAngle() const;
    
    // Reset target to center position
    void ResetTargetPosition();
    
    // Check if the overlay is visible
    bool IsOverlayVisible() const;
    
    // Toggle overlay visibility
    void ToggleOverlayVisibility();
    
    // Target crosshair visibility control
    void ShowTargetCrosshair();
    void HideTargetCrosshair();
    void SetTargetCrosshairVisibility(bool visible);
    
    // update the tracking target animation
    void UpdateAnimation();

    // start a calibration routine
    void StartRoutine(uint32_t routine);

    // Text display methods
    void SetDisplayString(const char* text);
    void SetDisplayStringWithGraph(const char* text, const std::vector<float>& lossHistory);
    
    // Graph rendering methods (public for external access)
    void RenderLossGraph(const std::vector<float>& lossHistory, int x, int y, int width, int height);
    void DrawLine(int x1, int y1, int x2, int y2, uint32_t color);
    void DrawPixel(int x, int y, uint32_t color);
    
    // Get overlay handle
    vr::VROverlayHandle_t GetOverlayHandle() const;
    ViewingAngles CalculateCurrentViewingAngle() const;
    
    // Public access for external graph rendering
    HDC m_hDC;
    HGLRC m_hRC;
    GLuint m_glTextTextureId;
    uint8_t* m_textTextureData;
    int m_textTextureWidth;
    int m_textTextureHeight;
    vr::VROverlayHandle_t m_ulTextOverlayHandle;

    static float s_routinePitch;
    static float s_routineYaw;
    static float s_routineDistance;
    static float s_routineFadeProgress;
    static bool s_routineSampleWritten;
    static uint32_t s_routineState;

    RoutineController g_routineController;

private:
    // OpenVR overlay handles
    vr::VROverlayHandle_t m_ulOverlayHandle;
    vr::VROverlayHandle_t m_ulThumbnailHandle;
    vr::VROverlayHandle_t m_ulBorderOverlayHandle;
    
    // OpenGL textures
    GLuint m_glTextureId;
    GLuint m_glBorderTextureId;
    
    // Texture dimensions
    int m_nTextureWidth;
    int m_nTextureHeight;
    int m_borderTextureWidth;
    int m_borderTextureHeight;
    
    // Texture data
    uint8_t* m_pTextureData;
    uint8_t* m_borderTextureData;
    
    // Current position of the target in angles (yaw, pitch)
    float m_targetYawAngle;
    float m_targetPitchAngle;

    bool m_targetIsPreview;
    
    // Flag to track if the overlay is visible
    bool m_isVisible;
    
    // Initialize OpenGL
    bool InitializeOpenGL();
    
    // Initialize the target texture
    bool CreateTargetTexture();
    
    // Update target texture if needed
    void UpdateOverlayTexture();
    
    vr::HmdMatrix34_t GetHmdPose() const;
    MU_Vector3 CalculateTargetPosition() const;
    
    // Update the overlay transform
    void UpdateOverlayTransform(const MU_Vector3& targetPosition);
    static void ResetFixedTargetPosition();
    static bool s_positionInitialized;  // Static class variable to track initialization

    // Text rendering members
    bool m_showText;
    std::string m_displayText;
    stbtt_fontinfo m_font;
    unsigned char* m_fontBuffer;
    float m_fontSize;
    std::map<char, CachedGlyph> m_glyphCache;

    // Text rendering methods
    bool InitializeFont(const char* fontPath, float fontSize);
    void RenderText(const std::string& text, int x, int y, uint32_t color);
    void RenderSingleLine(const std::string& line, int x, int baseline, uint32_t color);
    int MeasureTextWidth(const std::string& text);
    int MeasureLineWidth(const std::string& line);
    
    // OpenGL context (kept private)
    HWND m_hWnd;
};