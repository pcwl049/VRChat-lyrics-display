/**
 * Nova Renderer - Core Types
 * 核心类型定义
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>

namespace Nova {

// ============================================================================
// 基础类型
// ============================================================================

using i8  = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;
using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using f32 = float;
using f64 = double;

// Texture/Buffer Format
enum class Format : u32 {
    R8_UNORM,
    RG8_UNORM,
    RGBA8_UNORM,
    RGBA16_UINT,
    BC1_RGB_UNORM,
    BC2_UNORM,
    BC3_UNORM,
    BC5_UNORM,
    BC6H_UFLOAT,
    BC7_UNORM,
    R16F,
    RG16F,
    RGBA16F,
    R32F,
    RG32F,
    RGBA32F,
    Depth32F
};

// Math Types

struct Vec2 {
    f32 x, y;
    
    Vec2() : x(0), y(0) {}
    Vec2(f32 v) : x(v), y(v) {}
    Vec2(f32 x, f32 y) : x(x), y(y) {}
    
    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(f32 s) const { return {x * s, y * s}; }
    Vec2 operator*(const Vec2& o) const { return {x * o.x, y * o.y}; }
    Vec2 operator/(f32 s) const { return {x / s, y / s}; }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    
    f32 length() const { return std::sqrt(x * x + y * y); }
    f32 lengthSq() const { return x * x + y * y; }
    Vec2 normalized() const { f32 l = length(); return l > 0 ? *this / l : Vec2{}; }
    f32 dot(const Vec2& o) const { return x * o.x + y * o.y; }
    static Vec2 lerp(const Vec2& a, const Vec2& b, f32 t) { return a + (b - a) * t; }
};

struct Vec3 {
    f32 x, y, z;
    
    Vec3() : x(0), y(0), z(0) {}
    Vec3(f32 v) : x(v), y(v), z(v) {}
    Vec3(f32 x, f32 y, f32 z) : x(x), y(y), z(z) {}
    Vec3(const Vec2& v, f32 z) : x(v.x), y(v.y), z(z) {}
    
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(f32 s) const { return {x * s, y * s, z * s}; }
    Vec3 operator*(const Vec3& o) const { return {x * o.x, y * o.y, z * o.z}; }
    Vec3 operator/(f32 s) const { return {x / s, y / s, z / s}; }
    
    f32 length() const { return std::sqrt(x * x + y * y + z * z); }
    f32 lengthSq() const { return x * x + y * y + z * z; }
    Vec3 normalized() const { f32 l = length(); return l > 0 ? *this / l : Vec3{}; }
    f32 dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3& o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    static Vec3 lerp(const Vec3& a, const Vec3& b, f32 t) { return a + (b - a) * t; }
};

struct Vec4 {
    union { f32 x, r; };
    union { f32 y, g; };
    union { f32 z, b; };
    union { f32 w, a; };
    
    Vec4() : x(0), y(0), z(0), w(0) {}
    Vec4(f32 v) : x(v), y(v), z(v), w(v) {}
    Vec4(f32 x, f32 y, f32 z, f32 w) : x(x), y(y), z(z), w(w) {}
    Vec4(const Vec2& v, f32 z, f32 w) : x(v.x), y(v.y), z(z), w(w) {}
    Vec4(const Vec3& v, f32 w) : x(v.x), y(v.y), z(v.z), w(w) {}
    
    Vec4 operator+(const Vec4& o) const { return {x + o.x, y + o.y, z + o.z, w + o.w}; }
    Vec4 operator-(const Vec4& o) const { return {x - o.x, y - o.y, z - o.z, w - o.w}; }
    Vec4 operator*(f32 s) const { return {x * s, y * s, z * s, w * s}; }
    
    static Vec4 lerp(const Vec4& a, const Vec4& b, f32 t) { return a + (b - a) * t; }
};

// 整型向量
struct Vec2i {
    i32 x, y;
    
    Vec2i() : x(0), y(0) {}
    Vec2i(i32 v) : x(v), y(v) {}
    Vec2i(i32 x, i32 y) : x(x), y(y) {}
    
    Vec2i operator+(const Vec2i& o) const { return {x + o.x, y + o.y}; }
    Vec2i operator-(const Vec2i& o) const { return {x - o.x, y - o.y}; }
    Vec2i operator*(i32 s) const { return {x * s, y * s}; }
};

struct Vec3i {
    i32 x, y, z;
    
    Vec3i() : x(0), y(0), z(0) {}
    Vec3i(i32 v) : x(v), y(v), z(v) {}
    Vec3i(i32 x, i32 y, i32 z) : x(x), y(y), z(z) {}
    
    Vec3i operator+(const Vec3i& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3i operator-(const Vec3i& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3i operator*(i32 s) const { return {x * s, y * s, z * s}; }
};

struct Vec4i {
    i32 x, y, z, w;
    
    Vec4i() : x(0), y(0), z(0), w(0) {}
    Vec4i(i32 v) : x(v), y(v), z(v), w(v) {}
    Vec4i(i32 x, i32 y, i32 z, i32 w) : x(x), y(y), z(z), w(w) {}
    
    Vec4i operator+(const Vec4i& o) const { return {x + o.x, y + o.y, z + o.z, w + o.w}; }
    Vec4i operator-(const Vec4i& o) const { return {x - o.x, y - o.y, z - o.z, w - o.w}; }
    Vec4i operator*(i32 s) const { return {x * s, y * s, z * s, w * s}; }
};

struct Quat {
    f32 w, x, y, z;
    Quat() : w(1.0f), x(0.0f), y(0.0f), z(0.0f) {}
    Quat(f32 _w, f32 _x, f32 _y, f32 _z) : w(_w), x(_x), y(_y), z(_z) {}
    Quat operator*(const Quat& o) const {
        return Quat(w*o.w - x*o.x - y*o.y - z*o.z,
                    w*o.x + x*o.w + y*o.z - z*o.y,
                    w*o.y - x*o.z + y*o.w + z*o.x,
                    w*o.z + x*o.y - y*o.x + z*o.w);
    }
    Quat normalized() const {
        f32 len = std::sqrt(w*w + x*x + y*y + z*z);
        return len > 0 ? Quat(w/len, x/len, y/len, z/len) : Quat();
    }
    static Quat fromAxisAngle(const Vec3& axis, f32 angle) {
        f32 ha = angle * 0.5f;
        f32 s = std::sin(ha);
        return Quat(std::cos(ha), axis.x*s, axis.y*s, axis.z*s);
    }
    static Quat identity() { return Quat(); }
};

// 3x3 矩阵
struct Mat3 {
    f32 m[9];
    
    Mat3() {
        for (int i = 0; i < 9; i++) m[i] = 0;
        m[0] = m[4] = m[8] = 1;
    }
    
    static Mat3 identity() { return Mat3{}; }
    
    static Mat3 translation(f32 tx, f32 ty) {
        Mat3 r;
        r.m[6] = tx;
        r.m[7] = ty;
        return r;
    }
    
    static Mat3 scale(f32 sx, f32 sy) {
        Mat3 r;
        r.m[0] = sx;
        r.m[4] = sy;
        return r;
    }
    
    static Mat3 rotation(f32 angle) {
        Mat3 r;
        f32 c = std::cos(angle);
        f32 s = std::sin(angle);
        r.m[0] = c;
        r.m[1] = s;
        r.m[3] = -s;
        r.m[4] = c;
        return r;
    }
    
    Mat3 operator*(const Mat3& o) const {
        Mat3 r;
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                r.m[i * 3 + j] = 0;
                for (int k = 0; k < 3; k++) {
                    r.m[i * 3 + j] += m[i * 3 + k] * o.m[k * 3 + j];
                }
            }
        }
        return r;
    }
    
    Vec2 transform(const Vec2& v) const {
        f32 x = m[0] * v.x + m[3] * v.y + m[6];
        f32 y = m[1] * v.x + m[4] * v.y + m[7];
        return {x, y};
    }
};

// 4x4 矩阵
struct Mat4 {
    f32 m[16];
    
    Mat4() {
        for (int i = 0; i < 16; i++) m[i] = 0;
        m[0] = m[5] = m[10] = m[15] = 1;
    }
    
    static Mat4 identity() { return Mat4{}; }
    
    static Mat4 ortho(f32 left, f32 right, f32 bottom, f32 top, f32 nearZ, f32 farZ) {
        Mat4 r;
        r.m[0] = 2.0f / (right - left);
        r.m[5] = 2.0f / (top - bottom);
        r.m[10] = -2.0f / (farZ - nearZ);
        r.m[12] = -(right + left) / (right - left);
        r.m[13] = -(top + bottom) / (top - bottom);
        r.m[14] = -(farZ + nearZ) / (farZ - nearZ);
        return r;
    }
    
    static Mat4 perspective(f32 fov, f32 aspect, f32 nearZ, f32 farZ) {
        Mat4 r;
        f32 tanHalfFov = std::tan(fov / 2.0f);
        r.m[0] = 1.0f / (aspect * tanHalfFov);
        r.m[5] = 1.0f / tanHalfFov;
        r.m[10] = -(farZ + nearZ) / (farZ - nearZ);
        r.m[11] = -1.0f;
        r.m[14] = -(2.0f * farZ * nearZ) / (farZ - nearZ);
        r.m[15] = 0.0f;
        return r;
    }
    
    static Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
        Vec3 f = (center - eye).normalized();
        Vec3 s = f.cross(up).normalized();
        Vec3 u = s.cross(f);
        
        Mat4 r;
        r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
        r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
        r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
        r.m[12] = -s.dot(eye);
        r.m[13] = -u.dot(eye);
        r.m[14] = f.dot(eye);
        return r;
    }
    
    Mat4 operator*(const Mat4& o) const {
        // 列主序矩阵乘法
        Mat4 r;
        for (int col = 0; col < 4; col++) {
            for (int row = 0; row < 4; row++) {
                r.m[col * 4 + row] = 0;
                for (int k = 0; k < 4; k++) {
                    r.m[col * 4 + row] += m[k * 4 + row] * o.m[col * 4 + k];
                }
            }
        }
        return r;
    }
    
    Vec4 transform(const Vec4& v) const {
        return {
            m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12] * v.w,
            m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13] * v.w,
            m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * v.w,
            m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15] * v.w
        };
    }
};

// 矩形
struct Rect {
    f32 x, y, width, height;
    
    Rect() : x(0), y(0), width(0), height(0) {}
    Rect(f32 x, f32 y, f32 w, f32 h) : x(x), y(y), width(w), height(h) {}
    
    bool contains(const Vec2& p) const {
        return p.x >= x && p.x <= x + width && p.y >= y && p.y <= y + height;
    }
    
    bool intersects(const Rect& o) const {
        return x < o.x + o.width && x + width > o.x &&
               y < o.y + o.height && y + height > o.y;
    }
    
    Rect intersect(const Rect& o) const {
        f32 newX = std::max(x, o.x);
        f32 newY = std::max(y, o.y);
        f32 newW = std::min(x + width, o.x + o.width) - newX;
        f32 newH = std::min(y + height, o.y + o.height) - newY;
        return {newX, newY, std::max(0.0f, newW), std::max(0.0f, newH)};
    }
    
    Vec2 center() const { return {x + width / 2, y + height / 2}; }
    Vec2 size() const { return {width, height}; }
};

// 颜色
struct Color {
    f32 r, g, b, a;
    
    Color() : r(0), g(0), b(0), a(1) {}
    Color(f32 r, f32 g, f32 b, f32 a = 1.0f) : r(r), g(g), b(b), a(a) {}
    
    static Color fromRGBA8(u8 r, u8 g, u8 b, u8 a = 255) {
        return {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
    }
    
    static Color fromHex(u32 hex) {
        return {
            ((hex >> 24) & 0xFF) / 255.0f,
            ((hex >> 16) & 0xFF) / 255.0f,
            ((hex >> 8) & 0xFF) / 255.0f,
            (hex & 0xFF) / 255.0f
        };
    }
    
    static Color white() { return {1, 1, 1, 1}; }
    static Color black() { return {0, 0, 0, 1}; }
    static Color transparent() { return {0, 0, 0, 0}; }
    static Color red() { return {1, 0, 0, 1}; }
    static Color green() { return {0, 1, 0, 1}; }
    static Color blue() { return {0, 0, 1, 1}; }
    
    Color withAlpha(f32 alpha) const { return {r, g, b, alpha}; }
    
    Color operator*(f32 s) const { return {r * s, g * s, b * s, a * s}; }
    Color operator*(const Color& o) const { return {r * o.r, g * o.g, b * o.b, a * o.a}; }
    
    static Color lerp(const Color& a, const Color& b, f32 t) {
        return {
            a.r + (b.r - a.r) * t,
            a.g + (b.g - a.g) * t,
            a.b + (b.b - a.b) * t,
            a.a + (b.a - a.a) * t
        };
    }
    
    u32 toRGBA8() const {
        return (u32(r * 255) << 24) | (u32(g * 255) << 16) | 
               (u32(b * 255) << 8) | u32(a * 255);
    }
};

// ============================================================================
// 句柄系统
// ============================================================================

template<typename Tag>
class Handle {
public:
    u32 index = 0;
    u32 generation = 0;
    
    Handle() : index(0), generation(0) {}
    explicit Handle(u32 i) : index(i), generation(0) {}
    Handle(u32 i, u32 g) : index(i), generation(g) {}
    
    bool valid() const { return index != 0 || generation != 0; }
    bool operator==(const Handle& o) const { return index == o.index && generation == o.generation; }
    bool operator!=(const Handle& o) const { return !(*this == o); }
    
    explicit operator bool() const { return valid(); }
};

// 资源句柄类型
struct BufferTag {};
struct TextureTag {};
struct SamplerTag {};
struct PipelineTag {};
struct ShaderTag {};
struct RenderPassTag {};
struct FramebufferTag {};
struct DescriptorSetTag {};
struct FontTag {};
struct ImageTag {};

using BufferHandle = Handle<BufferTag>;
using TextureHandle = Handle<TextureTag>;
using SamplerHandle = Handle<SamplerTag>;
using PipelineHandle = Handle<PipelineTag>;
using ShaderHandle = Handle<ShaderTag>;
using RenderPassHandle = Handle<RenderPassTag>;
using FramebufferHandle = Handle<FramebufferTag>;
using DescriptorSetHandle = Handle<DescriptorSetTag>;
using FontHandle = Handle<FontTag>;
using ImageHandle = Handle<ImageTag>;

// ============================================================================
// Resource Pool Template
// ============================================================================

template<typename T, typename Tag = void>
class ResourcePool {
public:
    struct Entry {
        T resource;
        u32 generation = 1;
        bool used = false;
    };
    
    u32 insert(T&& resource) {
        for (u32 i = 1; i < entries_.size(); i++) {
            if (!entries_[i].used) {
                entries_[i].resource = std::move(resource);
                entries_[i].used = true;
                return i;
            }
        }
        u32 id = static_cast<u32>(entries_.size());
        entries_.push_back({std::move(resource), 1, true});
        return id;
    }
    
    void erase(u32 index) {
        if (index > 0 && index < entries_.size()) {
            entries_[index].generation++;
            entries_[index].used = false;
        }
    }
    
    T* get(u32 index) {
        if (index > 0 && index < entries_.size() && entries_[index].used) {
            return &entries_[index].resource;
        }
        return nullptr;
    }
    
    const T* get(u32 index) const {
        if (index > 0 && index < entries_.size() && entries_[index].used) {
            return &entries_[index].resource;
        }
        return nullptr;
    }
    
    u32 nextId() const {
        for (u32 i = 1; i < entries_.size(); i++) {
            if (!entries_[i].used) return i;
        }
        return static_cast<u32>(entries_.size());
    }
    
    void clear() {
        entries_.clear();
        entries_.push_back({});
    }
    
    size_t size() const {
        size_t count = 0;
        for (const auto& e : entries_) {
            if (e.used) count++;
        }
        return count;
    }
    
private:
    std::vector<Entry> entries_ = {{}};
};

// Resource Description Structures

struct BufferDesc {
    u64 size = 0;
    u32 usage = 0;
    bool mapped = false;
    const char* debugName = nullptr;
};

struct TextureDesc {
    u32 width = 1;
    u32 height = 1;
    u32 depth = 1;
    u32 mipLevels = 1;
    u32 arrayLayers = 1;
    Format format = Format::RGBA8_UNORM;
    u32 usage = 0;
    u32 samples = 1;
    const char* debugName = nullptr;
};

struct SamplerDesc {
    f32 minFilter = 0;
    f32 magFilter = 0;
    f32 mipFilter = 0;
    f32 addressModeU = 0;
    f32 addressModeV = 0;
    f32 addressModeW = 0;
    f32 maxAnisotropy = 1.0f;
    const char* debugName = nullptr;
};

struct ShaderDesc {
    const char* vertexSource = nullptr;
    const char* fragmentSource = nullptr;
    const char* computeSource = nullptr;
    const char* debugName = nullptr;
};

struct VertexAttribute {
    u32 location;
    u32 format;  // VkFormat
    u32 offset;
};

struct VertexBinding {
    u32 binding;
    u32 stride;
    bool perInstance = false;
};

struct PipelineDesc {
    ShaderHandle shader;
    std::vector<VertexAttribute> attributes;
    std::vector<VertexBinding> bindings;
    u32 blendMode = 0;
    bool depthTest = false;
    bool depthWrite = false;
    const char* debugName = nullptr;
};

// ============================================================================
// 结果和错误处理
// ============================================================================

template<typename T>
class Result {
public:
    bool success;
    T value;
    std::string error;
    
    static Result ok(T v) {
        Result r;
        r.success = true;
        r.value = std::move(v);
        return r;
    }
    
    static Result err(const std::string& e) {
        Result r;
        r.success = false;
        r.error = e;
        return r;
    }
    
    explicit operator bool() const { return success; }
};

template<>
class Result<void> {
public:
    bool success;
    std::string error;
    
    static Result ok() {
        Result r;
        r.success = true;
        return r;
    }
    
    static Result err(const std::string& e) {
        Result r;
        r.success = false;
        r.error = e;
        return r;
    }
    
    explicit operator bool() const { return success; }
};

} // namespace Nova
