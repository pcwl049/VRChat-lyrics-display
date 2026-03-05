#include "config.h"
#include <fstream>
#include <nlohmann/json.hpp>

namespace moekoe {

Config& Config::instance() {
    static Config instance;
    return instance;
}

bool Config::load(const std::string& path) {
    try {
        std::ifstream file(path);
        if (!file.is_open()) return false;
        
        nlohmann::json json;
        file >> json;
        
        if (json.contains("osc")) {
            if (json["osc"].contains("ip")) osc_ip_ = json["osc"]["ip"];
            if (json["osc"].contains("port")) osc_port_ = json["osc"]["port"];
        }
        
        if (json.contains("moekoe")) {
            if (json["moekoe"].contains("host")) moekoe_host_ = json["moekoe"]["host"];
            if (json["moekoe"].contains("port")) moekoe_port_ = json["moekoe"]["port"];
        }
        
        if (json.contains("display_mode")) display_mode_ = json["display_mode"];
        if (json.contains("show_lyrics")) show_lyrics_ = json["show_lyrics"];
        if (json.contains("min_send_interval")) min_send_interval_ = json["min_send_interval"];
        
        if (json.contains("lyric_settings")) {
            if (json["lyric_settings"].contains("advance")) {
                lyric_advance_ = json["lyric_settings"]["advance"];
            }
        }
        
        return true;
    } catch (...) {
        return false;
    }
}

bool Config::save(const std::string& path) {
    try {
        nlohmann::json json;
        
        json["osc"]["ip"] = osc_ip_;
        json["osc"]["port"] = osc_port_;
        json["moekoe"]["host"] = moekoe_host_;
        json["moekoe"]["port"] = moekoe_port_;
        json["display_mode"] = display_mode_;
        json["show_lyrics"] = show_lyrics_;
        json["min_send_interval"] = min_send_interval_;
        json["lyric_settings"]["advance"] = lyric_advance_;
        
        std::ofstream file(path);
        file << json.dump(4);
        
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace moekoe
