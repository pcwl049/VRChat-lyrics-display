// App.cpp - 应用程序入口
#include "pch.h"
#include "App.h"
#include "MainWindow.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Windows::ApplicationModel;

namespace winrt::MoeKoeGUI::implementation
{
    App::App()
    {
        // Xaml 控件需要初始化
        Initialize();
        AddRef();
        // 关联事件处理
        UnhandledException([this](IInspectable const&, UnhandledExceptionEventArgs const& e)
        {
            if (IsDebuggerPresent())
            {
                auto errorMessage = e.Message();
                __debugbreak();
            }
        });
    }

    void App::OnLaunched(LaunchActivatedEventArgs const& e)
    {
        window = make<MainWindow>();
        window.Activate();
    }

    void App::OnSuspending(IInspectable const&, SuspendingEventArgs const&)
    {
        // 保存应用状态
    }

    void App::OnNavigationFailed(IInspectable const&, Navigation::NavigationFailedEventArgs const& e)
    {
        throw hresult_error(E_FAIL, hstring(L("导航失败: ") + e.SourcePageType().Name));
    }
}
