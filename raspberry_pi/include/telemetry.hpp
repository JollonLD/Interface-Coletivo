// telemetry.hpp - Estruturas e parsing CSV
#pragma once

#include <string>
#include <vector>

namespace scca {

struct CollectiveTelemetry {
    int beep_trim_up;
    int beep_trim_down;
    int trim_release;
    int override_state;
    int load_cell;
};

struct DashboardControl {
    bool autopilot_active;
    bool hydraulic_failure;
    int transducer_position;
};

struct HardwareConfig {
    bool use_gpio;
    int beep_trim_up_pin;
    int beep_trim_down_pin;
    int trim_release_pin;
    int override_pin;
    int hx711_dout_pin;
    int hx711_sck_pin;
    int hx711_gain_pulses;
};

static inline std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> out;
    std::string item;
    size_t start = 0;
    for (size_t i = 0; i <= line.size(); ++i) {
        if (i == line.size() || line[i] == ',') {
            out.push_back(line.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

static inline std::string build_collective_csv(const CollectiveTelemetry& t) {
    std::string s = "C,";
    s += std::to_string(t.beep_trim_up) + ",";
    s += std::to_string(t.beep_trim_down) + ",";
    s += std::to_string(t.trim_release) + ",";
    s += std::to_string(t.override_state) + ",";
    s += std::to_string(t.load_cell);
    return s;
}

static inline bool parse_dashboard_csv(const std::string& payload, DashboardControl& out) {
    const std::vector<std::string> parts = split_csv(payload);
    if (parts.size() != 4 || parts[0] != "P") return false;
    try {
        const int autopilot = std::stoi(parts[1]);
        const int hydraulic = std::stoi(parts[2]);
        const int transducer = std::stoi(parts[3]);
        if ((autopilot != 0 && autopilot != 1) || (hydraulic != 0 && hydraulic != 1)) return false;
        out.autopilot_active = (autopilot == 1);
        out.hydraulic_failure = (hydraulic == 1);
        out.transducer_position = transducer;
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace scca
