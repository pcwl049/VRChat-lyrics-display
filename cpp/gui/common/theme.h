#pragma once

#include <windows.h>

// ============================================================================
// 主题颜色结构体
// ============================================================================

struct ThemeColors {
    COLORREF bgStart = 0;
    COLORREF bgEnd = 0;
    COLORREF bg = 0;
    COLORREF card = 0;
    COLORREF cardBorder = 0;
    COLORREF accent = 0;
    COLORREF accentGlow = 0;
    COLORREF text = 0;
    COLORREF textDim = 0;
    COLORREF border = 0;
    COLORREF titlebar = 0;
    COLORREF editBg = 0;
    COLORREF glassTint = 0;
    int glassAlpha = 100;
};

// 全局主题颜色
extern ThemeColors g_colors;

// ============================================================================
// 主题函数
// ============================================================================

void UpdateThemeColors();

// ============================================================================
// 颜色访问宏 (保持兼容性)
// ============================================================================
#define COLOR_BG_START g_colors.bgStart
#define COLOR_BG_END g_colors.bgEnd
#define COLOR_BG g_colors.bg
#define COLOR_CARD g_colors.card
#define COLOR_CARD_BORDER g_colors.cardBorder
#define COLOR_ACCENT g_colors.accent
#define COLOR_ACCENT_GLOW g_colors.accentGlow
#define COLOR_TEXT g_colors.text
#define COLOR_TEXT_DIM g_colors.textDim
#define COLOR_BORDER g_colors.border
#define COLOR_TITLEBAR g_colors.titlebar
#define COLOR_EDIT_BG g_colors.editBg
#define COLOR_GLASS_TINT g_colors.glassTint
#define GLASS_ALPHA g_colors.glassAlpha

// 主题相关颜色
#define COLOR_BTN_BG RGB(40, 40, 55)
#define COLOR_BTN_HOVER RGB(55, 55, 75)
#define COLOR_MENU_BG RGB(35, 40, 50)
#define COLOR_MENU_BORDER RGB(70, 75, 85)
#define COLOR_MENU_HOVER RGB(50, 55, 65)
#define COLOR_BOX_BG RGB(40, 45, 55)
#define COLOR_BOX_HOVER RGB(50, 55, 65)
#define COLOR_BOX_BORDER RGB(60, 65, 75)
#define COLOR_BOX_BORDER_HOVER RGB(80, 90, 110)
#define COLOR_CHECK_BG RGB(50, 50, 65)
#define COLOR_CHECK_ACCENT COLOR_ACCENT
#define COLOR_SUCCESS RGB(80, 200, 120)
#define COLOR_WARNING RGB(255, 180, 50)
#define COLOR_ERROR RGB(255, 100, 100)
