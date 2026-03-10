/**
 * Render Pipeline Demo
 * 展示如何使用自研渲染管线替换现有渲染代码
 */

#include "render_pipeline.h"
#include <windows.h>
#include <string>

using namespace RenderPipeline;

// ============================================================================
// 示例：音乐显示窗口
// ============================================================================

class MusicDisplayWindow {
public:
    MusicDisplayWindow(HWND hwnd, int width, int height);
    ~MusicDisplayWindow();
    
    // 更新歌词
    void setLyrics(const std::wstring& lyrics);
    
    // 更新歌曲信息
    void setSongInfo(const std::wstring& title, const std::wstring& artist);
    
    // 更新进度
    void setProgress(float progress); // 0.0 - 1.0
    
    // 标记重绘
    void markDirty();
    
    // 渲染一帧
    void render();
    
private:
    HWND hwnd_;
    Renderer renderer_;
    
    // 图层ID
    enum LayerID {
        LAYER_BACKGROUND = 1,
        LAYER_ALBUM_ART,
        LAYER_SONG_INFO,
        LAYER_LYRICS,
        LAYER_PROGRESS,
        LAYER_BUTTONS
    };
    
    // 状态
    std::wstring lyrics_;
    std::wstring songTitle_;
    std::wstring songArtist_;
    float progress_ = 0.0f;
    
    // 字体
    HFONT fontTitle_;
    HFONT fontArtist_;
    HFONT fontLyrics_;
    HFONT fontSmall_;
    
    // 颜色主题
    struct Theme {
        Color background{15, 20, 30, 230};
        Color card{25, 30, 45, 200};
        Color accent{0, 120, 255, 255};
        Color text{240, 240, 250, 255};
        Color textDim{150, 150, 160, 255};
        Color progressBg{40, 45, 60, 255};
        Color progressFg{0, 150, 255, 255};
    } theme_;
    
    void initLayers(int width, int height);
    void setupLayerPainters();
};

MusicDisplayWindow::MusicDisplayWindow(HWND hwnd, int width, int height)
    : hwnd_(hwnd) {
    
    // 初始化渲染器
    renderer_.initialize(hwnd, width, height);
    
    // 创建字体
    fontTitle_ = CreateFontW(28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    
    fontArtist_ = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    
    fontLyrics_ = CreateFontW(24, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    
    fontSmall_ = CreateFontW(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    
    // 初始化图层
    initLayers(width, height);
    setupLayerPainters();
}

MusicDisplayWindow::~MusicDisplayWindow() {
    DeleteObject(fontTitle_);
    DeleteObject(fontArtist_);
    DeleteObject(fontLyrics_);
    DeleteObject(fontSmall_);
}

void MusicDisplayWindow::initLayers(int width, int height) {
    int padding = 20;
    int cardWidth = width - padding * 2;
    
    // 背景层 - 静态，只绘制一次
    renderer_.createLayer(LAYER_BACKGROUND, Rect(0, 0, width, height));
    
    // 歌曲信息卡片 - 静态，歌曲变化时重绘
    renderer_.createLayer(LAYER_SONG_INFO, Rect(padding, padding, cardWidth, 100));
    
    // 歌词层 - 动态，每次歌词变化时重绘
    renderer_.createLayer(LAYER_LYRICS, Rect(padding, 140, cardWidth, 80));
    
    // 进度条 - 动态，频繁更新
    renderer_.createLayer(LAYER_PROGRESS, Rect(padding, height - 60, cardWidth, 40));
}

void MusicDisplayWindow::setupLayerPainters() {
    // ===== 背景层绘制 =====
    Layer* bgLayer = renderer_.getLayer(LAYER_BACKGROUND);
    bgLayer->setOpaque(true);  // 背景是不透明的
    bgLayer->setPaintFunc([this](CommandQueue& queue, const Rect& dirtyRect) {
        Rect bounds(0, 0, 700, 300);  // 示例尺寸
        
        // 绘制背景
        queue.addRoundRectAlpha(bounds, theme_.background, 15, 0);
        
        // 绘制边框
        Color borderColor{40, 50, 70, 150};
        // ... 边框绘制代码
    });
    
    // ===== 歌曲信息层绘制 =====
    Layer* infoLayer = renderer_.getLayer(LAYER_SONG_INFO);
    infoLayer->setPaintFunc([this](CommandQueue& queue, const Rect& dirtyRect) {
        Rect bounds = infoLayer->bounds();
        
        // 卡片背景
        queue.addRoundRectAlpha(bounds, theme_.card, 10, 0);
        
        // 歌曲标题
        Rect titleRect(bounds.x + 20, bounds.y + 15, bounds.width - 40, 35);
        queue.addText(titleRect, songTitle_.c_str(), songTitle_.length(),
                     theme_.text, fontTitle_, 0);
        
        // 艺术家
        Rect artistRect(bounds.x + 20, bounds.y + 55, bounds.width - 40, 25);
        queue.addText(artistRect, songArtist_.c_str(), songArtist_.length(),
                     theme_.textDim, fontArtist_, 0);
    });
    
    // ===== 歌词层绘制 =====
    Layer* lyricsLayer = renderer_.getLayer(LAYER_LYRICS);
    lyricsLayer->setPaintFunc([this](CommandQueue& queue, const Rect& dirtyRect) {
        Rect bounds = lyricsLayer->bounds();
        
        // 歌词卡片背景
        queue.addRoundRectAlpha(bounds, theme_.card, 10, 0);
        
        // 歌词文本（居中）
        Rect textRect(bounds.x + 10, bounds.y + 25, bounds.width - 20, 40);
        queue.addText(textRect, lyrics_.c_str(), lyrics_.length(),
                     theme_.text, fontLyrics_, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    });
    
    // ===== 进度条层绘制 =====
    Layer* progressLayer = renderer_.getLayer(LAYER_PROGRESS);
    progressLayer->setPaintFunc([this](CommandQueue& queue, const Rect& dirtyRect) {
        Rect bounds = progressLayer->bounds();
        
        int barHeight = 8;
        int barY = bounds.y + (bounds.height - barHeight) / 2;
        int barWidth = bounds.width - 40;
        int progressWidth = (int)(barWidth * progress_);
        
        // 进度条背景
        Rect bgRect(bounds.x + 20, barY, barWidth, barHeight);
        queue.addRoundRect(bgRect, theme_.progressBg, 4, 0);
        
        // 进度条前景
        if (progressWidth > 0) {
            Rect fgRect(bounds.x + 20, barY, progressWidth, barHeight);
            queue.addRoundRect(fgRect, theme_.progressFg, 4, 0);
        }
    });
}

void MusicDisplayWindow::setLyrics(const std::wstring& lyrics) {
    if (lyrics_ != lyrics) {
        lyrics_ = lyrics;
        renderer_.markLayerDirty(LAYER_LYRICS);
    }
}

void MusicDisplayWindow::setSongInfo(const std::wstring& title, const std::wstring& artist) {
    bool changed = (songTitle_ != title || songArtist_ != artist);
    songTitle_ = title;
    songArtist_ = artist;
    if (changed) {
        renderer_.markLayerDirty(LAYER_SONG_INFO);
    }
}

void MusicDisplayWindow::setProgress(float progress) {
    progress_ = progress;
    renderer_.markLayerDirty(LAYER_PROGRESS);
}

void MusicDisplayWindow::markDirty() {
    renderer_.markAllDirty();
}

void MusicDisplayWindow::render() {
    renderer_.beginFrame();
    
    // 获取脏区域
    Rect dirtyRect(0, 0, 700, 300);  // 实际应从 dirtyRegions 获取
    
    // 渲染所有图层
    renderer_.render(dirtyRect);
    
    renderer_.endFrame();
    renderer_.present();
}

// ============================================================================
// 性能对比示例
// ============================================================================

void ComparePerformance() {
    /**
     * 传统 GDI 方式：
     * - 每帧重绘所有元素
     * - O(n) n = 元素数量
     * - 30-60 FPS
     * 
     * 渲染管线方式：
     * - 只重绘变化的图层
     * - O(m) m = 变化的图层数
     * - 60-120+ FPS
     * 
     * 示例场景：歌词滚动
     * 
     * 传统方式：
     * 每33ms重绘：背景 + 卡片 + 歌词 + 进度条 = 4次绘制
     * 每秒：4 * 30 = 120次绘制
     * 
     * 渲染管线：
     * 背景层：只绘制1次（启动时）
     * 卡片层：只绘制1次（歌曲变化时）
     * 歌词层：每个歌词变化时绘制 ≈ 每2秒1次
     * 进度层：每帧绘制
     * 
     * 每秒：1 + 0.5 + 30 = 31.5次绘制
     * 性能提升：120 / 31.5 ≈ 3.8倍
     */
    
    /**
     * 实际测试方法：
     * 
     * // 传统方式
     * DWORD start = GetTickCount();
     * for (int i = 0; i < 1000; i++) {
     *     traditionalPaint();  // 全部重绘
     * }
     * DWORD traditionalTime = GetTickCount() - start;
     * 
     * // 渲染管线方式
     * start = GetTickCount();
     * for (int i = 0; i < 1000; i++) {
     *     pipelinePaint();  // 只重绘变化部分
     * }
     * DWORD pipelineTime = GetTickCount() - start;
     * 
     * printf("传统方式: %dms\n", traditionalTime);
     * printf("渲染管线: %dms\n", pipelineTime);
     * printf("性能提升: %.1fx\n", (float)traditionalTime / pipelineTime);
     */
}

// ============================================================================
// 与现有代码集成示例
// ============================================================================

/**
 * 如何将现有 main_gui.cpp 迁移到渲染管线：
 * 
 * 1. 创建 Renderer 实例
 *    Renderer g_renderer;
 * 
 * 2. 初始化时创建图层
 *    g_renderer.initialize(hwnd, width, height);
 *    Layer* bgLayer = g_renderer.createLayer(LAYER_BG, Rect(0, 0, width, height));
 *    Layer* cardLayer = g_renderer.createLayer(LAYER_CARD, ...);
 * 
 * 3. 设置每个图层的绘制函数
 *    bgLayer->setPaintFunc([](CommandQueue& q, const Rect& r) {
 *        // 原来的背景绘制代码
 *    });
 * 
 * 4. 状态变化时标记脏
 *    void setLyrics(const wchar_t* text) {
 *        g_lyrics = text;
 *        g_renderer.markLayerDirty(LAYER_LYRICS);
 *    }
 * 
 * 5. WM_PAINT 中调用渲染
 *    case WM_PAINT:
 *        g_renderer.beginFrame();
 *        g_renderer.render(g_renderer.dirtyBounds());
 *        g_renderer.endFrame();
 *        g_renderer.present();
 *        break;
 * 
 * 6. 可选：切换后端
 *    // 使用 Direct2D 后端（需要额外实现）
 *    g_renderer.setBackend(std::make_unique<D2DBackend>());
 */

// ============================================================================
// 图层缓存策略
// ============================================================================

/**
 * 图层缓存策略：
 * 
 * 1. 完全静态（缓存一次）
 *    - 背景层：窗口创建时绘制，之后不再变化
 *    - 固定UI元素：标题栏、装饰元素
 * 
 * 2. 偶尔更新（按需缓存）
 *    - 歌曲信息：歌曲切换时重绘
 *    - 设置面板：设置变化时重绘
 * 
 * 3. 频繁更新（禁用缓存或部分缓存）
 *    - 进度条：每帧更新，但可以只缓存轨道背景
 *    - 动画元素：按帧更新
 * 
 * 4. 混合策略
 *    - 图层内部再分层：静态部分缓存，动态部分直接绘制
 *    - 例如：歌词卡片 = 卡片背景(缓存) + 歌词文本(动态)
 */

// ============================================================================
// 内存优化
// ============================================================================

/**
 * 内存优化技巧：
 * 
 * 1. 按需创建缓存
 *    - 只在图层第一次变脏时创建缓存位图
 *    - 长时间不更新的图层可以释放缓存
 * 
 * 2. 共享资源
 *    - 字体、画刷、位图在多个图层间共享
 *    - 使用纹理图集减少小位图数量
 * 
 * 3. 分辨率适配
 *    - 小尺寸图层使用低分辨率缓存
 *    - 高DPI适配：按比例放大缓存
 * 
 * 4. 延迟加载
 *    - 不可见图层不创建缓存
 *    - 滚动时只缓存可见区域
 */
