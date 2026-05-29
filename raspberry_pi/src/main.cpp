/*
 * src/main.cpp - refatorado entry point
 */

#include <iostream>
#include <string>
#include <unistd.h>

#include "../include/telemetry.hpp"
#include "net/udp_comm.h"
#include "gpio/gpio_input_reader.h"
#include "sensor/hx711_adapter.h"

static int getenv_int_or_default(const char* name, int default_value) {
    const char* value = std::getenv(name);
    if (!value || !*value) return default_value;
    try { return std::stoi(value); } catch (...) { return default_value; }
}

static scca::HardwareConfig read_hardware_config_from_env() {
    return scca::HardwareConfig{
        getenv_int_or_default("SCCA_USE_GPIO", 0) != 0,
        getenv_int_or_default("SCCA_GPIO_BEEP_UP", 17),
        getenv_int_or_default("SCCA_GPIO_BEEP_DOWN", 27),
        getenv_int_or_default("SCCA_GPIO_TRIM_RELEASE", 22),
        getenv_int_or_default("SCCA_GPIO_OVERRIDE", 23),
        getenv_int_or_default("SCCA_GPIO_HX711_DOUT", 5),
        getenv_int_or_default("SCCA_GPIO_HX711_SCK", 6),
        getenv_int_or_default("SCCA_GPIO_HX711_GAIN_PULSES", 1),
    };
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Uso: " << argv[0] << " <IP_dashboard> <porta_dashboard> [porta_local_escuta]" << std::endl;
        return 1;
    }

    const std::string dashboard_ip = argv[1];
    const int dashboard_port = std::atoi(argv[2]);
    const int local_listen_port = (argc >= 4) ? std::atoi(argv[3]) : 12346;

    int sock = -1;
    if (!scca::create_udp_socket_and_bind(sock, local_listen_port)) {
        std::perror("create/bind socket");
        return 1;
    }

    std::cout << "UDP CSV ativo. TX -> " << dashboard_ip << ":" << dashboard_port << " | RX em 0.0.0.0:" << local_listen_port << std::endl;

    const scca::HardwareConfig hw_cfg = read_hardware_config_from_env();

    GpioInputReader gpio_reader;
    HX711Adapter hx_adapter;

    bool gpio_mode_enabled = false;
    int last_load_cell = 0;

    if (hw_cfg.use_gpio) {
        gpio_mode_enabled = gpio_reader.init(hw_cfg);
        if (!gpio_mode_enabled) {
            std::cerr << "Falha ao iniciar leitura GPIO, mantendo modo simulado." << std::endl;
        } else {
            std::cout << "Modo GPIO ativo: beep_up=" << hw_cfg.beep_trim_up_pin << " beep_down=" << hw_cfg.beep_trim_down_pin << " trim_release=" << hw_cfg.trim_release_pin << " override=" << hw_cfg.override_pin << std::endl;
            if (!hx_adapter.init(hw_cfg.hx711_dout_pin, hw_cfg.hx711_sck_pin)) {
                std::cerr << "Aviso: nao foi possivel inicializar HX711, mantendo simulacao." << std::endl;
            }
        }
    }

    scca::DashboardControl control_state{false, false, 0};
    int sample_index = 0;

    while (true) {
        scca::poll_dashboard_commands_non_blocking(sock, control_state);

        int beep_up = 0, beep_down = 0, trim_release = 0, override_state = 1;
        int load_cell = 0;

        if (gpio_mode_enabled) {
            int hw_beep_up = 0, hw_beep_down = 0, hw_trim_release = 0, hw_override = 0;
            if (gpio_reader.read_digital_states(hw_beep_up, hw_beep_down, hw_trim_release, hw_override)) {
                beep_up = hw_beep_up;
                beep_down = hw_beep_down;
                trim_release = hw_trim_release;
                override_state = hw_override;
            }
            int val = 0;
            if (hx_adapter.try_read(val)) last_load_cell = val;
            load_cell = last_load_cell;
        } else {
            trim_release = control_state.autopilot_active ? 1 : 0;
            override_state = control_state.autopilot_active ? 0 : 1;
            if (control_state.autopilot_active) {
                if ((sample_index % 20) < 10) beep_up = 1; else beep_down = 1;
            }
            load_cell = control_state.transducer_position;
            if (control_state.hydraulic_failure) { load_cell = 0; beep_up = 0; beep_down = 1; }
        }

        scca::CollectiveTelemetry telemetry{beep_up, beep_down, trim_release, override_state, load_cell};
        const std::string payload = scca::build_collective_csv(telemetry);
        const ssize_t sent = scca::udp_send_payload(sock, dashboard_ip, dashboard_port, payload);
        if (sent < 0) std::perror("sendto"); else std::cout << "TX C: " << payload << std::endl;

        ++sample_index;
        usleep(50000);
    }

    return 0;
}
