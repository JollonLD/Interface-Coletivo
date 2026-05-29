// src/gpio/gpio_input_reader.cpp
#include "gpio_input_reader.h"
#include <iostream>

#if __has_include(<gpiod.h>)
#include <gpiod.h>
#define SCCA_HAS_GPIOD 1
#else
#define SCCA_HAS_GPIOD 0
#endif

#if SCCA_HAS_GPIOD
struct GpioInputReader::Impl {
    Impl() = default;
    ~Impl() = default;
    scca::HardwareConfig cfg{};
    gpiod_chip* chip = nullptr;
    gpiod_line* beep_up_line = nullptr;
    gpiod_line* beep_down_line = nullptr;
    gpiod_line* trim_release_line = nullptr;
    gpiod_line* override_line = nullptr;
};

GpioInputReader::GpioInputReader() : pimpl_(new Impl()) {}
GpioInputReader::~GpioInputReader() { close(); delete pimpl_; }

bool GpioInputReader::init(const scca::HardwareConfig& cfg) {
    if (!pimpl_) return false;
    pimpl_->cfg = cfg;
    pimpl_->chip = gpiod_chip_open("/dev/gpiochip0");
    if (!pimpl_->chip) {
        std::perror("gpiod_chip_open");
        return false;
    }
    auto request_input_line = [&](int pin, gpiod_line*& out_line, const char* consumer) -> bool {
        out_line = gpiod_chip_get_line(pimpl_->chip, pin);
        if (!out_line) { std::cerr << "Erro ao obter linha GPIO " << pin << std::endl; return false; }
        if (gpiod_line_request_input(out_line, consumer) < 0) { std::cerr << "Erro ao configurar GPIO " << pin << " como entrada" << std::endl; return false; }
        return true;
    };
    if (!request_input_line(cfg.beep_trim_up_pin, pimpl_->beep_up_line, "scca_beep_up")) return false;
    if (!request_input_line(cfg.beep_trim_down_pin, pimpl_->beep_down_line, "scca_beep_down")) return false;
    if (!request_input_line(cfg.trim_release_pin, pimpl_->trim_release_line, "scca_trim_release")) return false;
    if (!request_input_line(cfg.override_pin, pimpl_->override_line, "scca_override")) return false;
    return true;
}

void GpioInputReader::close() {
    if (!pimpl_) return;
    auto release_line = [](gpiod_line*& line){ if (line) { gpiod_line_release(line); line = nullptr; } };
    release_line(pimpl_->beep_up_line);
    release_line(pimpl_->beep_down_line);
    release_line(pimpl_->trim_release_line);
    release_line(pimpl_->override_line);
    if (pimpl_->chip) { gpiod_chip_close(pimpl_->chip); pimpl_->chip = nullptr; }
}

bool GpioInputReader::read_digital_states(int& beep_up, int& beep_down, int& trim_release, int& override_state) const {
    if (!pimpl_) return false;
    if (!pimpl_->beep_up_line || !pimpl_->beep_down_line || !pimpl_->trim_release_line || !pimpl_->override_line) return false;
    auto read_line_value = [](gpiod_line* line)->int { return gpiod_line_get_value(line); };
    beep_up = read_line_value(pimpl_->beep_up_line);
    beep_down = read_line_value(pimpl_->beep_down_line);
    trim_release = read_line_value(pimpl_->trim_release_line);
    override_state = read_line_value(pimpl_->override_line);
    if (beep_up < 0 || beep_down < 0 || trim_release < 0 || override_state < 0) return false;
    beep_up = (beep_up != 0) ? 1 : 0;
    beep_down = (beep_down != 0) ? 1 : 0;
    trim_release = (trim_release != 0) ? 1 : 0;
    override_state = (override_state != 0) ? 1 : 0;
    return true;
}

#else

GpioInputReader::GpioInputReader() {}
GpioInputReader::~GpioInputReader() {}
bool GpioInputReader::init(const scca::HardwareConfig&) { return false; }
void GpioInputReader::close() {}
bool GpioInputReader::read_digital_states(int&, int&, int&, int&) const { return false; }

#endif
