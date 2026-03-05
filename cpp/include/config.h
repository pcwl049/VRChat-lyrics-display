#pragma once

#include "types.h"
#include <string>

namespace moekoe {

class Config {
public:
    static Config& instance();
    
    bool load(const std::string& path = "config.json");
    bool save(const std::string& path = "config.json");
    
    // 访问配置
    const std::string& oscIp() const { return osc_ip_; }
    int oscPort() const { return osc_port_; }
    const std::string& moekoeHost() const { return moekoe_host_; }
    int moekoePort() const { return moekoe_port_; }
    const std::string& displayMode() const { return display_mode_; }
    bool showLyrics() const { return show_lyrics_; }
    double lyricAdvance() const { return lyric_advance_; }
    double minSendInterval() const { return min_send_interval_; }
    
    // 修改配置
    void setDisplayMode(const std::string& mode) { display_mode_ = mode; }
    void setLyricAdvance(double advance) { lyric_advance_ = advance; }
    
private:
    Config() = default;
    
    std::string osc_ip_ = "127.0.0.1";
    int osc_port_ = 9000;
    std::string moekoe_host_ = "127.0.0.1";
    int moekoe_port_ = 6520;
    std::string display_mode_ = "full";
    bool show_lyrics_ = true;
    double lyric_advance_ = 0.0;
    double min_send_interval_ = 3.0;
};

} // namespace moekoe
