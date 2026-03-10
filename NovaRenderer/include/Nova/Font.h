/**
 * Nova Renderer - Font System
 * 字体系统 (MSDF)
 */

#pragma once

#include "Types.h"
#include "Texture.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace Nova {

// 字形信息
struct GlyphInfo {
    // 纹理坐标
    f32 u0, v0;          // 左上角
    f32 u1, v1;          // 右下角
    
    // 尺寸
    f32 width, height;   // 字形尺寸
    
    // 边距
    f32 bearingX, bearingY;
    f32 advance;
    
    // MSDF 像素范围
    f32 pxRange;
    
    // 码点
    u32 codepoint;
};

// 字体描述
struct FontDesc {
    std::string path;
    f32 pixelSize = 32.0f;
    u32 atlasSize = 1024;
    f32 pxRange = 2.0f;
    bool useMSDF = true;
};

// 字体类
class Font {
public:
    Font() = default;
    ~Font();
    
    // 创建字体
    bool create(const FontDesc& desc);
    void destroy();
    
    // 获取字形
    const GlyphInfo* getGlyph(u32 codepoint) const;
    
    // 预缓存字形
    void precache(const std::vector<u32>& codepoints);
    void precache(const char* text);
    void precacheASCII();
    void precacheCJK();
    
    // 测量文本
    f32 measureText(const char* text) const;
    Vec2 measureTextBounds(const char* text) const;
    
    // 获取纹理
    TextureHandle getTexture() const { return atlasTexture_; }
    
    // 获取信息
    f32 getPixelSize() const { return pixelSize_; }
    f32 getLineHeight() const { return lineHeight_; }
    f32 getAscent() const { return ascent_; }
    f32 getDescent() const { return descent_; }
    
    // 图集是否有空间
    bool hasSpace(u32 width, u32 height) const;
    
private:
    // 加载字形到图集
    bool loadGlyph(u32 codepoint);
    
    // FreeType 内部数据
    void* ftFace_ = nullptr;
    void* ftLibrary_ = nullptr;
    
    // 图集
    TextureHandle atlasTexture_;
    u32 atlasSize_ = 1024;
    u32 atlasX_ = 0;
    u32 atlasY_ = 0;
    u32 atlasRowHeight_ = 0;
    std::vector<u8> atlasData_;
    
    // 字形映射
    std::unordered_map<u32, GlyphInfo> glyphs_;
    
    // 字体信息
    f32 pixelSize_ = 32.0f;
    f32 lineHeight_ = 0.0f;
    f32 ascent_ = 0.0f;
    f32 descent_ = 0.0f;
    f32 pxRange_ = 2.0f;
    bool useMSDF_ = true;
    
    // 已加载范围
    std::vector<std::pair<u32, u32>> loadedRanges_;
};

// 文本布局
struct TextLayout {
    std::vector<GlyphInfo> glyphs;
    std::vector<Vec2> positions;
    f32 width = 0;
    f32 height = 0;
    u32 lineCount = 1;
};

// 文本格式化选项
struct TextFormat {
    f32 fontSize = 32.0f;
    f32 lineHeight = 1.2f;
    f32 letterSpacing = 0.0f;
    bool wordWrap = false;
    f32 maxWidth = 0.0f;
    bool alignRight = false;
    bool alignCenter = false;
};

// 字体管理器
class FontManager {
public:
    bool initialize();
    void shutdown();
    
    // 加载字体
    FontHandle loadFont(const FontDesc& desc);
    FontHandle loadFont(const char* path, f32 pixelSize = 32.0f);
    
    // 获取字体
    Font* getFont(FontHandle handle);
    
    // 布局文本
    TextLayout layoutText(FontHandle font, const char* text, const TextFormat& format = {});
    
    // 默认字体
    FontHandle getDefaultFont() const { return defaultFont_; }
    void setDefaultFont(FontHandle font) { defaultFont_ = font; }
    
    // 预加载常见字体
    void preloadCommonFonts();
    
private:
    std::unordered_map<u32, Font> fonts_;
    FontHandle defaultFont_;
    u32 nextId_ = 1;
};

// MSDF 生成器
namespace MSDF {
    // 生成 MSDF 字形
    bool generateGlyph(void* ftFace, u32 codepoint, u32 width, u32 height, 
                      f32 pxRange, std::vector<u8>& outMSDF);
    
    // 渲染 MSDF 到位图
    void renderToBitmap(const u8* msdf, u32 width, u32 height, 
                       std::vector<u8>& outBitmap, f32 pxRange = 2.0f);
    
    // 计算距离
    f32 signedDistance(const u8* msdf, u32 width, u32 height, f32 x, f32 y);
}

// 文本渲染器
class TextRenderer {
public:
    bool initialize();
    void shutdown();
    
    // 渲染文本
    void renderText(const char* text, const Vec2& position, 
                   const Color& color, f32 size = 32.0f,
                   FontHandle font = {});
    
    // 渲染布局好的文本
    void renderLayout(const TextLayout& layout, const Vec2& position, const Color& color);
    
    // 批量渲染
    void beginBatch();
    void addText(const char* text, const Vec2& position, 
                const Color& color, f32 size = 32.0f,
                FontHandle font = {});
    void endBatch();
    
private:
    struct TextVertex {
        Vec2 position;
        Vec2 texcoord;
        Color color;
    };
    
    std::vector<TextVertex> vertices_;
    std::vector<u32> indices_;
    
    BufferHandle vertexBuffer_;
    BufferHandle indexBuffer_;
    PipelineHandle pipeline_;
    SamplerHandle sampler_;
    
    bool inBatch_ = false;
};

} // namespace Nova
