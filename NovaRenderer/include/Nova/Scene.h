/**
 * Nova Renderer - Scene Graph and Camera System
 * 场景图和相机系统
 */

#pragma once

#include "Types.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>

namespace Nova {

class VulkanBackend;

// ============================================================================
// 变换
// ============================================================================

struct Transform {
    Vec3 position = Vec3(0.0f);
    Quat rotation = Quat(1.0f, 0.0f, 0.0f, 0.0f);
    Vec3 scale = Vec3(1.0f);
    
    // 组合变换
    Mat4 toMatrix() const;
    
    // 从矩阵设置
    void fromMatrix(const Mat4& matrix);
    
    // 组合变换
    Transform operator*(const Transform& other) const;
    
    // 插值
    static Transform lerp(const Transform& a, const Transform& b, f32 t);
    
    // 前向/右向/上向向量
    Vec3 forward() const;
    Vec3 right() const;
    Vec3 up() const;
    
    // 看向目标
    void lookAt(const Vec3& target, const Vec3& worldUp = Vec3(0, 1, 0));
    
    // 重置
    void reset();
};

// ============================================================================
// 场景节点
// ============================================================================

class SceneNode {
public:
    SceneNode();
    virtual ~SceneNode();
    
    // 名称
    void setName(const std::string& name) { name_ = name; }
    const std::string& getName() const { return name_; }
    
    // 变换
    Transform& getTransform() { return transform_; }
    const Transform& getTransform() const { return transform_; }
    void setTransform(const Transform& transform) { transform_ = transform; dirty_ = true; }
    
    // 世界变换
    Mat4 getWorldMatrix() const;
    void updateWorldMatrix(const Mat4& parentWorld);
    
    // 层级
    SceneNode* getParent() const { return parent_; }
    void setParent(SceneNode* parent);
    
    void addChild(SceneNode* child);
    void removeChild(SceneNode* child);
    const std::vector<SceneNode*>& getChildren() const { return children_; }
    
    // 遍历
    void traverse(const std::function<void(SceneNode*)>& visitor);
    void traverseDepthFirst(const std::function<bool(SceneNode*)>& visitor);
    
    // 可见性
    void setVisible(bool visible) { visible_ = visible; }
    bool isVisible() const { return visible_; }
    
    // 脏标志
    bool isDirty() const { return dirty_; }
    void markDirty() { dirty_ = true; }
    void clearDirty() { dirty_ = false; }
    
    // 用户数据
    void setUserData(void* data) { userData_ = data; }
    void* getUserData() const { return userData_; }
    
    // ID
    u32 getId() const { return id_; }
    
protected:
    u32 id_;
    std::string name_;
    Transform transform_;
    Mat4 worldMatrix_;
    
    SceneNode* parent_ = nullptr;
    std::vector<SceneNode*> children_;
    
    bool visible_ = true;
    mutable bool dirty_ = true;
    
    void* userData_ = nullptr;
};

// ============================================================================
// 场景图
// ============================================================================

class SceneGraph {
public:
    SceneGraph();
    ~SceneGraph();
    
    bool initialize();
    void shutdown();
    
    SceneNode* getRoot() const { return root_; }
    
    SceneNode* createNode(const std::string& name = "");
    void destroyNode(SceneNode* node);
    
    SceneNode* findNode(const std::string& name);
    SceneNode* findNode(u32 id) const;
    
    void update();
    void traverse(const std::function<void(SceneNode*)>& visitor);
    void clear();
    
    u32 getNodeCount() const { return nodeCount_; }
    
private:
    SceneNode* findNodeRecursive(SceneNode* node, const std::string& name);
    void updateNode(SceneNode* node, const Mat4& parentWorld);
    
    SceneNode* root_ = nullptr;
    std::unordered_map<u32, SceneNode*> nodes_;
    std::unordered_map<std::string, SceneNode*> nodesByName_;
    
    u32 nextNodeId_ = 1;
    u32 nodeCount_ = 0;
};

// ============================================================================
// 相机
// ============================================================================

// 投影类型
enum class ProjectionType : u32 {
    Perspective,
    Orthographic
};

// Frustum for culling
class Frustum {
public:
    Vec4 planes[6];
    
    void update(const Mat4& viewProjection);
    bool containsPoint(const Vec3& point) const;
    bool containsSphere(const Vec3& center, f32 radius) const;
    bool containsAABB(const Vec3& min, const Vec3& max) const;
};

// Camera Parameters
struct CameraParams {
    // 透视投影
    f32 fov = 60.0f;
    f32 aspectRatio = 16.0f / 9.0f;
    f32 nearPlane = 0.1f;
    f32 farPlane = 1000.0f;
    
    // 正交投影
    f32 orthoWidth = 10.0f;
    f32 orthoHeight = 10.0f;
    bool orthographic = false;
    
    ProjectionType projectionType = ProjectionType::Perspective;
};

// 相机
class Camera {
    friend class CameraController;
public:
    Camera();
    ~Camera();
    
    // 初始化
    void initialize(const CameraParams& params);
    
    // 变换
    Transform& getTransform() { return transform_; }
    const Transform& getTransform() const { return transform_; }
    
    // 投影
    void setPerspective(f32 fov, f32 aspect, f32 nearDist, f32 farDist);
    void setOrthographic(f32 width, f32 height, f32 nearDist, f32 farDist);
    
    Mat4 getViewMatrix() const;
    Mat4 getProjectionMatrix() const;
    Mat4 getViewProjectionMatrix() const;
    
    Frustum getFrustum() const;
    
    // 参数
    CameraParams& getParams() { return params_; }
    const CameraParams& getParams() const { return params_; }
    
    // 视口
    void setViewport(f32 x, f32 y, f32 width, f32 height);
    struct Viewport {
        f32 x = 0, y = 0, width = 1, height = 1;
        f32 minDepth = 0, maxDepth = 1;
    };
    const Viewport& getViewport() const { return viewport_; }
    
    // 看向目标
    void lookAt(const Vec3& target, const Vec3& worldUp = Vec3(0, 1, 0));
    
    // 控制器
    void orbit(const Vec3& target, f32 yaw, f32 pitch, f32 distance);
    void pan(const Vec3& delta);
    void zoom(f32 delta);
    
    // 屏幕到世界
    Vec3 screenToWorld(const Vec2& screenPos, f32 depth = 1.0f) const;
    Vec2 worldToScreen(const Vec3& worldPos) const;
    
    // 射线
    struct Ray {
        Vec3 origin;
        Vec3 direction;
    };
    Ray screenPointToRay(const Vec2& screenPos) const;
    
private:
    Transform transform_;
    CameraParams params_;
    Viewport viewport_;
    
    mutable Mat4 projectionMatrix_;
    mutable bool projectionDirty_ = true;
};

// ============================================================================
// 相机控制器
// ============================================================================

// 相机控制器
class CameraController {
public:
    CameraController();
    ~CameraController();
    
    // 设置相机
    void setCamera(Camera* camera) { camera_ = camera; }
    Camera* getCamera() const { return camera_; }
    
    // 设置目标
    void setTarget(const Vec3& target) { target_ = target; }
    const Vec3& getTarget() const { return target_; }
    
    // 轨道控制
    void orbit(f32 yawDelta, f32 pitchDelta);
    void zoom(f32 delta);
    void pan(const Vec2& delta);
    
    // 第一人称控制
    void moveForward(f32 delta);
    void moveRight(f32 delta);
    void moveUp(f32 delta);
    void rotate(f32 yawDelta, f32 pitchDelta);
    
    // 更新
    void update(f32 deltaTime);
    
    // 设置
    void setOrbitSpeed(f32 speed) { orbitSpeed_ = speed; }
    void setPanSpeed(f32 speed) { panSpeed_ = speed; }
    void setZoomSpeed(f32 speed) { zoomSpeed_ = speed; }
    void setMoveSpeed(f32 speed) { moveSpeed_ = speed; }
    
    // 模式
    enum class Mode : u32 {
        Orbit,
        FirstPerson,
        Fly
    };
    void setMode(Mode mode) { mode_ = mode; }
    Mode getMode() const { return mode_; }
    
    // 限制
    void setMinDistance(f32 distance) { minDistance_ = distance; }
    void setMaxDistance(f32 distance) { maxDistance_ = distance; }
    void setMinPitch(f32 pitch) { minPitch_ = pitch; }
    void setMaxPitch(f32 pitch) { maxPitch_ = pitch; }
    
private:
    Camera* camera_ = nullptr;
    
    Vec3 target_ = Vec3(0.0f);
    f32 distance_ = 10.0f;
    f32 yaw_ = 0.0f;
    f32 pitch_ = 0.0f;
    
    f32 orbitSpeed_ = 0.5f;
    f32 panSpeed_ = 0.01f;
    f32 zoomSpeed_ = 1.0f;
    f32 moveSpeed_ = 5.0f;
    
    f32 minDistance_ = 1.0f;
    f32 maxDistance_ = 100.0f;
    f32 minPitch_ = -89.0f;
    f32 maxPitch_ = 89.0f;
    
    Mode mode_ = Mode::Orbit;
};

// ============================================================================
// 场景渲染器
// ============================================================================

// 渲染上下文
struct RenderContext {
    Camera* camera = nullptr;
    VkCommandBuffer commandBuffer;
    
    Mat4 viewMatrix;
    Mat4 projectionMatrix;
    Mat4 viewProjectionMatrix;
    Vec3 cameraPosition;
    
    Vec4 frustumPlanes[6];
    
    // 时间
    f32 time = 0.0f;
    f32 deltaTime = 0.0f;
    
    // 渲染目标
    TextureHandle renderTarget;
    TextureHandle depthTarget;
    
    // 统计
    u32 drawCallCount = 0;
    u32 triangleCount = 0;
};

// 渲染项
struct RenderItem {
    SceneNode* node = nullptr;
    Mat4 worldMatrix;
    u32 sortKey = 0;
    f32 distance = 0.0f;
};

// 场景渲染器
class SceneRenderer {
public:
    SceneRenderer();
    ~SceneRenderer();
    
    bool initialize(VulkanBackend* backend);
    void shutdown();
    
    // 设置场景
    void setScene(SceneGraph* scene) { scene_ = scene; }
    
    // 渲染
    void render(const RenderContext& context);
    
    // 收集渲染项
    void collectRenderItems(const RenderContext& context, std::vector<RenderItem>& items);
    
    // 排序
    void sortRenderItems(std::vector<RenderItem>& items, const Vec3& cameraPosition);
    
    // 绘制
    void drawRenderItems(const std::vector<RenderItem>& items, const RenderContext& context);
    
    // 剔除
    bool frustumCull(const RenderItem& item, const Frustum& frustum) const;
    
    // 统计
    u32 getDrawCallCount() const { return drawCallCount_; }
    u32 getTriangleCount() const { return triangleCount_; }
    
private:
    VulkanBackend* backend_ = nullptr;
    SceneGraph* scene_ = nullptr;
    
    std::vector<RenderItem> renderItems_;
    
    u32 drawCallCount_ = 0;
    u32 triangleCount_ = 0;
};

// Convenience Scene class (inherits from SceneGraph)
class Scene : public SceneGraph {
public:
    Scene();
    ~Scene();
};

} // namespace Nova
