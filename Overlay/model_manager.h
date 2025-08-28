#pragma once

#include <openvr/openvr.h>
#include <windows.h>
#include <GL/gl.h>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include "math_utils.h"

class ModelManager {
public:
    struct BlendShape {
        std::string name;
        std::vector<MU_Vector3> targetVertices;  // Target positions for this blendshape
        float weight;  // Current blend weight (0.0 to 1.0)
    };

    struct AnimationKeyframe {
        float time;
        std::map<std::string, float> blendWeights;  // blendshape name -> weight
    };

    struct AnimationClip {
        std::string name;
        std::vector<AnimationKeyframe> keyframes;
        float duration;
        bool loop;
    };

    struct Model3D {
        std::string name;
        vr::VROverlayHandle_t overlayHandle;
        
        // OpenGL resources
        GLuint textureId, framebuffer, depthBuffer;
        GLuint vertexBuffer, indexBuffer;
        GLuint normalBuffer, uvBuffer;
        
        // Geometry data
        std::vector<MU_Vector3> baseVertices;     // Original vertex positions
        std::vector<MU_Vector3> currentVertices;  // Current animated vertices
        std::vector<MU_Vector3> normals;
        std::vector<float> uvCoordinates;
        std::vector<unsigned int> indices;
        int indexCount;
        
        // Transform data
        MU_Vector3 position;
        MU_Vector3 rotation;
        MU_Vector3 scale;
        
        // Blendshape data
        std::vector<BlendShape> blendShapes;
        
        // Animation data
        std::vector<AnimationClip> animations;
        std::string currentAnimation;
        float animationTime;
        bool isAnimating;
        
        // Texture dimensions
        int textureWidth, textureHeight;
        
        Model3D() : overlayHandle(vr::k_ulOverlayHandleInvalid), 
                   textureId(0), framebuffer(0), depthBuffer(0),
                   vertexBuffer(0), indexBuffer(0), normalBuffer(0), uvBuffer(0),
                   indexCount(0), position({0,0,0}), rotation({0,0,0}), scale({1,1,1}),
                   animationTime(0.0f), isAnimating(false),
                   textureWidth(512), textureHeight(512) {}
    };

private:
    std::vector<std::unique_ptr<Model3D>> m_models;
    
    // OpenGL context (shared with OverlayManager for now)
    HWND m_hWnd;
    HDC m_hDC;
    HGLRC m_hRC;
    bool m_ownsContext;  // Whether we created our own context
    
    // Shader program for 3D rendering
    GLuint m_shaderProgram;
    GLuint m_vertexShader, m_fragmentShader;
    
    // Shader uniform locations
    GLint m_mvpMatrixLocation;
    GLint m_modelMatrixLocation;
    GLint m_viewMatrixLocation;
    GLint m_projectionMatrixLocation;
    GLint m_lightPosLocation;
    GLint m_lightColorLocation;
    
    // Camera settings for rendering
    MU_Vector3 m_cameraPosition;
    MU_Vector3 m_cameraTarget;
    MU_Vector3 m_cameraUp;
    float m_fov;
    float m_nearPlane;
    float m_farPlane;

public:
    ModelManager();
    ~ModelManager();

    // Initialization and cleanup
    bool Initialize();
    bool InitializeWithSharedContext(HDC hDC, HGLRC hRC);  // Use existing OpenGL context
    void Shutdown();
    
    // Model management
    int LoadModel(const std::string& name, const std::string& filePath);
    int CreateModel(const std::string& name);  // Create empty model for manual setup
    bool RemoveModel(int modelId);
    Model3D* GetModel(int modelId);
    int GetModelCount() const;
    
    // Model transform
    void SetModelPosition(int modelId, float x, float y, float z);
    void SetModelRotation(int modelId, float x, float y, float z);
    void SetModelScale(int modelId, float x, float y, float z);
    void SetModelWorldTransform(int modelId, const MU_Matrix4& transform);
    
    // Blendshape management
    bool AddBlendShape(int modelId, const std::string& name, const std::vector<MU_Vector3>& targetVertices);
    void SetBlendShapeWeight(int modelId, const std::string& blendShapeName, float weight);
    float GetBlendShapeWeight(int modelId, const std::string& blendShapeName);
    void SetAllBlendShapeWeights(int modelId, const std::map<std::string, float>& weights);
    void ClearBlendShapeWeights(int modelId);
    
    // Animation management
    bool AddAnimation(int modelId, const AnimationClip& animation);
    void PlayAnimation(int modelId, const std::string& animationName, bool loop = true);
    void StopAnimation(int modelId);
    void PauseAnimation(int modelId);
    void ResumeAnimation(int modelId);
    void SetAnimationTime(int modelId, float time);
    float GetAnimationTime(int modelId);
    
    // Camera control
    void SetCamera(const MU_Vector3& position, const MU_Vector3& target, const MU_Vector3& up);
    void SetCameraFOV(float fov);
    void SetCameraClipping(float nearPlane, float farPlane);
    
    // Rendering
    void Update(float deltaTime);
    void RenderModel(int modelId);
    void RenderAllModels();
    
    // Overlay management
    void SetOverlaySize(int modelId, float sizeInMeters);
    void ShowOverlay(int modelId);
    void HideOverlay(int modelId);
    bool IsOverlayVisible(int modelId);

private:
    // Internal rendering methods
    bool InitializeShaders();
    bool InitializeOpenGL();
    void CleanupOpenGL();
    
    // Model loading helpers
    bool LoadOBJModel(const std::string& filePath, Model3D* model);
    bool LoadGLTFModel(const std::string& filePath, Model3D* model);
    
    // Blendshape processing
    void UpdateBlendShapes(Model3D* model);
    void ApplyBlendShape(Model3D* model, const BlendShape& blendShape);
    
    // Animation processing
    void UpdateAnimation(Model3D* model, float deltaTime);
    AnimationKeyframe InterpolateKeyframes(const AnimationKeyframe& a, const AnimationKeyframe& b, float t);
    
    // OpenGL helpers
    bool CreateFramebuffer(Model3D* model);
    void UpdateVertexBuffer(Model3D* model);
    GLuint CompileShader(GLenum shaderType, const char* source);
    GLuint LinkShaderProgram(GLuint vertexShader, GLuint fragmentShader);
    
    // Matrix helpers
    MU_Matrix4 CreateProjectionMatrix(float fov, float aspect, float nearPlane, float farPlane);
    MU_Matrix4 CreateViewMatrix(const MU_Vector3& position, const MU_Vector3& target, const MU_Vector3& up);
    MU_Matrix4 CreateModelMatrix(const MU_Vector3& position, const MU_Vector3& rotation, const MU_Vector3& scale);
};
