#pragma once

#include <windows.h>
#include <string>
#include "../common/types.h"

// ============================================================================
// 自定义对话框系统
// ============================================================================

// 对话框类型
enum DialogType { DIALOG_INFO, DIALOG_CONFIRM, DIALOG_ERROR, DIALOG_UPDATE };

// 对话框配置
struct DialogConfig {
    DialogType type = DIALOG_INFO;
    std::wstring title;
    std::wstring content;      // 主内容（支持多行 \n）
    std::wstring btn1Text;     // 主按钮（强调色）
    std::wstring btn2Text;     // 次按钮（边框样式）
    std::wstring btn3Text;     // 第三按钮（用于更新对话框：跳过）
    bool hasBtn2 = false;
    bool hasBtn3 = false;
};

// 对话框全局状态（供外部访问）
extern HWND g_dialogHwnd;
extern int g_dialogResult;
extern bool g_dialogClosed;
extern int g_dialogBtnHover;
extern DialogConfig g_dialogConfig;
extern int g_dialogWidth;
extern int g_dialogHeight;
extern Animation g_dialogFadeAnim;
extern Animation g_dialogScaleAnim;
extern bool g_dialogAnimComplete;

// 对话框窗口过程
LRESULT CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// 显示自定义对话框
// 返回值: 0=关闭/取消, 1=按钮1(主), 2=按钮2(次), 3=按钮3(跳过)
int ShowCustomDialog(const DialogConfig& config);

// 便捷函数
bool ShowInfoDialog(const std::wstring& title, const std::wstring& content);
bool ShowErrorDialog(const std::wstring& title, const std::wstring& content);
bool ShowConfirmDialog(const std::wstring& title, const std::wstring& content, 
                       const std::wstring& btnYes = L"确定", const std::wstring& btnNo = L"取消");

// 更新对话框（3按钮：更新 | 取消 | 跳过）
// 返回值: 0=取消/关闭, 1=更新, 2=取消, 3=跳过
int ShowUpdateDialog(const std::wstring& title, const std::wstring& content);

// 首次运行对话框
bool ShowFirstRunDialog();
