// src/sensor/hx711_adapter.h
#pragma once

#include "../include/telemetry.hpp"

class HX711Adapter {
public:
    HX711Adapter();
    ~HX711Adapter();
    bool init(int dataPin, int clockPin);
    bool try_read(int& out_value);
private:
    struct Impl;
    Impl* pimpl_;
};
