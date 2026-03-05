// App.h - 应用程序入口
#pragma once
#include "pch.h"

namespace winrt::MoeKoeGUI::implementation
{
    struct App : AppT<App>
    {
        App();
        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);
        void OnSuspending(IInspectable const&, Windows::ApplicationModel::SuspendingEventArgs const&);
        void OnNavigationFailed(IInspectable const&, Microsoft::UI::Xaml::Navigation::NavigationFailedEventArgs const&);
    };
}

namespace winrt::MoeKoeGUI::factory_implementation
{
    struct App : AppT<App, implementation::App>
    {
    };
}
