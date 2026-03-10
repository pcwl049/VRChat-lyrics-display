/**
 * Render Pipeline - 自研渲染管线
 * 极致优化版本 v1.0
 * 
 * 特性:
 * - 图层缓存系统: 静态元素缓存，只重绘变化部分
 * - 脏区域追踪: 最小化重绘范围
 * - 命令队列: 批量绘制，减少状态切换
 * - 多后端支持: GDI+/Direct2D/Direct3D
 */

#pragma once

#include <windows.h>
#include <gdiplus.h>
#include <vector>
#include <list>
#include <unordered_map>
#include <memory>
#include <functional>
#include <algorithm>

#pragma comment(lib, "gdiplus.lib")

namespace RenderPipeline {

// ============================================================================
// 基础类型定义
// ============================================================================

struct Rect {
    int x, y, width, height;
    
    Rect() : x(0), y(0), width(0), height(0) {}
    Rect(int x, int y, int w, int h) : x(x), y(y), width(w), height(h) {}
    
    bool intersects(const Rect& other) const {
        return x < other.x + other.width && x + width > other.x &&
               y < other.y + other.height && y + height > other.y;
    }
    
    Rect intersect(const Rect& other) const {
        int x1 = max(x, other.x);
        int y1 = max(y, other.y);
        int x2 = min(x + width, other.x + other.width);
        int y2 = min(y + height, other.y + other.height);
        if (x2 > x1 && y2 > y1)
            return Rect(x1, y1, x2 - x1, y2 - y1);
        return Rect();
    }
    
    bool empty() const { return width <= 0 || height <= 0; }
    bool contains(int px, int py) const {
        return px >= x && px < x + width && py >= y && py < y + height;
    }
};

struct Color {
    BYTE r, g, b, a;
    
    Color() : r(0), g(0), b(0), a(255) {}
    Color(BYTE r, BYTE g, BYTE b, BYTE a = 255) : r(r), g(g), b(b), a(a) {}
    Color(COLORREF c, BYTE alpha = 255) 
        : r(GetRValue(c)), g(GetGValue(c)), b(GetBValue(c)), a(alpha) {}
    
    COLORREF toRGB() const { return RGB(r, g, b); }
    
    // 预乘alpha
    Color premultiplied() const {
        return Color(
            (BYTE)(r * a / 255),
            (BYTE)(g * a / 255),
            (BYTE)(b * a / 255),
            a
        );
    }
};

// ============================================================================
// 绘制命令
// ============================================================================

enum class CommandType : BYTE {
    None = 0,
    FillRect,
    FillRectAlpha,
    DrawText,
    DrawLine,
    DrawRoundRect,
    DrawRoundRectAlpha,
    DrawBitmap,
    ClipPush,
    ClipPop,
    SetTransform,
};

struct DrawCommand {
    CommandType type = CommandType::None;
    Rect bounds;              // 命令影响的区域
    int zOrder = 0;           // Z序
    bool isOpaque = false;    // 是否不透明
    
    // 命令数据（使用union节省内存）
    union {
        struct { Color color; } fillRect;
        struct { Color color; int radius; } roundRect;
        struct { const wchar_t* text; int len; Color color; HFONT font; int flags; } text;
        struct { Color color; int width; } line;
        struct { HBITMAP bitmap; int srcX, srcY; } bitmap;
        struct { Rect clipRect; } clip;
    };
    
    DrawCommand() { memset(this, 0, sizeof(*this)); }
};

// ============================================================================
// 命令队列
// ============================================================================

class CommandQueue {
public:
    void clear() {
        commands_.clear();
        hasTranslucent_ = false;
    }
    
    void addFillRect(const Rect& bounds, Color color, int zOrder = 0) {
        DrawCommand cmd;
        cmd.type = CommandType::FillRect;
        cmd.bounds = bounds;
        cmd.zOrder = zOrder;
        cmd.isOpaque = (color.a == 255);
        cmd.fillRect.color = color;
        commands_.push_back(cmd);
    }
    
    void addFillRectAlpha(const Rect& bounds, Color color, int zOrder = 0) {
        DrawCommand cmd;
        cmd.type = CommandType::FillRectAlpha;
        cmd.bounds = bounds;
        cmd.zOrder = zOrder;
        cmd.isOpaque = false;
        cmd.fillRect.color = color;
        commands_.push_back(cmd);
        hasTranslucent_ = true;
    }
    
    void addRoundRect(const Rect& bounds, Color color, int radius, int zOrder = 0) {
        DrawCommand cmd;
        cmd.type = CommandType::DrawRoundRect;
        cmd.bounds = bounds;
        cmd.zOrder = zOrder;
        cmd.isOpaque = (color.a == 255);
        cmd.roundRect.color = color;
        cmd.roundRect.radius = radius;
        commands_.push_back(cmd);
    }
    
    void addRoundRectAlpha(const Rect& bounds, Color color, int radius, int zOrder = 0) {
        DrawCommand cmd;
        cmd.type = CommandType::DrawRoundRectAlpha;
        cmd.bounds = bounds;
        cmd.zOrder = zOrder;
        cmd.isOpaque = false;
        cmd.roundRect.color = color;
        cmd.roundRect.radius = radius;
        commands_.push_back(cmd);
        hasTranslucent_ = true;
    }
    
    void addText(const Rect& bounds, const wchar_t* text, int len, 
                 Color color, HFONT font, int flags, int zOrder = 0) {
        DrawCommand cmd;
        cmd.type = CommandType::DrawText;
        cmd.bounds = bounds;
        cmd.zOrder = zOrder;
        cmd.isOpaque = false;
        cmd.text.text = text;
        cmd.text.len = len;
        cmd.text.color = color;
        cmd.text.font = font;
        cmd.text.flags = flags;
        commands_.push_back(cmd);
    }
    
    void addBitmap(const Rect& bounds, HBITMAP bitmap, int srcX, int srcY, int zOrder = 0) {
        DrawCommand cmd;
        cmd.type = CommandType::DrawBitmap;
        cmd.bounds = bounds;
        cmd.zOrder = zOrder;
        cmd.isOpaque = true;
        cmd.bitmap.bitmap = bitmap;
        cmd.bitmap.srcX = srcX;
        cmd.bitmap.srcY = srcY;
        commands_.push_back(cmd);
    }
    
    void addClipPush(const Rect& clipRect) {
        DrawCommand cmd;
        cmd.type = CommandType::ClipPush;
        cmd.bounds = clipRect;
        cmd.clip.clipRect = clipRect;
        commands_.push_back(cmd);
    }
    
    void addClipPop() {
        DrawCommand cmd;
        cmd.type = CommandType::ClipPop;
        commands_.push_back(cmd);
    }
    
    // 排序命令（按Z序，不透明的先绘制）
    void sort() {
        if (commands_.empty()) return;
        
        // 稳定排序，保持相同Z序的绘制顺序
        std::stable_sort(commands_.begin(), commands_.end(),
            [](const DrawCommand& a, const DrawCommand& b) {
                // 不透明的排在前面
                if (a.isOpaque != b.isOpaque)
                    return a.isOpaque;
                return a.zOrder < b.zOrder;
            });
    }
    
    // 批处理优化：合并相邻的相同类型命令
    void batch() {
        if (commands_.size() < 2) return;
        
        std::vector<DrawCommand> optimized;
        optimized.reserve(commands_.size());
        
        for (auto& cmd : commands_) {
            if (optimized.empty()) {
                optimized.push_back(cmd);
                continue;
            }
            
            auto& last = optimized.back();
            
            // 尝试合并相同颜色的FillRect
            if (last.type == CommandType::FillRect && 
                cmd.type == CommandType::FillRect &&
                last.fillRect.color.toRGB() == cmd.fillRect.color.toRGB() &&
                last.fillRect.color.a == cmd.fillRect.color.a) {
                // 扩展bounds包含两个矩形
                int x1 = min(last.bounds.x, cmd.bounds.x);
                int y1 = min(last.bounds.y, cmd.bounds.y);
                int x2 = max(last.bounds.x + last.bounds.width, cmd.bounds.x + cmd.bounds.width);
                int y2 = max(last.bounds.y + last.bounds.height, cmd.bounds.y + cmd.bounds.height);
                last.bounds = Rect(x1, y1, x2 - x1, y2 - y1);
            } else {
                optimized.push_back(cmd);
            }
        }
        
        commands_ = std::move(optimized);
    }
    
    const std::vector<DrawCommand>& commands() const { return commands_; }
    bool hasTranslucent() const { return hasTranslucent_; }
    
private:
    std::vector<DrawCommand> commands_;
    bool hasTranslucent_ = false;
};

// ============================================================================
// 图层 - 每个图层有独立的缓存位图
// ============================================================================

class Layer {
public:
    using PaintFunc = std::function<void(CommandQueue&, const Rect& dirtyRect)>;
    
    Layer(int id, const Rect& bounds);
    ~Layer();
    
    int id() const { return id_; }
    const Rect& bounds() const { return bounds_; }
    void setBounds(const Rect& bounds);
    
    bool visible() const { return visible_; }
    void setVisible(bool v) { visible_ = v; }
    
    bool dirty() const { return dirty_; }
    void markDirty() { dirty_ = true; dirtyBounds_ = bounds_; }
    void markDirty(const Rect& area);
    void clearDirty() { dirty_ = false; dirtyBounds_ = Rect(); }
    const Rect& dirtyBounds() const { return dirtyBounds_; }
    
    bool opaque() const { return opaque_; }
    void setOpaque(bool o) { opaque_ = o; }
    
    int zOrder() const { return zOrder_; }
    void setZOrder(int z) { zOrder_ = z; }
    
    void setPaintFunc(PaintFunc func) { paintFunc_ = func; }
    
    // 渲染图层到缓存，返回是否需要更新
    bool updateCache(HDC targetDC);
    
    // 从缓存绘制到目标DC
    void blit(HDC targetDC, const Rect& targetRect, const Rect& srcRect);
    
    // 直接绘制（不使用缓存）
    void directPaint(HDC targetDC, const Rect& clipRect);
    
    // 获取缓存位图
    HBITMAP cacheBitmap() const { return cacheBitmap_; }
    HDC cacheDC() const { return cacheDC_; }
    
private:
    int id_;
    Rect bounds_;
    Rect dirtyBounds_;
    bool visible_ = true;
    bool dirty_ = true;
    bool opaque_ = false;
    int zOrder_ = 0;
    
    HDC cacheDC_ = nullptr;
    HBITMAP cacheBitmap_ = nullptr;
    HBITMAP oldBitmap_ = nullptr;
    
    PaintFunc paintFunc_;
    
    void createCache(int width, int height);
    void destroyCache();
};

// ============================================================================
// 脏区域管理器
// ============================================================================

class DirtyRegionManager {
public:
    void clear() { regions_.clear(); }
    
    void add(const Rect& rect) {
        if (rect.empty()) return;
        
        // 尝试与现有区域合并
        for (auto& r : regions_) {
            if (r.intersects(rect)) {
                // 合并两个区域
                int x1 = min(r.x, rect.x);
                int y1 = min(r.y, rect.y);
                int x2 = max(r.x + r.width, rect.x + rect.width);
                int y2 = max(r.y + r.height, rect.y + rect.height);
                r = Rect(x1, y1, x2 - x1, y2 - y1);
                return;
            }
        }
        
        // 没有可合并的，添加新区域
        regions_.push_back(rect);
        
        // 限制区域数量，太多时合并所有
        if (regions_.size() > 8) {
            mergeAll();
        }
    }
    
    const std::vector<Rect>& regions() const { return regions_; }
    
    void mergeAll() {
        if (regions_.empty()) return;
        
        int x1 = regions_[0].x, y1 = regions_[0].y;
        int x2 = x1 + regions_[0].width, y2 = y1 + regions_[0].height;
        
        for (size_t i = 1; i < regions_.size(); i++) {
            x1 = min(x1, regions_[i].x);
            y1 = min(y1, regions_[i].y);
            x2 = max(x2, regions_[i].x + regions_[i].width);
            y2 = max(y2, regions_[i].y + regions_[i].height);
        }
        
        regions_.clear();
        regions_.push_back(Rect(x1, y1, x2 - x1, y2 - y1));
    }
    
    // 获取合并后的边界
    Rect bounds() const {
        if (regions_.empty()) return Rect();
        
        int x1 = regions_[0].x, y1 = regions_[0].y;
        int x2 = x1 + regions_[0].width, y2 = y1 + regions_[0].height;
        
        for (const auto& r : regions_) {
            x1 = min(x1, r.x);
            y1 = min(y1, r.y);
            x2 = max(x2, r.x + r.width);
            y2 = max(y2, r.y + r.height);
        }
        
        return Rect(x1, y1, x2 - x1, y2 - y1);
    }
    
private:
    std::vector<Rect> regions_;
};

// ============================================================================
// 场景图节点
// ============================================================================

class SceneNode {
public:
    virtual ~SceneNode() = default;
    
    const Rect& bounds() const { return bounds_; }
    void setBounds(const Rect& b) { bounds_ = b; markDirty(); }
    
    bool visible() const { return visible_; }
    void setVisible(bool v) { if (visible_ != v) { visible_ = v; markDirty(); } }
    
    bool dirty() const { return dirty_; }
    void markDirty();
    void clearDirty() { dirty_ = false; }
    
    int zOrder() const { return zOrder_; }
    void setZOrder(int z);
    
    SceneNode* parent() const { return parent_; }
    void setParent(SceneNode* p) { parent_ = p; }
    
    const std::vector<std::unique_ptr<SceneNode>>& children() const { return children_; }
    void addChild(std::unique_ptr<SceneNode> child);
    void removeChild(SceneNode* child);
    
    virtual void paint(CommandQueue& queue, const Rect& clipRect) = 0;
    virtual bool hitTest(int x, int y) const { return bounds_.contains(x, y); }
    
protected:
    Rect bounds_;
    bool visible_ = true;
    bool dirty_ = true;
    int zOrder_ = 0;
    SceneNode* parent_ = nullptr;
    std::vector<std::unique_ptr<SceneNode>> children_;
};

// ============================================================================
// 渲染后端接口
// ============================================================================

class RenderBackend {
public:
    virtual ~RenderBackend() = default;
    
    virtual bool initialize(HWND hwnd, int width, int height) = 0;
    virtual void resize(int width, int height) = 0;
    virtual void beginFrame() = 0;
    virtual void endFrame() = 0;
    virtual void present() = 0;
    
    virtual void execute(const CommandQueue& queue, const Rect& clipRect) = 0;
    
    virtual HDC getDC() = 0;
    virtual void releaseDC() = 0;
    
    virtual bool hasHardwareAcceleration() const = 0;
    virtual const wchar_t* name() const = 0;
};

// ============================================================================
// GDI+ 渲染后端
// ============================================================================

class GDIBackend : public RenderBackend {
public:
    ~GDIBackend() override;
    
    bool initialize(HWND hwnd, int width, int height) override;
    void resize(int width, int height) override;
    void beginFrame() override;
    void endFrame() override;
    void present() override;
    
    void execute(const CommandQueue& queue, const Rect& clipRect) override;
    
    HDC getDC() override { return memDC_; }
    void releaseDC() override {}
    
    bool hasHardwareAcceleration() const override { return false; }
    const wchar_t* name() const override { return L"GDI+"; }
    
private:
    HWND hwnd_ = nullptr;
    HDC memDC_ = nullptr;
    HBITMAP memBitmap_ = nullptr;
    HBITMAP oldBitmap_ = nullptr;
    int width_ = 0, height_ = 0;
    
    ULONG gdiplusToken_ = 0;
    Gdiplus::Graphics* graphics_ = nullptr;
    
    void createBuffer(int width, int height);
    void destroyBuffer();
    void executeCommand(const DrawCommand& cmd, const Rect& clipRect);
};

// ============================================================================
// 渲染器 - 核心类
// ============================================================================

class Renderer {
public:
    Renderer();
    ~Renderer();
    
    // 初始化
    bool initialize(HWND hwnd, int width, int height);
    void resize(int width, int height);
    
    // 图层管理
    Layer* createLayer(int id, const Rect& bounds);
    Layer* getLayer(int id) const;
    void destroyLayer(int id);
    void destroyAllLayers();
    
    // 渲染
    void beginFrame();
    void render(const Rect& dirtyRect);
    void endFrame();
    void present();
    
    // 脏区域
    void markDirty(const Rect& rect);
    void markLayerDirty(int layerId);
    void markAllDirty();
    
    // 后端
    RenderBackend* backend() const { return backend_.get(); }
    void setBackend(std::unique_ptr<RenderBackend> b);
    
    // 统计
    struct Stats {
        int frameCount = 0;
        int totalCommands = 0;
        int dirtyPixels = 0;
        int cachedPixels = 0;
        double avgFrameTime = 0;
    };
    const Stats& stats() const { return stats_; }
    void resetStats();
    
private:
    HWND hwnd_ = nullptr;
    std::unique_ptr<RenderBackend> backend_;
    std::unordered_map<int, std::unique_ptr<Layer>> layers_;
    DirtyRegionManager dirtyRegions_;
    Stats stats_;
    int width_ = 0, height_ = 0;
    
    void updateDirtyLayers();
    void renderLayers(const Rect& clipRect);
};

// ============================================================================
// 辅助绘制函数
// ============================================================================

namespace DrawHelper {
    // 绘制圆角矩形（GDI）
    void FillRoundRect(HDC hdc, const Rect& rect, int radius, COLORREF color);
    void FillRoundRectAlpha(HDC hdc, const Rect& rect, int radius, COLORREF color, BYTE alpha);
    
    // 绘制渐变
    void FillGradientRect(HDC hdc, const Rect& rect, COLORREF color1, COLORREF color2, bool vertical);
    void FillGradientRoundRect(HDC hdc, const Rect& rect, int radius, COLORREF color1, COLORREF color2, bool vertical);
    
    // 绘制阴影
    void DrawShadow(HDC hdc, const Rect& rect, int radius, int shadowSize, BYTE alpha);
    
    // 绘制毛玻璃效果
    void DrawGlassEffect(HDC hdc, const Rect& rect, int radius, Color baseColor, float blur = 0.5f);
    
    // 文字绘制
    void DrawText(HDC hdc, const wchar_t* text, int len, const Rect& bounds, 
                  COLORREF color, HFONT font, int flags);
    SIZE MeasureText(HDC hdc, const wchar_t* text, int len, HFONT font);
}

} // namespace RenderPipeline
