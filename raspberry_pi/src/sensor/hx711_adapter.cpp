// src/sensor/hx711_adapter.cpp
#include "hx711_adapter.h"
#include <iostream>

#if __has_include(<hx711/SimpleHX711.h>)
#include <hx711/SimpleHX711.h>
using namespace HX711;
#define HAVE_HX711 1
#else
#define HAVE_HX711 0
#endif

struct HX711Adapter::Impl {
#if HAVE_HX711
    SimpleHX711* dev = nullptr;
#else
    void* dev = nullptr;
#endif
    int last_val = 0;
};

HX711Adapter::HX711Adapter() : pimpl_(new Impl()) {}
HX711Adapter::~HX711Adapter() {
    if (!pimpl_) return;
#if HAVE_HX711
    if (pimpl_->dev) { delete pimpl_->dev; pimpl_->dev = nullptr; }
#endif
    delete pimpl_;
}

bool HX711Adapter::init(int dataPin, int clockPin) {
#if HAVE_HX711
    try {
        pimpl_->dev = new SimpleHX711(dataPin, clockPin, Value(1), Value(0));
        return true;
    } catch (const std::exception& ex) {
        std::cerr << "HX711 init exception: " << ex.what() << std::endl;
        return false;
    }
#else
    (void)dataPin; (void)clockPin;
    return false;
#endif
}

bool HX711Adapter::try_read(int& out_value) {
    if (!pimpl_) return false;
#if HAVE_HX711
    if (!pimpl_->dev) return false;
    try {
        if (pimpl_->dev->isReady()) {
            auto v = pimpl_->dev->read();
            pimpl_->last_val = static_cast<int>(v);
            out_value = pimpl_->last_val;
            return true;
        }
        return false;
    } catch (const std::exception& ex) {
        std::cerr << "HX711 read error: " << ex.what() << std::endl;
        return false;
    }
#else
    (void)out_value;
    return false;
#endif
}
