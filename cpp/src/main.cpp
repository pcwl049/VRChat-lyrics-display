#include <windows.h>
#include <winhttp.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Foundation.h>
#include <iostream>
#include <chrono>
#include <thread>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "runtimeobject.lib")
#pragma comment(lib, "windowsapp.lib")

using namespace winrt::Windows::Media::Control;
using namespace winrt::Windows::Foundation;

// Convert 100ns units to seconds
inline double TimeSpanToSeconds(const TimeSpan& span) {
    return std::chrono::duration<double>(span).count();
}

int main() {
    // Initialize WinRT
    winrt::init_apartment(winrt::apartment_type::single_threaded);

    std::cout << "========================================" << std::endl;
    std::cout << "  MoeKoeVRChat C++ Test" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    try {
        // Get session manager
        auto async = GlobalSystemMediaTransportControlsSessionManager::RequestAsync();
        auto session_manager = async.get();

        if (session_manager) {
            auto session = session_manager.GetCurrentSession();

            if (session) {
                auto app_id = session.SourceAppUserModelId();
                std::wcout << L"App: " << app_id.c_str() << std::endl;

                auto playback = session.GetPlaybackInfo();
                std::cout << "Status: " << (playback.PlaybackStatus() == 4 ? "Playing" :
                                            playback.PlaybackStatus() == 5 ? "Paused" : "Other") << std::endl;

                auto timeline = session.GetTimelineProperties();
                std::cout << "Progress: " << TimeSpanToSeconds(timeline.Position()) << "s / "
                          << TimeSpanToSeconds(timeline.EndTime()) << "s" << std::endl;

                auto media = session.TryGetMediaPropertiesAsync().get();
                std::wcout << L"Title: " << media.Title().c_str() << std::endl;
                std::wcout << L"Artist: " << media.Artist().c_str() << std::endl;
            } else {
                std::cout << "No active media session" << std::endl;
            }
        }
    } catch (const winrt::hresult_error& e) {
        std::wcout << L"Error: " << e.message().c_str() << std::endl;
    }

    winrt::uninit_apartment();
    return 0;
}