#pragma once
#include <string>
#include <vector>
#include "../moekoe_ws.h"

// Search lyrics from QQ Music API
std::vector<moekoe::LyricLine> SearchLyricsForQQMusic(const std::wstring& title, const std::wstring& artist);

// Search lyrics from Qishui Music (汽水音乐) API
std::vector<moekoe::LyricLine> SearchLyricsForQishuiMusic(const std::wstring& title, const std::wstring& artist);
