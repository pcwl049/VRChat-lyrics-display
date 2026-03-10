/**
 * Nova Renderer - Scene Graph and Camera System Implementation
 */

#include "Nova/Scene.h"
#include "Nova/VulkanBackend.h"
#include <algorithm>
#include <cmath>

namespace Nova {

// ============================================================================
// Transform Implementation
// ============================================================================

Mat4 Transform::toMatrix() const {
    Mat4 result = Mat4::identity();
    
    // 平移
    result.m[12] = position.x;
    result.m[13] = position.y;
    result.m[14] = position.z;
    
    // 缩放
    result.m[0] *= scale.x;
    result.m[5] *= scale.y;
    result.m[10] *= scale.z;
    
    return result;
}

void Transform::fromMatrix(const Mat4& matrix) {
    position = Vec3(matrix.m[12], matrix.m[13], matrix.m[14]);
    scale = Vec3(1.0f);
    rotation = Quat::identity();
}

Transform Transform::operator*(const Transform& other) const {
    Transform result;
    result.position = position + other.position;
    result.rotation = rotation * other.rotation;
    result.scale = Vec3(scale.x * other.scale.x, scale.y * other.scale.y, scale.z * other.scale.z);
    return result;
}

Transform Transform::lerp(const Transform& a, const Transform& b, f32 t) {
    Transform result;
    result.position = Vec3::lerp(a.position, b.position, t);
    result.rotation = a.rotation;
    result.scale = Vec3::lerp(a.scale, b.scale, t);
    return result;
}

Vec3 Transform::forward() const {
    return Vec3(0, 0, -1);
}

Vec3 Transform::right() const {
    return Vec3(1, 0, 0);
}

Vec3 Transform::up() const {
    return Vec3(0, 1, 0);
}

// ============================================================================
// SceneNode Implementation
// ============================================================================

SceneNode::SceneNode() = default;
SceneNode::~SceneNode() = default;

void SceneNode::setParent(SceneNode* parent) {
    parent_ = parent;
}

void SceneNode::addChild(SceneNode* child) {
    if (child) {
        child->parent_ = this;
        children_.push_back(child);
    }
}

void SceneNode::removeChild(SceneNode* child) {
    auto it = std::find(children_.begin(), children_.end(), child);
    if (it != children_.end()) {
        (*it)->parent_ = nullptr;
        children_.erase(it);
    }
}

Mat4 SceneNode::getWorldMatrix() const {
    return worldMatrix_;
}

void SceneNode::updateWorldMatrix(const Mat4& parentWorld) {
    Mat4 local = transform_.toMatrix();
    // 简化矩阵乘法
    worldMatrix_ = local;
    
    for (SceneNode* child : children_) {
        child->updateWorldMatrix(worldMatrix_);
    }
    dirty_ = false;
}

void SceneNode::traverse(const std::function<void(SceneNode*)>& visitor) {
    visitor(this);
    for (SceneNode* child : children_) {
        child->traverse(visitor);
    }
}

void SceneNode::traverseDepthFirst(const std::function<bool(SceneNode*)>& visitor) {
    if (!visitor(this)) return;
    for (SceneNode* child : children_) {
        child->traverseDepthFirst(visitor);
    }
}

// ============================================================================
// SceneGraph Implementation
// ============================================================================

SceneGraph::SceneGraph() {
    root_ = new SceneNode();
}

SceneGraph::~SceneGraph() {
    delete root_;
}

bool SceneGraph::initialize() {
    return true;
}

void SceneGraph::shutdown() {
    // 清理资源
}

SceneNode* SceneGraph::createNode(const std::string& name) {
    SceneNode* node = new SceneNode();
    node->setName(name);
    root_->addChild(node);
    return node;
}

void SceneGraph::destroyNode(SceneNode* node) {
    if (node && node->getParent()) {
        node->getParent()->removeChild(node);
    }
    delete node;
}

void SceneGraph::update() {
    if (root_) {
        Mat4 identity = Mat4::identity();
        root_->updateWorldMatrix(identity);
    }
}

SceneNode* SceneGraph::findNode(const std::string& name) {
    return findNodeRecursive(root_, name);
}

SceneNode* SceneGraph::findNodeRecursive(SceneNode* node, const std::string& name) {
    if (!node) return nullptr;
    if (node->getName() == name) return node;
    
    for (SceneNode* child : node->getChildren()) {
        SceneNode* found = findNodeRecursive(child, name);
        if (found) return found;
    }
    return nullptr;
}

// ============================================================================
// Camera Implementation
// ============================================================================

Camera::Camera() {
    params_.fov = 60.0f;
    params_.aspectRatio = 16.0f / 9.0f;
    params_.nearPlane = 0.1f;
    params_.farPlane = 1000.0f;
    params_.orthographic = false;
}

Mat4 Camera::getViewMatrix() const {
    Mat4 view = Mat4::identity();
    Vec3 pos = transform_.position;
    
    view.m[12] = -pos.x;
    view.m[13] = -pos.y;
    view.m[14] = -pos.z;
    
    return view;
}

Mat4 Camera::getProjectionMatrix() const {
    Mat4 proj = Mat4::identity();
    
    if (params_.orthographic) {
        f32 w = params_.orthoWidth * 0.5f;
        f32 h = params_.orthoHeight * 0.5f;
        proj.m[0] = 1.0f / w;
        proj.m[5] = 1.0f / h;
        proj.m[10] = -2.0f / (params_.farPlane - params_.nearPlane);
        proj.m[14] = -(params_.farPlane + params_.nearPlane) / (params_.farPlane - params_.nearPlane);
    } else {
        f32 fovRad = params_.fov * 3.14159f / 180.0f;
        f32 tanHalfFov = std::tan(fovRad * 0.5f);
        proj.m[0] = 1.0f / (params_.aspectRatio * tanHalfFov);
        proj.m[5] = 1.0f / tanHalfFov;
        proj.m[10] = -(params_.farPlane + params_.nearPlane) / (params_.farPlane - params_.nearPlane);
        proj.m[11] = -1.0f;
        proj.m[14] = -(2.0f * params_.farPlane * params_.nearPlane) / (params_.farPlane - params_.nearPlane);
        proj.m[15] = 0.0f;
    }
    
    return proj;
}

Mat4 Camera::getViewProjectionMatrix() const {
    return getProjectionMatrix() * getViewMatrix();
}

void Camera::lookAt(const Vec3& target, const Vec3& worldUp) {
    // 简化实现
}

void Camera::setPerspective(f32 fov, f32 aspect, f32 nearDist, f32 farDist) {
    params_.fov = fov;
    params_.aspectRatio = aspect;
    params_.nearPlane = nearDist;
    params_.farPlane = farDist;
    params_.orthographic = false;
}

void Camera::setOrthographic(f32 width, f32 height, f32 nearDist, f32 farDist) {
    params_.orthoWidth = width;
    params_.orthoHeight = height;
    params_.nearPlane = nearDist;
    params_.farPlane = farDist;
    params_.orthographic = true;
}

// ============================================================================
// Frustum Implementation
// ============================================================================

void Frustum::update(const Mat4& viewProjection) {
}

bool Frustum::containsPoint(const Vec3& point) const {
    return true;
}

bool Frustum::containsSphere(const Vec3& center, f32 radius) const {
    return true;
}

bool Frustum::containsAABB(const Vec3& min, const Vec3& max) const {
    return true;
}

// ============================================================================
// CameraController Implementation
// ============================================================================

CameraController::CameraController() = default;
CameraController::~CameraController() = default;

void CameraController::orbit(f32 yawDelta, f32 pitchDelta) {
    yaw_ += yawDelta * orbitSpeed_;
    pitch_ += pitchDelta * orbitSpeed_;
    pitch_ = std::max(minPitch_, std::min(maxPitch_, pitch_));
}

void CameraController::zoom(f32 delta) {
    distance_ -= delta * zoomSpeed_;
    distance_ = std::max(minDistance_, std::min(maxDistance_, distance_));
}

void CameraController::pan(const Vec2& delta) {
    if (!camera_) return;
    Vec3 right = Vec3(1, 0, 0);
    Vec3 up = Vec3(0, 1, 0);
    target_ = target_ + right * delta.x * panSpeed_ + up * delta.y * panSpeed_;
}

void CameraController::moveForward(f32 delta) {
    if (!camera_) return;
}

void CameraController::moveRight(f32 delta) {
    if (!camera_) return;
}

void CameraController::moveUp(f32 delta) {
    if (!camera_) return;
}

void CameraController::rotate(f32 yawDelta, f32 pitchDelta) {
    yaw_ += yawDelta * orbitSpeed_;
    pitch_ += pitchDelta * orbitSpeed_;
}

void CameraController::update(f32 deltaTime) {
    if (!camera_) return;
}

// ============================================================================
// Scene Implementation (backward compat)
// ============================================================================

Scene::Scene() : SceneGraph() {}
Scene::~Scene() = default;

} // namespace Nova
