/**
 * Nova Renderer - Font System Implementation
 */

#include "Nova/Font.h"
#include "Nova/Texture.h"

#ifdef NOVA_HAS_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

#include <algorithm>
#include <cmath>

namespace Nova {

// ============================================================================
// Font Implementation
// ============================================================================

Font::~Font() {
    destroy();
}

bool Font::create(const FontDesc& desc) {
#ifdef NOVA_HAS_FREETYPE
    pixelSize_ = desc.pixelSize;
    pxRange_ = desc.pxRange;
    useMSDF_ = desc.useMSDF;
    atlasSize_ = desc.atlasSize;
    
    // 初始化 FreeType
    FT_Library* library = new FT_Library;
    if (FT_Init_FreeType(library) != 0) {
        delete library;
        return false;
    }
    ftLibrary_ = library;
    
    // 加载字体
    FT_Face* face = new FT_Face;
    if (FT_New_Face(*library, desc.path.c_str(), 0, face) != 0) {
        FT_Done_FreeType(*library);
        delete library;
        delete face;
        ftLibrary_ = nullptr;
        return false;
    }
    ftFace_ = face;
    
    // 设置像素大小
    FT_Set_Pixel_Sizes(*face, 0, static_cast<FT_UInt>(pixelSize_));
    
    // 获取字体信息
    FT_Size_Metrics metrics = (*face)->size->metrics;
    lineHeight_ = metrics.height / 64.0f;
    ascent_ = metrics.ascender / 64.0f;
    descent_ = metrics.descender / 64.0f;
    
    // 创建图集数据
    atlasData_.resize(atlasSize_ * atlasSize_ * 4, 0);
    
    return true;
#else
    // FreeType not available - font rendering disabled
    (void)desc;
    return false;
#endif
}

void Font::destroy() {
#ifdef NOVA_HAS_FREETYPE
    if (ftFace_) {
        FT_Face* face = static_cast<FT_Face*>(ftFace_);
        FT_Done_Face(*face);
        delete face;
        ftFace_ = nullptr;
    }
    
    if (ftLibrary_) {
        FT_Library* library = static_cast<FT_Library*>(ftLibrary_);
        FT_Done_FreeType(*library);
        delete library;
        ftLibrary_ = nullptr;
    }
#endif
    
    glyphs_.clear();
    atlasData_.clear();
    atlasTexture_ = {};
}

const GlyphInfo* Font::getGlyph(u32 codepoint) const {
    auto it = glyphs_.find(codepoint);
    if (it != glyphs_.end()) {
        return &it->second;
    }
    
    // 尝试加载
    const_cast<Font*>(this)->loadGlyph(codepoint);
    
    it = glyphs_.find(codepoint);
    return it != glyphs_.end() ? &it->second : nullptr;
}

bool Font::loadGlyph(u32 codepoint) {
#ifdef NOVA_HAS_FREETYPE
    // 检查是否已加载
    if (glyphs_.find(codepoint) != glyphs_.end()) {
        return true;
    }
    
    FT_Face face = *static_cast<FT_Face*>(ftFace_);
    
    // 加载字形
    FT_UInt glyphIndex = FT_Get_Char_Index(face, codepoint);
    if (glyphIndex == 0) {
        return false;
    }
    
    if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_RENDER) != 0) {
        return false;
    }
    
    FT_GlyphSlot slot = face->glyph;
    
    u32 width = slot->bitmap.width;
    u32 height = slot->bitmap.rows;
    
    // 检查图集空间
    if (!hasSpace(width + 2, height + 2)) {
        return false;
    }
    
    // 分配图集空间
    u32 x = atlasX_;
    u32 y = atlasY_;
    
    atlasX_ += width + 2;
    atlasRowHeight_ = std::max(atlasRowHeight_, height + 2);
    
    if (atlasX_ >= atlasSize_) {
        atlasX_ = 0;
        atlasY_ += atlasRowHeight_;
        atlasRowHeight_ = height + 2;
        x = 0;
        y = atlasY_;
    }
    
    // 复制位图到图集
    for (u32 row = 0; row < height; row++) {
        for (u32 col = 0; col < width; col++) {
            u8 value = slot->bitmap.buffer[row * slot->bitmap.pitch + col];
            
            u64 dstIdx = ((y + row + 1) * atlasSize_ + (x + col + 1)) * 4;
            atlasData_[dstIdx] = value;
            atlasData_[dstIdx + 1] = value;
            atlasData_[dstIdx + 2] = value;
            atlasData_[dstIdx + 3] = value;
        }
    }
    
    // 创建字形信息
    GlyphInfo info;
    info.codepoint = codepoint;
    info.width = static_cast<f32>(width);
    info.height = static_cast<f32>(height);
    info.bearingX = static_cast<f32>(slot->bitmap_left);
    info.bearingY = static_cast<f32>(slot->bitmap_top);
    info.advance = static_cast<f32>(slot->advance.x >> 6);
    info.pxRange = pxRange_;
    
    // 计算纹理坐标
    info.u0 = static_cast<f32>(x + 1) / atlasSize_;
    info.v0 = static_cast<f32>(y + 1) / atlasSize_;
    info.u1 = static_cast<f32>(x + width + 1) / atlasSize_;
    info.v1 = static_cast<f32>(y + height + 1) / atlasSize_;
    
    glyphs_[codepoint] = info;
    return true;
#else
    (void)codepoint;
    return false;
#endif
}

void Font::precache(const std::vector<u32>& codepoints) {
    for (u32 cp : codepoints) {
        loadGlyph(cp);
    }
}

void Font::precache(const char* text) {
    const u8* p = reinterpret_cast<const u8*>(text);
    while (*p) {
        u32 codepoint = 0;
        
        // UTF-8 解码
        if ((*p & 0x80) == 0) {
            codepoint = *p++;
        } else if ((*p & 0xE0) == 0xC0) {
            codepoint = (*p++ & 0x1F) << 6;
            codepoint |= (*p++ & 0x3F);
        } else if ((*p & 0xF0) == 0xE0) {
            codepoint = (*p++ & 0x0F) << 12;
            codepoint |= (*p++ & 0x3F) << 6;
            codepoint |= (*p++ & 0x3F);
        } else if ((*p & 0xF8) == 0xF0) {
            codepoint = (*p++ & 0x07) << 18;
            codepoint |= (*p++ & 0x3F) << 12;
            codepoint |= (*p++ & 0x3F) << 6;
            codepoint |= (*p++ & 0x3F);
        } else {
            p++;
            continue;
        }
        
        loadGlyph(codepoint);
    }
}

void Font::precacheASCII() {
    std::vector<u32> codepoints;
    for (u32 i = 32; i < 127; i++) {
        codepoints.push_back(i);
    }
    precache(codepoints);
}

void Font::precacheCJK() {
    // 常用汉字范围
    for (u32 i = 0x4E00; i < 0x9FFF; i++) {
        loadGlyph(i);
    }
}

f32 Font::measureText(const char* text) const {
    f32 width = 0;
    
    const u8* p = reinterpret_cast<const u8*>(text);
    while (*p) {
        u32 codepoint = 0;
        
        if ((*p & 0x80) == 0) {
            codepoint = *p++;
        } else if ((*p & 0xE0) == 0xC0) {
            codepoint = (*p++ & 0x1F) << 6;
            codepoint |= (*p++ & 0x3F);
        } else if ((*p & 0xF0) == 0xE0) {
            codepoint = (*p++ & 0x0F) << 12;
            codepoint |= (*p++ & 0x3F) << 6;
            codepoint |= (*p++ & 0x3F);
        } else if ((*p & 0xF8) == 0xF0) {
            codepoint = (*p++ & 0x07) << 18;
            codepoint |= (*p++ & 0x3F) << 12;
            codepoint |= (*p++ & 0x3F) << 6;
            codepoint |= (*p++ & 0x3F);
        } else {
            p++;
            continue;
        }
        
        const GlyphInfo* glyph = getGlyph(codepoint);
        if (glyph) {
            width += glyph->advance;
        }
    }
    
    return width;
}

Vec2 Font::measureTextBounds(const char* text) const {
    f32 width = 0;
    f32 height = lineHeight_;
    f32 x = 0;
    
    const u8* p = reinterpret_cast<const u8*>(text);
    while (*p) {
        u32 codepoint = 0;
        
        if ((*p & 0x80) == 0) {
            codepoint = *p++;
        } else if ((*p & 0xE0) == 0xC0) {
            codepoint = (*p++ & 0x1F) << 6;
            codepoint |= (*p++ & 0x3F);
        } else if ((*p & 0xF0) == 0xE0) {
            codepoint = (*p++ & 0x0F) << 12;
            codepoint |= (*p++ & 0x3F) << 6;
            codepoint |= (*p++ & 0x3F);
        } else if ((*p & 0xF8) == 0xF0) {
            codepoint = (*p++ & 0x07) << 18;
            codepoint |= (*p++ & 0x3F) << 12;
            codepoint |= (*p++ & 0x3F) << 6;
            codepoint |= (*p++ & 0x3F);
        } else {
            p++;
            continue;
        }
        
        if (codepoint == '\n') {
            height += lineHeight_;
            x = 0;
            continue;
        }
        
        const GlyphInfo* glyph = getGlyph(codepoint);
        if (glyph) {
            x += glyph->advance;
            width = std::max(width, x);
        }
    }
    
    return Vec2(width, height);
}

bool Font::hasSpace(u32 width, u32 height) const {
    if (atlasX_ + width < atlasSize_ && atlasY_ + height < atlasSize_) {
        return true;
    }
    
    u32 newY = atlasY_ + atlasRowHeight_;
    return newY + height < atlasSize_;
}

// ============================================================================
// FontManager Implementation
// ============================================================================

bool FontManager::initialize() {
    return true;
}

void FontManager::shutdown() {
    fonts_.clear();
    defaultFont_ = {};
}

FontHandle FontManager::loadFont(const FontDesc& desc) {
    Font font;
    if (!font.create(desc)) {
        return {};
    }
    
    u32 id = nextId_++;
    fonts_[id] = std::move(font);
    return FontHandle{id, 0};
}

FontHandle FontManager::loadFont(const char* path, f32 pixelSize) {
    FontDesc desc;
    desc.path = path;
    desc.pixelSize = pixelSize;
    return loadFont(desc);
}

Font* FontManager::getFont(FontHandle handle) {
    auto it = fonts_.find(handle.index);
    return it != fonts_.end() ? &it->second : nullptr;
}

TextLayout FontManager::layoutText(FontHandle fontHandle, const char* text, const TextFormat& format) {
    TextLayout layout;
    
    Font* font = getFont(fontHandle);
    if (!font) return layout;
    
    f32 x = 0;
    f32 y = font->getAscent() * format.lineHeight;
    f32 lineHeight = font->getLineHeight() * format.lineHeight;
    f32 maxWidth = format.maxWidth;
    
    layout.lineCount = 1;
    
    const u8* p = reinterpret_cast<const u8*>(text);
    while (*p) {
        u32 codepoint = 0;
        
        if ((*p & 0x80) == 0) {
            codepoint = *p++;
        } else if ((*p & 0xE0) == 0xC0) {
            codepoint = (*p++ & 0x1F) << 6;
            codepoint |= (*p++ & 0x3F);
        } else if ((*p & 0xF0) == 0xE0) {
            codepoint = (*p++ & 0x0F) << 12;
            codepoint |= (*p++ & 0x3F) << 6;
            codepoint |= (*p++ & 0x3F);
        } else if ((*p & 0xF8) == 0xF0) {
            codepoint = (*p++ & 0x07) << 18;
            codepoint |= (*p++ & 0x3F) << 12;
            codepoint |= (*p++ & 0x3F) << 6;
            codepoint |= (*p++ & 0x3F);
        } else {
            p++;
            continue;
        }
        
        if (codepoint == '\n') {
            y += lineHeight;
            x = 0;
            layout.lineCount++;
            continue;
        }
        
        if (codepoint == ' ') {
            x += format.fontSize * 0.25f;
            continue;
        }
        
        const GlyphInfo* glyph = font->getGlyph(codepoint);
        if (!glyph) continue;
        
        f32 scale = format.fontSize / font->getPixelSize();
        
        layout.glyphs.push_back(*glyph);
        layout.positions.push_back(Vec2(
            x + glyph->bearingX * scale,
            y - glyph->bearingY * scale
        ));
        
        x += glyph->advance * scale + format.letterSpacing;
        
        // 自动换行
        if (format.wordWrap && maxWidth > 0 && x > maxWidth) {
            y += lineHeight;
            x = 0;
            layout.lineCount++;
        }
    }
    
    layout.width = x;
    layout.height = static_cast<f32>(layout.lineCount) * lineHeight;
    
    return layout;
}

void FontManager::preloadCommonFonts() {
    // 加载系统字体
    // Windows: C:\Windows\Fonts
    // macOS: /System/Library/Fonts
    // Linux: /usr/share/fonts
}

// ============================================================================
// MSDF Implementation
// ============================================================================

namespace MSDF {

#ifdef NOVA_HAS_FREETYPE
bool generateGlyph(void* ftFace, u32 codepoint, u32 width, u32 height, 
                   f32 pxRange, std::vector<u8>& outMSDF) {
    // 简化实现：生成普通 SDF
    // 完整 MSDF 生成需要 msdfgen 库
    
    FT_Face face = *static_cast<FT_Face*>(ftFace);
    
    FT_UInt glyphIndex = FT_Get_Char_Index(face, codepoint);
    if (glyphIndex == 0) return false;
    
    if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_RENDER) != 0) {
        return false;
    }
    
    FT_GlyphSlot slot = face->glyph;
    
    u32 glyphWidth = slot->bitmap.width;
    u32 glyphHeight = slot->bitmap.rows;
    
    // 分配输出缓冲
    outMSDF.resize(width * height * 4);
    
    // 简单的距离场生成
    f32 centerX = width / 2.0f;
    f32 centerY = height / 2.0f;
    
    for (u32 y = 0; y < height; y++) {
        for (u32 x = 0; x < width; x++) {
            // 计算到字形边界的距离
            f32 dist = 0;
            
            // 将坐标映射到字形空间
            i32 gx = static_cast<i32>((x - centerX + glyphWidth / 2.0f));
            i32 gy = static_cast<i32>((y - centerY + glyphHeight / 2.0f));
            
            if (gx >= 0 && gx < static_cast<i32>(glyphWidth) &&
                gy >= 0 && gy < static_cast<i32>(glyphHeight)) {
                u8 value = slot->bitmap.buffer[gy * slot->bitmap.pitch + gx];
                dist = (value - 128) / 128.0f;
            } else {
                // 外部距离
                f32 dx = 0, dy = 0;
                if (gx < 0) dx = -gx;
                else if (gx >= static_cast<i32>(glyphWidth)) dx = gx - glyphWidth + 1;
                
                if (gy < 0) dy = -gy;
                else if (gy >= static_cast<i32>(glyphHeight)) dy = gy - glyphHeight + 1;
                
                dist = -std::sqrt(dx * dx + dy * dy) / pxRange;
            }
            
            // 编码为 RGBA
            u64 idx = (y * width + x) * 4;
            u8 d = static_cast<u8>(std::clamp((dist + 1.0f) * 127.5f, 0.0f, 255.0f));
            outMSDF[idx] = d;
            outMSDF[idx + 1] = d;
            outMSDF[idx + 2] = d;
            outMSDF[idx + 3] = 255;
        }
    }
    
    return true;
}
#else
bool generateGlyph(void*, u32, u32, u32, f32, std::vector<u8>&) {
    return false;
}
#endif

void renderToBitmap(const u8* msdf, u32 width, u32 height, 
                   std::vector<u8>& outBitmap, f32 pxRange) {
    outBitmap.resize(width * height);
    
    for (u32 y = 0; y < height; y++) {
        for (u32 x = 0; x < width; x++) {
            u64 idx = (y * width + x);
            u64 msdfIdx = idx * 4;
            
            // 简单的距离计算
            f32 d = msdf[msdfIdx] / 255.0f - 0.5f;
            f32 opacity = std::clamp(d * pxRange + 0.5f, 0.0f, 1.0f);
            
            outBitmap[idx] = static_cast<u8>(opacity * 255);
        }
    }
}

f32 signedDistance(const u8* msdf, u32 width, u32 height, f32 x, f32 y) {
    i32 ix = static_cast<i32>(x);
    i32 iy = static_cast<i32>(y);
    
    if (ix < 0 || ix >= static_cast<i32>(width) ||
        iy < 0 || iy >= static_cast<i32>(height)) {
        return -1.0f;
    }
    
    u64 idx = (iy * width + ix) * 4;
    return msdf[idx] / 255.0f - 0.5f;
}

} // namespace MSDF

// ============================================================================
// TextRenderer Implementation
// ============================================================================

bool TextRenderer::initialize() {
    return true;
}

void TextRenderer::shutdown() {
    vertexBuffer_ = {};
    indexBuffer_ = {};
    pipeline_ = {};
    sampler_ = {};
}

void TextRenderer::renderText(const char* text, const Vec2& position,
                              const Color& color, f32 size,
                              FontHandle font) {
    vertices_.clear();
    indices_.clear();
    
    // 获取字形并生成顶点
    // 需要字体管理器
    
    (void)text;
    (void)position;
    (void)color;
    (void)size;
    (void)font;
    
    if (!vertices_.empty()) {
        // 绑定缓冲并绘制
    }
}

void TextRenderer::renderLayout(const TextLayout& layout, const Vec2& position, const Color& color) {
    for (size_t i = 0; i < layout.glyphs.size(); i++) {
        const GlyphInfo& glyph = layout.glyphs[i];
        const Vec2& pos = layout.positions[i];
        
        f32 x0 = position.x + pos.x;
        f32 y0 = position.y + pos.y;
        f32 x1 = x0 + glyph.width;
        f32 y1 = y0 + glyph.height;
        
        u32 baseIdx = static_cast<u32>(vertices_.size());
        
        // 四个顶点
        vertices_.push_back({{x0, y0}, {glyph.u0, glyph.v0}, color});
        vertices_.push_back({{x1, y0}, {glyph.u1, glyph.v0}, color});
        vertices_.push_back({{x1, y1}, {glyph.u1, glyph.v1}, color});
        vertices_.push_back({{x0, y1}, {glyph.u0, glyph.v1}, color});
        
        // 两个三角形
        indices_.push_back(baseIdx);
        indices_.push_back(baseIdx + 1);
        indices_.push_back(baseIdx + 2);
        indices_.push_back(baseIdx);
        indices_.push_back(baseIdx + 2);
        indices_.push_back(baseIdx + 3);
    }
}

void TextRenderer::beginBatch() {
    vertices_.clear();
    indices_.clear();
    inBatch_ = true;
}

void TextRenderer::addText(const char* text, const Vec2& position,
                          const Color& color, f32 size,
                          FontHandle font) {
    if (!inBatch_) return;
    
    (void)text;
    (void)position;
    (void)color;
    (void)size;
    (void)font;
    // 添加到批次
}

void TextRenderer::endBatch() {
    if (vertices_.empty()) {
        inBatch_ = false;
        return;
    }
    
    // 上传顶点缓冲并绘制
    
    inBatch_ = false;
}

} // namespace Nova