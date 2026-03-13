// theme.cpp - 主题管理
#include "theme.h"

// 全局主题颜色定义
ThemeColors g_colors;

void UpdateThemeColors() {
    // 固定使用深色主题 (毛玻璃深空蓝主题)
    g_colors.bgStart = RGB(18, 18, 24);
    g_colors.bgEnd = RGB(18, 18, 24);
    g_colors.bg = RGB(18, 18, 24);
    g_colors.card = RGB(30, 40, 60);
    g_colors.cardBorder = RGB(50, 70, 100);
    g_colors.accent = RGB(80, 180, 255);
    g_colors.accentGlow = RGB(60, 140, 220);
    g_colors.text = RGB(240, 245, 255);
    g_colors.textDim = RGB(140, 150, 170);
    g_colors.border = RGB(50, 70, 100);
    g_colors.titlebar = RGB(20, 28, 45);
    g_colors.editBg = RGB(35, 45, 65);
    g_colors.glassTint = RGB(20, 30, 50);
    g_colors.glassAlpha = 100;  // 大幅降低不透明度，让毛玻璃非常明显
}
