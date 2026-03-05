// MainWindow.cpp - 主窗口实现
#include "pch.h"
#include "MainWindow.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Media::Control;

namespace winrt::MoeKoeGUI::implementation
{
    MainWindow::MainWindow()
    {
        // 设置窗口标题
        Title(L"MoeKoeVRChat");
        
        // 设置窗口大小
        this->ExtendsContentIntoTitleBar(true);
        
        // 初始化 UI
        InitializeUI();
        
        // 启动 GSMTC 监控
        StartMonitoring();
    }
    
    void MainWindow::InitializeUI()
    {
        // 创建根容器 - 玻璃效果
        Grid rootGrid;
        rootGrid.Background(make<AcrylicBrush>());
        
        // 定义行
        auto rowDef1 = RowDefinition();
        auto rowDef2 = RowDefinition();
        auto rowDef3 = RowDefinition();
        auto rowDef4 = RowDefinition();
        auto rowDef5 = RowDefinition();
        
        rowDef1.Height(GridLengthHelper::Auto);
        rowDef2.Height(GridLengthHelper::Auto);
        rowDef3.Height(GridLengthHelper::Auto);
        rowDef4.Height(GridLengthHelper::Auto);
        rowDef5.Height(GridLengthHelper(1, GridUnitType::Star));
        
        rootGrid.RowDefinitions().Append(rowDef1);
        rootGrid.RowDefinitions().Append(rowDef2);
        rootGrid.RowDefinitions().Append(rowDef3);
        rootGrid.RowDefinitions().Append(rowDef4);
        rootGrid.RowDefinitions().Append(rowDef5);
        
        // 歌曲标题区域
        StackPanel titlePanel;
        titlePanel.Orientation(Orientation::Vertical);
        titlePanel.Margin(ThicknessHelper::FromLengths(16, 16, 16, 8));
        Grid::SetRow(titlePanel, 0);
        
        TextBlock titleText;
        titleText.Text(m_songTitle);
        titleText.FontSize(22);
        titleText.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        titleText.Foreground(SolidColorBrush(Windows::UI::Colors::White()));
        
        TextBlock artistText;
        artistText.Text(m_songArtist);
        artistText.FontSize(14);
        artistText.Foreground(SolidColorBrush(Windows::UI::ColorHelper::FromArgb(180, 255, 255, 255)));
        artistText.Margin(ThicknessHelper::FromLengths(0, 4, 0, 0));
        
        titlePanel.Children().Append(titleText);
        titlePanel.Children().Append(artistText);
        
        // 进度条区域
        StackPanel progressPanel;
        progressPanel.Orientation(Orientation::Horizontal);
        progressPanel.Margin(ThicknessHelper::FromLengths(16, 8, 16, 8));
        Grid::SetRow(progressPanel, 1);
        
        ProgressBar progressBar;
        progressBar.Width(320);
        progressBar.Height(4);
        progressBar.Value(m_progress * 100);
        progressBar.Maximum(100);
        progressBar.Foreground(SolidColorBrush(Windows::UI::ColorHelper::FromArgb(255, 96, 205, 255)));
        progressBar.Background(SolidColorBrush(Windows::UI::ColorHelper::FromArgb(60, 255, 255, 255)));
        progressBar.CornerRadius(CornerRadiusHelper::FromUniformRadius(2));
        
        TextBlock timeText;
        timeText.Text(m_timeText);
        timeText.FontSize(12);
        timeText.Foreground(SolidColorBrush(Windows::UI::ColorHelper::FromArgb(150, 255, 255, 255)));
        timeText.Margin(ThicknessHelper::FromLengths(12, 0, 0, 0));
        timeText.VerticalAlignment(VerticalAlignment::Center);
        
        progressPanel.Children().Append(progressBar);
        progressPanel.Children().Append(timeText);
        
        // 歌词区域
        Border lyricBorder;
        lyricBorder.Background(SolidColorBrush(Windows::UI::ColorHelper::FromArgb(25, 255, 255, 255)));
        lyricBorder.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
        lyricBorder.Margin(ThicknessHelper::FromLengths(16, 8, 16, 8));
        lyricBorder.Padding(ThicknessHelper::FromUniformLength(12));
        Grid::SetRow(lyricBorder, 2);
        
        TextBlock lyricText;
        lyricText.Text(m_currentLyric);
        lyricText.FontSize(16);
        lyricText.Foreground(SolidColorBrush(Windows::UI::Colors::White()));
        lyricText.TextAlignment(TextAlignment::Center);
        lyricText.TextWrapping(TextWrapping::Wrap);
        lyricText.MaxHeight(80);
        
        lyricBorder.Child(lyricText);
        
        // 统计区域
        StackPanel statsPanel;
        statsPanel.Orientation(Orientation::Horizontal);
        statsPanel.HorizontalAlignment(HorizontalAlignment::Center);
        statsPanel.Margin(ThicknessHelper::FromLengths(0, 8, 0, 16));
        Grid::SetRow(statsPanel, 3);
        
        // 今日统计
        StackPanel todayPanel;
        todayPanel.Orientation(Orientation::Vertical);
        todayPanel.Margin(ThicknessHelper::FromLengths(16, 0, 16, 0));
        
        TextBlock todayLabel;
        todayLabel.Text(L"今日");
        todayLabel.FontSize(10);
        todayLabel.Foreground(SolidColorBrush(Windows::UI::ColorHelper::FromArgb(100, 255, 255, 255)));
        todayLabel.HorizontalAlignment(HorizontalAlignment::Center);
        
        TextBlock todayCount;
        todayCount.Text(L"0");
        todayCount.FontSize(24);
        todayCount.FontWeight(Windows::UI::Text::FontWeights::Bold());
        todayCount.Foreground(SolidColorBrush(Windows::UI::ColorHelper::FromArgb(255, 96, 205, 255)));
        todayCount.HorizontalAlignment(HorizontalAlignment::Center);
        
        todayPanel.Children().Append(todayLabel);
        todayPanel.Children().Append(todayCount);
        
        // 分隔线
        Border separator;
        separator.Width(1);
        separator.Height(32);
        separator.Background(SolidColorBrush(Windows::UI::ColorHelper::FromArgb(50, 255, 255, 255)));
        
        // 总计统计
        StackPanel totalPanel;
        totalPanel.Orientation(Orientation::Vertical);
        totalPanel.Margin(ThicknessHelper::FromLengths(16, 0, 16, 0));
        
        TextBlock totalLabel;
        totalLabel.Text(L"总计");
        totalLabel.FontSize(10);
        totalLabel.Foreground(SolidColorBrush(Windows::UI::ColorHelper::FromArgb(100, 255, 255, 255)));
        totalLabel.HorizontalAlignment(HorizontalAlignment::Center);
        
        TextBlock totalCount;
        totalCount.Text(L"0");
        totalCount.FontSize(24);
        totalCount.FontWeight(Windows::UI::Text::FontWeights::Bold());
        totalCount.Foreground(SolidColorBrush(Windows::UI::ColorHelper::FromArgb(255, 168, 85, 247)));
        totalCount.HorizontalAlignment(HorizontalAlignment::Center);
        
        totalPanel.Children().Append(totalLabel);
        totalPanel.Children().Append(totalCount);
        
        statsPanel.Children().Append(todayPanel);
        statsPanel.Children().Append(separator);
        statsPanel.Children().Append(totalPanel);
        
        // 添加到根容器
        rootGrid.Children().Append(titlePanel);
        rootGrid.Children().Append(progressPanel);
        rootGrid.Children().Append(lyricBorder);
        rootGrid.Children().Append(statsPanel);
        
        // 设置内容
        Content(rootGrid);
        m_rootGrid = rootGrid;
        
        // 设置窗口背景为透明
        this->SystemBackdrop(Microsoft::UI::Xaml::Media::MicaBackdrop());
    }
    
    void MainWindow::StartMonitoring()
    {
        // 初始化 Dispatcher
        m_dispatcher = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        
        // 启动 GSMTC 监控线程
        std::thread([this]() {
            auto sessionManager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
            
            if (sessionManager)
            {
                m_sessionManager = sessionManager;
                
                // 获取当前会话
                m_session = sessionManager.GetCurrentSession();
                
                // 订阅事件
                if (m_session)
                {
                    m_mediaPropertiesChangedToken = m_session.MediaPropertiesChanged(
                        [this](auto&&, auto&&) { OnMediaPropertiesChanged(m_session, {}); });
                    m_timelinePropertiesChangedToken = m_session.TimelinePropertiesChanged(
                        [this](auto&&, auto&&) { OnTimelinePropertiesChanged(m_session, {}); });
                }
                
                // 订阅会话变化
                sessionManager.CurrentSessionChanged([this](auto&&, auto&&) {
                    m_session = m_sessionManager.GetCurrentSession();
                    if (m_session)
                    {
                        m_mediaPropertiesChangedToken = m_session.MediaPropertiesChanged(
                            [this](auto&&, auto&&) { OnMediaPropertiesChanged(m_session, {}); });
                        m_timelinePropertiesChangedToken = m_session.TimelinePropertiesChanged(
                            [this](auto&&, auto&&) { OnTimelinePropertiesChanged(m_session, {}); });
                    }
                });
            }
        }).detach();
    }
    
    fire_and_forget MainWindow::OnMediaPropertiesChanged(
        GlobalSystemMediaTransportControlsSession const& session,
        MediaPropertiesChangedEventArgs const&)
    {
        auto properties = co_await session.TryGetMediaPropertiesAsync();
        if (properties)
        {
            co_await winrt::resume_foreground(m_dispatcher);
            SongTitle(properties.Title());
            SongArtist(properties.Artist());
        }
    }
    
    void MainWindow::OnTimelinePropertiesChanged(
        GlobalSystemMediaTransportControlsSession const& session,
        TimelinePropertiesChangedEventArgs const&)
    {
        auto timeline = session.GetTimelineProperties();
        if (timeline)
        {
            m_dispatcher.TryEnqueue([this, timeline]() {
                double current = timeline.Position().count() / 10000000.0;
                double total = timeline.EndTime().count() / 10000000.0;
                UpdateProgress(current, total);
            });
        }
    }
    
    void MainWindow::OnPlaybackInfoChanged(
        GlobalSystemMediaTransportControlsSession const& session,
        PlaybackInfoChangedEventArgs const&)
    {
        // 播放状态变化
    }
    
    void MainWindow::SongTitle(hstring const& value)
    {
        if (m_songTitle != value)
        {
            m_songTitle = value;
            m_propertyChanged(*this, Data::PropertyChangedEventArgs{ L"SongTitle" });
        }
    }
    
    void MainWindow::SongArtist(hstring const& value)
    {
        if (m_songArtist != value)
        {
            m_songArtist = value;
            m_propertyChanged(*this, Data::PropertyChangedEventArgs{ L"SongArtist" });
        }
    }
    
    void MainWindow::CurrentLyric(hstring const& value)
    {
        if (m_currentLyric != value)
        {
            m_currentLyric = value;
            m_propertyChanged(*this, Data::PropertyChangedEventArgs{ L"CurrentLyric" });
        }
    }
    
    void MainWindow::Progress(double value)
    {
        if (abs(m_progress - value) > 0.001)
        {
            m_progress = value;
            m_propertyChanged(*this, Data::PropertyChangedEventArgs{ L"Progress" });
        }
    }
    
    void MainWindow::TimeText(hstring const& value)
    {
        if (m_timeText != value)
        {
            m_timeText = value;
            m_propertyChanged(*this, Data::PropertyChangedEventArgs{ L"TimeText" });
        }
    }
    
    void MainWindow::UpdateSongInfo(hstring const& title, hstring const& artist)
    {
        SongTitle(title);
        SongArtist(artist);
    }
    
    void MainWindow::UpdateLyric(hstring const& lyric)
    {
        CurrentLyric(lyric);
    }
    
    void MainWindow::UpdateProgress(double current, double total)
    {
        Progress(total > 0 ? current / total : 0);
        
        int curMin = (int)current / 60;
        int curSec = (int)current % 60;
        int totMin = (int)total / 60;
        int totSec = (int)total % 60;
        
        wchar_t buffer[32];
        swprintf_s(buffer, L"%d:%02d / %d:%02d", curMin, curSec, totMin, totSec);
        TimeText(buffer);
    }
    
    event_token MainWindow::PropertyChanged(Data::PropertyChangedEventHandler const& handler)
    {
        return m_propertyChanged.add(handler);
    }
    
    void MainWindow::PropertyChanged(event_token const& token) noexcept
    {
        m_propertyChanged.remove(token);
    }
}
