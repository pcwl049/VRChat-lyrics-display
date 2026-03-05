// MainWindow.h - 主窗口
#pragma once
#include "pch.h"

namespace winrt::MoeKoeGUI::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();
        
        // 属性
        hstring SongTitle() { return m_songTitle; }
        void SongTitle(hstring const& value);
        hstring SongArtist() { return m_songArtist; }
        void SongArtist(hstring const& value);
        hstring CurrentLyric() { return m_currentLyric; }
        void CurrentLyric(hstring const& value);
        double Progress() { return m_progress; }
        void Progress(double value);
        hstring TimeText() { return m_timeText; }
        void TimeText(hstring const& value);
        
        // 方法
        void UpdateSongInfo(hstring const& title, hstring const& artist);
        void UpdateLyric(hstring const& lyric);
        void UpdateProgress(double current, double total);
        
        // 事件
        event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(event_token const& token) noexcept;
        
    private:
        hstring m_songTitle;
        hstring m_songArtist;
        hstring m_currentLyric;
        double m_progress = 0.0;
        hstring m_timeText;
        
        event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
        
        Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };
        Microsoft::UI::Xaml::Controls::Grid m_rootGrid{ nullptr };
        
        void InitializeUI();
        void StartMonitoring();
        
        // GSMTC 监控
        Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager m_sessionManager{ nullptr };
        Windows::Media::Control::GlobalSystemMediaTransportControlsSession m_session{ nullptr };
        event_token m_mediaPropertiesChangedToken;
        event_token m_timelinePropertiesChangedToken;
        event_token m_playbackInfoChangedToken;
        
        void OnMediaPropertiesChanged(Windows::Media::Control::GlobalSystemMediaTransportControlsSession const&, Windows::Media::Control::MediaPropertiesChangedEventArgs const&);
        void OnTimelinePropertiesChanged(Windows::Media::Control::GlobalSystemMediaTransportControlsSession const&, Windows::Media::Control::TimelinePropertiesChangedEventArgs const&);
        void OnPlaybackInfoChanged(Windows::Media::Control::GlobalSystemMediaTransportControlsSession const&, Windows::Media::Control::PlaybackInfoChangedEventArgs const&);
    };
}

namespace winrt::MoeKoeGUI::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
