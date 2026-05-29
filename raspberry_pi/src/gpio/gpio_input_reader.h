// src/gpio/gpio_input_reader.h
#pragma once

#include "../include/telemetry.hpp"

class GpioInputReader {
public:
    GpioInputReader();
    ~GpioInputReader();
    bool init(const scca::HardwareConfig& cfg);
    void close();
    bool read_digital_states(int& beep_up, int& beep_down, int& trim_release, int& override_state) const;
private:
    struct Impl;
    Impl* pimpl_ = nullptr;
};
