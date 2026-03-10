/**
 * Render Pipeline Implementation
 */

#include "render_pipeline.h"
#include <algorithm>
#include <cmath>

namespace RenderPipeline {

// ============================================================================
// Layer Implementation
// ============================================================================

Layer::Layer(int id, const Rect& bounds) 
    : id_(id), bounds_(bounds), dirtyBounds_(bounds) {
}

Layer::~Layer() {
    destroyCache();
}

void Layer::setBounds(const Rect& bounds) {
    if (bounds_.width != bounds.width || bounds_.height != bounds.height) {
        // 尺寸改变，需要重建缓存
        destroyCache();
        dirty_ = true;
    }
    bounds_ = bounds;
    dirtyBounds_ = bounds;
}

void Layer::markDirty(const Rect& area) {
    dirty_ = true;
    if (dirtyBounds_.empty()) {
        dirtyBounds_ = area;
    } else {
        int x1 = min(dirtyBounds_.x, area.x);
        int y1 = min(dirtyBounds_.y, area.y);
        int x2 = max(dirtyBounds_.x + dirtyBounds_.width, area.x + area.width);
        int y2 = max(dirtyBounds_.y + dirtyBounds_.height, area.y + area.height);
        dirtyBounds_ = Rect(x1, y1, x2 - x1, y2 - y1);
    }
}

void Layer::createCache(int width, int height) {
    if (width <= 0 || height <= 0) return;
    
    HDC screenDC = GetDC(nullptr);
    cacheDC_ = CreateCompatibleDC(screenDC);
    cacheBitmap_ = CreateCompatibleBitmap(screenDC, width, height);
    oldBitmap_ = (HBITMAP)SelectObject(cacheDC_, cacheBitmap_);
    ReleaseDC(nullptr, screenDC);
}

void Layer::destroyCache() {
    if (cacheDC_) {
        SelectObject(cacheDC_, oldBitmap_);
        DeleteObject(cacheBitmap_);
        DeleteDC(cacheDC_);
        cacheDC_ = nullptr;
        cacheBitmap_ = nullptr;
        oldBitmap_ = nullptr;
    }
}

bool Layer::updateCache(HDC targetDC) {
    if (!dirty_ || !paintFunc_) return false;
    
    // 确保缓存存在
    if (!cacheDC_) {
        createCache(bounds_.width, bounds_.height);
    }
    
    if (!cacheDC_) return false;
    
    // 清空缓存区域
    RECT r = {0, 0, bounds_.width, bounds_.height};
    FillRect(cacheDC_, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
    
    // 绘制到缓存
    CommandQueue queue;
    paintFunc_(queue, dirtyBounds_);
    
    // 执行命令
    Gdiplus::Graphics graphics(cacheDC_);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
    
    for (const auto& cmd : queue.commands()) {
        // 转换坐标（相对于图层）
        Rect localBounds = cmd.bounds;
        localBounds.x -= bounds_.x;
        localBounds.y -= bounds_.y;
        
        switch (cmd.type) {
            case CommandType::FillRect: {
                Gdiplus::SolidBrush brush(Gdiplus::Color(
                    cmd.fillRect.color.a,
                    cmd.fillRect.color.r,
                    cmd.fillRect.color.g,
                    cmd.fillRect.color.b
                ));
                graphics.FillRectangle(&brush, 
                    localBounds.x, localBounds.y, 
                    localBounds.width, localBounds.height);
                break;
            }
            case CommandType::FillRectAlpha: {
                Gdiplus::SolidBrush brush(Gdiplus::Color(
                    cmd.fillRect.color.a,
                    cmd.fillRect.color.r,
                    cmd.fillRect.color.g,
                    cmd.fillRect.color.b
                ));
                graphics.FillRectangle(&brush,
                    localBounds.x, localBounds.y,
                    localBounds.width, localBounds.height);
                break;
            }
            case CommandType::DrawRoundRect:
            case CommandType::DrawRoundRectAlpha: {
                int radius = cmd.roundRect.radius;
                Gdiplus::GraphicsPath path;
                int x = localBounds.x, y = localBounds.y;
                int w = localBounds.width, h = localBounds.height;
                
                path.AddArc(x, y, radius * 2, radius * 2, 180, 90);
                path.AddArc(x + w - radius * 2, y, radius * 2, radius * 2, 270, 90);
                path.AddArc(x + w - radius * 2, y + h - radius * 2, radius * 2, radius * 2, 0, 90);
                path.AddArc(x, y + h - radius * 2, radius * 2, radius * 2, 90, 90);
                path.CloseFigure();
                
                Gdiplus::SolidBrush brush(Gdiplus::Color(
                    cmd.roundRect.color.a,
                    cmd.roundRect.color.r,
                    cmd.roundRect.color.g,
                    cmd.roundRect.color.b
                ));
                graphics.FillPath(&brush, &path);
                break;
            }
            case CommandType::DrawText: {
                Gdiplus::Font font(cacheDC_, cmd.text.font);
                Gdiplus::SolidBrush brush(Gdiplus::Color(
                    cmd.text.color.a,
                    cmd.text.color.r,
                    cmd.text.color.g,
                    cmd.text.color.b
                ));
                Gdiplus::StringFormat format;
                format.SetAlignment(Gdiplus::StringAlignmentNear);
                format.SetLineAlignment(Gdiplus::StringAlignmentNear);
                
                Gdiplus::RectF rectF(
                    (Gdiplus::REAL)localBounds.x,
                    (Gdiplus::REAL)localBounds.y,
                    (Gdiplus::REAL)localBounds.width,
                    (Gdiplus::REAL)localBounds.height
                );
                
                graphics.DrawString(
                    cmd.text.text, cmd.text.len, &font, rectF, &format, &brush
                );
                break;
            }
            default:
                break;
        }
    }
    
    dirty_ = false;
    return true;
}

void Layer::blit(HDC targetDC, const Rect& targetRect, const Rect& srcRect) {
    if (!cacheDC_) return;
    
    BitBlt(targetDC, 
           targetRect.x, targetRect.y, 
           targetRect.width, targetRect.height,
           cacheDC_, 
           srcRect.x - bounds_.x, srcRect.y - bounds_.y,
           SRCCOPY);
}

void Layer::directPaint(HDC targetDC, const Rect& clipRect) {
    if (!paintFunc_) return;
    
    CommandQueue queue;
    paintFunc_(queue, clipRect);
    
    Gdiplus::Graphics graphics(targetDC);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    
    for (const auto& cmd : queue.commands()) {
        // ... 类似 updateCache 的绘制逻辑
    }
}

// ============================================================================
// SceneNode Implementation
// ============================================================================

void SceneNode::markDirty() {
    dirty_ = true;
    // 传播到父节点
    if (parent_) {
        parent_->markDirty();
    }
}

void SceneNode::setZOrder(int z) {
    if (zOrder_ != z) {
        zOrder_ = z;
        markDirty();
    }
}

void SceneNode::addChild(std::unique_ptr<SceneNode> child) {
    child->setParent(this);
    children_.push_back(std::move(child));
    markDirty();
}

void SceneNode::removeChild(SceneNode* child) {
    children_.erase(
        std::remove_if(children_.begin(), children_.end(),
            [child](const std::unique_ptr<SceneNode>& c) { return c.get() == child; }),
        children_.end()
    );
    markDirty();
}

// ============================================================================
// GDIBackend Implementation
// ============================================================================

GDIBackend::~GDIBackend() {
    destroyBuffer();
    if (gdiplusToken_) {
        Gdiplus::GdiplusShutdown(gdiplusToken_);
    }
}

bool GDIBackend::initialize(HWND hwnd, int width, int height) {
    hwnd_ = hwnd;
    width_ = width;
    height_ = height;
    
    // 初始化 GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&gdiplusToken_, &gdiplusStartupInput, nullptr);
    
    createBuffer(width, height);
    return memDC_ != nullptr;
}

void GDIBackend::resize(int width, int height) {
    width_ = width;
    height_ = height;
    destroyBuffer();
    createBuffer(width, height);
}

void GDIBackend::createBuffer(int width, int height) {
    HDC screenDC = GetDC(hwnd_);
    memDC_ = CreateCompatibleDC(screenDC);
    memBitmap_ = CreateCompatibleBitmap(screenDC, width, height);
    oldBitmap_ = (HBITMAP)SelectObject(memDC_, memBitmap_);
    ReleaseDC(hwnd_, screenDC);
    
    graphics_ = new Gdiplus::Graphics(memDC_);
    graphics_->SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics_->SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
}

void GDIBackend::destroyBuffer() {
    delete graphics_;
    graphics_ = nullptr;
    
    if (memDC_) {
        SelectObject(memDC_, oldBitmap_);
        DeleteObject(memBitmap_);
        DeleteDC(memDC_);
        memDC_ = nullptr;
        memBitmap_ = nullptr;
        oldBitmap_ = nullptr;
    }
}

void GDIBackend::beginFrame() {
    // 清空缓冲区
    RECT r = {0, 0, width_, height_};
    FillRect(memDC_, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
}

void GDIBackend::endFrame() {
    // 可以在这里做一些后期处理
}

void GDIBackend::present() {
    HDC hdc = GetDC(hwnd_);
    BitBlt(hdc, 0, 0, width_, height_, memDC_, 0, 0, SRCCOPY);
    ReleaseDC(hwnd_, hdc);
}

void GDIBackend::execute(const CommandQueue& queue, const Rect& clipRect) {
    // 设置裁剪区域
    HRGN clipRgn = CreateRectRgn(clipRect.x, clipRect.y, 
                                  clipRect.x + clipRect.width, 
                                  clipRect.y + clipRect.height);
    SelectClipRgn(memDC_, clipRgn);
    DeleteObject(clipRgn);
    
    for (const auto& cmd : queue.commands()) {
        executeCommand(cmd, clipRect);
    }
    
    // 取消裁剪
    SelectClipRgn(memDC_, nullptr);
}

void GDIBackend::executeCommand(const DrawCommand& cmd, const Rect& clipRect) {
    switch (cmd.type) {
        case CommandType::FillRect: {
            HBRUSH brush = CreateSolidBrush(cmd.fillRect.color.toRGB());
            RECT r = {cmd.bounds.x, cmd.bounds.y, 
                     cmd.bounds.x + cmd.bounds.width, 
                     cmd.bounds.y + cmd.bounds.height};
            FillRect(memDC_, &r, brush);
            DeleteObject(brush);
            break;
        }
        
        case CommandType::FillRectAlpha: {
            Gdiplus::SolidBrush brush(Gdiplus::Color(
                cmd.fillRect.color.a,
                cmd.fillRect.color.r,
                cmd.fillRect.color.g,
                cmd.fillRect.color.b
            ));
            graphics_->FillRectangle(&brush,
                cmd.bounds.x, cmd.bounds.y,
                cmd.bounds.width, cmd.bounds.height);
            break;
        }
        
        case CommandType::DrawRoundRect:
        case CommandType::DrawRoundRectAlpha: {
            int radius = cmd.roundRect.radius;
            int x = cmd.bounds.x, y = cmd.bounds.y;
            int w = cmd.bounds.width, h = cmd.bounds.height;
            
            Gdiplus::GraphicsPath path;
            path.AddArc(x, y, radius * 2, radius * 2, 180, 90);
            path.AddArc(x + w - radius * 2, y, radius * 2, radius * 2, 270, 90);
            path.AddArc(x + w - radius * 2, y + h - radius * 2, radius * 2, radius * 2, 0, 90);
            path.AddArc(x, y + h - radius * 2, radius * 2, radius * 2, 90, 90);
            path.CloseFigure();
            
            Gdiplus::SolidBrush brush(Gdiplus::Color(
                cmd.roundRect.color.a,
                cmd.roundRect.color.r,
                cmd.roundRect.color.g,
                cmd.roundRect.color.b
            ));
            graphics_->FillPath(&brush, &path);
            break;
        }
        
        case CommandType::DrawText: {
            Gdiplus::Font font(memDC_, cmd.text.font);
            Gdiplus::SolidBrush brush(Gdiplus::Color(
                cmd.text.color.a,
                cmd.text.color.r,
                cmd.text.color.g,
                cmd.text.color.b
            ));
            
            Gdiplus::StringFormat format;
            format.SetAlignment(Gdiplus::StringAlignmentNear);
            format.SetLineAlignment(Gdiplus::StringAlignmentNear);
            
            Gdiplus::RectF rectF(
                (Gdiplus::REAL)cmd.bounds.x,
                (Gdiplus::REAL)cmd.bounds.y,
                (Gdiplus::REAL)cmd.bounds.width,
                (Gdiplus::REAL)cmd.bounds.height
            );
            
            graphics_->DrawString(cmd.text.text, cmd.text.len, &font, rectF, &format, &brush);
            break;
        }
        
        case CommandType::DrawBitmap: {
            HDC srcDC = CreateCompatibleDC(memDC_);
            HBITMAP oldBmp = (HBITMAP)SelectObject(srcDC, cmd.bitmap.bitmap);
            
            BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, 0};
            AlphaBlend(memDC_, 
                      cmd.bounds.x, cmd.bounds.y, cmd.bounds.width, cmd.bounds.height,
                      srcDC, cmd.bitmap.srcX, cmd.bitmap.srcY, cmd.bounds.width, cmd.bounds.height,
                      bf);
            
            SelectObject(srcDC, oldBmp);
            DeleteDC(srcDC);
            break;
        }
        
        case CommandType::ClipPush: {
            // 支持嵌套裁剪
            HRGN rgn = CreateRectRgn(
                cmd.clip.clipRect.x, cmd.clip.clipRect.y,
                cmd.clip.clipRect.x + cmd.clip.clipRect.width,
                cmd.clip.clipRect.y + cmd.clip.clipRect.height
            );
            SelectClipRgn(memDC_, rgn);
            DeleteObject(rgn);
            break;
        }
        
        case CommandType::ClipPop: {
            SelectClipRgn(memDC_, nullptr);
            break;
        }
        
        default:
            break;
    }
}

// ============================================================================
// Renderer Implementation
// ============================================================================

Renderer::Renderer() {
    backend_ = std::make_unique<GDIBackend>();
}

Renderer::~Renderer() {
    destroyAllLayers();
}

bool Renderer::initialize(HWND hwnd, int width, int height) {
    hwnd_ = hwnd;
    width_ = width;
    height_ = height;
    return backend_->initialize(hwnd, width, height);
}

void Renderer::resize(int width, int height) {
    width_ = width;
    height_ = height;
    backend_->resize(width, height);
    markAllDirty();
}

Layer* Renderer::createLayer(int id, const Rect& bounds) {
    auto layer = std::make_unique<Layer>(id, bounds);
    Layer* ptr = layer.get();
    layers_[id] = std::move(layer);
    return ptr;
}

Layer* Renderer::getLayer(int id) const {
    auto it = layers_.find(id);
    return it != layers_.end() ? it->second.get() : nullptr;
}

void Renderer::destroyLayer(int id) {
    layers_.erase(id);
}

void Renderer::destroyAllLayers() {
    layers_.clear();
}

void Renderer::beginFrame() {
    backend_->beginFrame();
    stats_.frameCount++;
}

void Renderer::render(const Rect& dirtyRect) {
    updateDirtyLayers();
    renderLayers(dirtyRect);
}

void Renderer::endFrame() {
    backend_->endFrame();
}

void Renderer::present() {
    backend_->present();
}

void Renderer::markDirty(const Rect& rect) {
    dirtyRegions_.add(rect);
}

void Renderer::markLayerDirty(int layerId) {
    Layer* layer = getLayer(layerId);
    if (layer) {
        layer->markDirty();
        dirtyRegions_.add(layer->bounds());
    }
}

void Renderer::markAllDirty() {
    dirtyRegions_.clear();
    dirtyRegions_.add(Rect(0, 0, width_, height_));
    for (auto& [id, layer] : layers_) {
        layer->markDirty();
    }
}

void Renderer::setBackend(std::unique_ptr<RenderBackend> b) {
    backend_ = std::move(b);
    if (hwnd_) {
        backend_->initialize(hwnd_, width_, height_);
    }
}

void Renderer::updateDirtyLayers() {
    for (auto& [id, layer] : layers_) {
        if (layer->dirty() && layer->visible()) {
            layer->updateCache(backend_->getDC());
        }
    }
}

void Renderer::renderLayers(const Rect& clipRect) {
    // 收集所有可见图层并按Z序排序
    std::vector<Layer*> sortedLayers;
    for (auto& [id, layer] : layers_) {
        if (layer->visible()) {
            sortedLayers.push_back(layer.get());
        }
    }
    
    std::sort(sortedLayers.begin(), sortedLayers.end(),
        [](Layer* a, Layer* b) { return a->zOrder() < b->zOrder(); });
    
    // 绘制每个图层
    for (Layer* layer : sortedLayers) {
        Rect layerRect = layer->bounds();
        Rect drawRect = layerRect.intersect(clipRect);
        
        if (!drawRect.empty()) {
            layer->blit(backend_->getDC(), drawRect, drawRect);
        }
    }
}

void Renderer::resetStats() {
    stats_ = Stats{};
}

// ============================================================================
// DrawHelper Implementation
// ============================================================================

namespace DrawHelper {

void FillRoundRect(HDC hdc, const Rect& rect, int radius, COLORREF color) {
    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    
    int x = rect.x, y = rect.y, w = rect.width, h = rect.height;
    
    Gdiplus::GraphicsPath path;
    path.AddArc(x, y, radius * 2, radius * 2, 180, 90);
    path.AddArc(x + w - radius * 2, y, radius * 2, radius * 2, 270, 90);
    path.AddArc(x + w - radius * 2, y + h - radius * 2, radius * 2, radius * 2, 0, 90);
    path.AddArc(x, y + h - radius * 2, radius * 2, radius * 2, 90, 90);
    path.CloseFigure();
    
    Gdiplus::SolidBrush brush(Gdiplus::Color(GetRValue(color), GetGValue(color), GetBValue(color)));
    graphics.FillPath(&brush, &path);
}

void FillRoundRectAlpha(HDC hdc, const Rect& rect, int radius, COLORREF color, BYTE alpha) {
    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    
    int x = rect.x, y = rect.y, w = rect.width, h = rect.height;
    
    Gdiplus::GraphicsPath path;
    path.AddArc(x, y, radius * 2, radius * 2, 180, 90);
    path.AddArc(x + w - radius * 2, y, radius * 2, radius * 2, 270, 90);
    path.AddArc(x + w - radius * 2, y + h - radius * 2, radius * 2, radius * 2, 0, 90);
    path.AddArc(x, y + h - radius * 2, radius * 2, radius * 2, 90, 90);
    path.CloseFigure();
    
    Gdiplus::SolidBrush brush(Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color)));
    graphics.FillPath(&brush, &path);
}

void FillGradientRect(HDC hdc, const Rect& rect, COLORREF color1, COLORREF color2, bool vertical) {
    Gdiplus::Graphics graphics(hdc);
    
    Gdiplus::LinearGradientBrush brush(
        vertical ? Gdiplus::Rect(rect.x, rect.y, rect.width, rect.height)
                 : Gdiplus::Rect(rect.x, rect.y, rect.width, rect.height),
        Gdiplus::Color(GetRValue(color1), GetGValue(color1), GetBValue(color1)),
        Gdiplus::Color(GetRValue(color2), GetGValue(color2), GetBValue(color2)),
        vertical ? Gdiplus::LinearGradientModeVertical : Gdiplus::LinearGradientModeHorizontal
    );
    
    graphics.FillRectangle(&brush, rect.x, rect.y, rect.width, rect.height);
}

void FillGradientRoundRect(HDC hdc, const Rect& rect, int radius, COLORREF color1, COLORREF color2, bool vertical) {
    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    
    int x = rect.x, y = rect.y, w = rect.width, h = rect.height;
    
    Gdiplus::GraphicsPath path;
    path.AddArc(x, y, radius * 2, radius * 2, 180, 90);
    path.AddArc(x + w - radius * 2, y, radius * 2, radius * 2, 270, 90);
    path.AddArc(x + w - radius * 2, y + h - radius * 2, radius * 2, radius * 2, 0, 90);
    path.AddArc(x, y + h - radius * 2, radius * 2, radius * 2, 90, 90);
    path.CloseFigure();
    
    Gdiplus::LinearGradientBrush brush(
        Gdiplus::Rect(x, y, w, h),
        Gdiplus::Color(GetRValue(color1), GetGValue(color1), GetBValue(color1)),
        Gdiplus::Color(GetRValue(color2), GetGValue(color2), GetBValue(color2)),
        vertical ? Gdiplus::LinearGradientModeVertical : Gdiplus::LinearGradientModeHorizontal
    );
    
    graphics.FillPath(&brush, &path);
}

void DrawShadow(HDC hdc, const Rect& rect, int radius, int shadowSize, BYTE alpha) {
    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    
    // 多层阴影
    for (int i = shadowSize; i > 0; i--) {
        BYTE a = (BYTE)(alpha * i / shadowSize / 2);
        int expand = shadowSize - i;
        
        Gdiplus::GraphicsPath path;
        int x = rect.x - expand;
        int y = rect.y - expand;
        int w = rect.width + expand * 2;
        int h = rect.height + expand * 2;
        
        path.AddArc(x, y, radius * 2, radius * 2, 180, 90);
        path.AddArc(x + w - radius * 2, y, radius * 2, radius * 2, 270, 90);
        path.AddArc(x + w - radius * 2, y + h - radius * 2, radius * 2, radius * 2, 0, 90);
        path.AddArc(x, y + h - radius * 2, radius * 2, radius * 2, 90, 90);
        path.CloseFigure();
        
        Gdiplus::SolidBrush brush(Gdiplus::Color(a, 0, 0, 0));
        graphics.FillPath(&brush, &path);
    }
}

void DrawGlassEffect(HDC hdc, const Rect& rect, int radius, Color baseColor, float blur) {
    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    
    int x = rect.x, y = rect.y, w = rect.width, h = rect.height;
    
    // 主背景
    Gdiplus::GraphicsPath path;
    path.AddArc(x, y, radius * 2, radius * 2, 180, 90);
    path.AddArc(x + w - radius * 2, y, radius * 2, radius * 2, 270, 90);
    path.AddArc(x + w - radius * 2, y + h - radius * 2, radius * 2, radius * 2, 0, 90);
    path.AddArc(x, y + h - radius * 2, radius * 2, radius * 2, 90, 90);
    path.CloseFigure();
    
    Gdiplus::SolidBrush brush(Gdiplus::Color(
        (BYTE)(baseColor.a * blur),
        baseColor.r, baseColor.g, baseColor.b
    ));
    graphics.FillPath(&brush, &path);
    
    // 顶部高光
    Gdiplus::GraphicsPath highlightPath;
    int highlightHeight = h / 3;
    highlightPath.AddArc(x, y, radius * 2, radius * 2, 180, 90);
    highlightPath.AddArc(x + w - radius * 2, y, radius * 2, radius * 2, 270, 90);
    highlightPath.AddLine(x + w - radius, y + highlightHeight, x + radius, y + highlightHeight);
    highlightPath.CloseFigure();
    
    Gdiplus::LinearGradientBrush highlightBrush(
        Gdiplus::Rect(x, y, w, highlightHeight),
        Gdiplus::Color(60, 255, 255, 255),
        Gdiplus::Color(0, 255, 255, 255),
        Gdiplus::LinearGradientModeVertical
    );
    graphics.FillPath(&highlightBrush, &highlightPath);
}

void DrawText(HDC hdc, const wchar_t* text, int len, const Rect& bounds, 
              COLORREF color, HFONT font, int flags) {
    HFONT oldFont = (HFONT)SelectObject(hdc, font);
    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);
    
    RECT r = {bounds.x, bounds.y, bounds.x + bounds.width, bounds.y + bounds.height};
    DrawTextW(hdc, text, len, &r, flags);
    
    SelectObject(hdc, oldFont);
}

SIZE MeasureText(HDC hdc, const wchar_t* text, int len, HFONT font) {
    HFONT oldFont = (HFONT)SelectObject(hdc, font);
    SIZE size;
    GetTextExtentPoint32W(hdc, text, len, &size);
    SelectObject(hdc, oldFont);
    return size;
}

} // namespace DrawHelper

} // namespace RenderPipeline
