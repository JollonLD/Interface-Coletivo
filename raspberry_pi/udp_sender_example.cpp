/*
 * PROTOCOLO UDP CSV - RASPBERRY PI 5
 * ==================================
 *
 * Fluxo Raspberry -> Dashboard (telemetria):
 *   C,<beep_trim_up>,<beep_trim_down>,<trim_release>,<override>,<load_cell>
 *
 * Fluxo Dashboard -> Raspberry (controle):
 *   P,<autopilot_active>,<hydraulic_failure>,<transducer_position>
 *
 * Compilar:
 *   g++ -std=c++17 -O2 -o udp_csv udp_sender_example.cpp
 *
 * Executar:
 *   ./udp_csv <IP_dashboard> <porta_dashboard> [porta_local_escuta]
 *
 * Exemplo:
 *   ./udp_csv 192.168.1.100 12345 12346
 */

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#if __has_include(<gpiod.h>)
#include <gpiod.h>
#define SCCA_HAS_GPIOD 1
#else
#define SCCA_HAS_GPIOD 0
#endif

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

static int getenv_int_or_default(const char* name, int default_value) {
    const char* value = std::getenv(name);
    if (!value || !*value) {
        return default_value;
    }
    try {
        return std::stoi(value);
    } catch (...) {
        return default_value;
    }
}

static HardwareConfig read_hardware_config_from_env() {
    return HardwareConfig{
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

#if SCCA_HAS_GPIOD
class GpioInputReader {
public:
    GpioInputReader() = default;

    ~GpioInputReader() {
        close();
    }

    bool init(const HardwareConfig& cfg) {
        close();

        cfg_ = cfg;
        if (cfg_.hx711_gain_pulses < 1 || cfg_.hx711_gain_pulses > 3) {
            cfg_.hx711_gain_pulses = 1;
        }

        chip_ = gpiod_chip_open("/dev/gpiochip0");
        if (!chip_) {
            std::perror("gpiod_chip_open");
            return false;
        }

        if (!request_input_line(cfg_.beep_trim_up_pin, beep_up_line_, "scca_beep_up")) return false;
        if (!request_input_line(cfg_.beep_trim_down_pin, beep_down_line_, "scca_beep_down")) return false;
        if (!request_input_line(cfg_.trim_release_pin, trim_release_line_, "scca_trim_release")) return false;
        if (!request_input_line(cfg_.override_pin, override_line_, "scca_override")) return false;
        if (!request_input_line(cfg_.hx711_dout_pin, hx711_dout_line_, "scca_hx711_dout")) return false;
        if (!request_output_line(cfg_.hx711_sck_pin, hx711_sck_line_, "scca_hx711_sck")) return false;

        return true;
    }

    void close() {
        release_line(beep_up_line_);
        release_line(beep_down_line_);
        release_line(trim_release_line_);
        release_line(override_line_);
        release_line(hx711_dout_line_);
        release_line(hx711_sck_line_);

        if (chip_) {
            gpiod_chip_close(chip_);
            chip_ = nullptr;
        }
    }

    bool read_digital_states(int& beep_up, int& beep_down, int& trim_release, int& override_state) const {
        if (!beep_up_line_ || !beep_down_line_ || !trim_release_line_ || !override_line_) {
            return false;
        }

        beep_up = read_line_value(beep_up_line_);
        beep_down = read_line_value(beep_down_line_);
        trim_release = read_line_value(trim_release_line_);
        override_state = read_line_value(override_line_);

        if (beep_up < 0 || beep_down < 0 || trim_release < 0 || override_state < 0) {
            return false;
        }

        beep_up = (beep_up != 0) ? 1 : 0;
        beep_down = (beep_down != 0) ? 1 : 0;
        trim_release = (trim_release != 0) ? 1 : 0;
        override_state = (override_state != 0) ? 1 : 0;

        return true;
    }

    bool read_hx711_value(int& out_value) {
        if (!hx711_dout_line_ || !hx711_sck_line_) {
            return false;
        }

        // Se DOUT estiver alto, o HX711 ainda nao tem dado pronto.
        const int ready = read_line_value(hx711_dout_line_);
        if (ready < 0 || ready != 0) {
            return false;
        }

        int32_t raw = 0;
        for (int i = 0; i < 24; ++i) {
            if (!set_sck(1)) return false;
            usleep(1);

            const int bit = read_line_value(hx711_dout_line_);
            if (bit < 0) return false;
            raw = static_cast<int32_t>((raw << 1) | (bit & 0x01));

            if (!set_sck(0)) return false;
            usleep(1);
        }

        // Pulso(s) extra para selecionar ganho/canal do proximo sample.
        for (int i = 0; i < cfg_.hx711_gain_pulses; ++i) {
            if (!set_sck(1)) return false;
            usleep(1);
            if (!set_sck(0)) return false;
            usleep(1);
        }

        // Extensao de sinal do valor 24 bits.
        if (raw & 0x00800000) {
            raw |= static_cast<int32_t>(0xFF000000);
        }

        out_value = raw;
        return true;
    }

private:
    static void release_line(gpiod_line*& line) {
        if (line) {
            gpiod_line_release(line);
            line = nullptr;
        }
    }

    bool request_input_line(int pin, gpiod_line*& out_line, const char* consumer) {
        out_line = gpiod_chip_get_line(chip_, pin);
        if (!out_line) {
            std::cerr << "Erro ao obter linha GPIO " << pin << std::endl;
            return false;
        }
        if (gpiod_line_request_input(out_line, consumer) < 0) {
            std::cerr << "Erro ao configurar GPIO " << pin << " como entrada" << std::endl;
            return false;
        }
        return true;
    }

    bool request_output_line(int pin, gpiod_line*& out_line, const char* consumer) {
        out_line = gpiod_chip_get_line(chip_, pin);
        if (!out_line) {
            std::cerr << "Erro ao obter linha GPIO " << pin << std::endl;
            return false;
        }
        if (gpiod_line_request_output(out_line, consumer, 0) < 0) {
            std::cerr << "Erro ao configurar GPIO " << pin << " como saida" << std::endl;
            return false;
        }
        return true;
    }

    static int read_line_value(gpiod_line* line) {
        return gpiod_line_get_value(line);
    }

    bool set_sck(int value) {
        return gpiod_line_set_value(hx711_sck_line_, value) == 0;
    }

    HardwareConfig cfg_{};
    gpiod_chip* chip_ = nullptr;
    gpiod_line* beep_up_line_ = nullptr;
    gpiod_line* beep_down_line_ = nullptr;
    gpiod_line* trim_release_line_ = nullptr;
    gpiod_line* override_line_ = nullptr;
    gpiod_line* hx711_dout_line_ = nullptr;
    gpiod_line* hx711_sck_line_ = nullptr;
};
#endif

static std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> out;
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, ',')) {
        out.push_back(item);
    }
    return out;
}

static std::string build_collective_csv(const CollectiveTelemetry& t) {
    std::ostringstream oss;
    oss << "C,"
        << t.beep_trim_up << ","
        << t.beep_trim_down << ","
        << t.trim_release << ","
        << t.override_state << ","
        << t.load_cell;
    return oss.str();
}

static bool parse_dashboard_csv(const std::string& payload, DashboardControl& out) {
    const std::vector<std::string> parts = split_csv(payload);
    if (parts.size() != 4 || parts[0] != "P") {
        return false;
    }

    try {
        const int autopilot = std::stoi(parts[1]);
        const int hydraulic = std::stoi(parts[2]);
        const int transducer = std::stoi(parts[3]);

        if ((autopilot != 0 && autopilot != 1) || (hydraulic != 0 && hydraulic != 1)) {
            return false;
        }

        out.autopilot_active = (autopilot == 1);
        out.hydraulic_failure = (hydraulic == 1);
        out.transducer_position = transducer;
        return true;
    } catch (...) {
        return false;
    }
}

static bool make_socket_non_blocking(int sock) {
    const int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK) == 0;
}

static bool bind_local_port(int sock, int local_port) {
    sockaddr_in local_addr;
    std::memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(static_cast<uint16_t>(local_port));

    return bind(sock, reinterpret_cast<sockaddr*>(&local_addr), sizeof(local_addr)) == 0;
}

static void poll_dashboard_commands_non_blocking(int sock, DashboardControl& control_state) {
    char buffer[256];

    while (true) {
        sockaddr_in src_addr;
        socklen_t src_len = sizeof(src_addr);
        const ssize_t bytes = recvfrom(
            sock,
            buffer,
            sizeof(buffer) - 1,
            0,
            reinterpret_cast<sockaddr*>(&src_addr),
            &src_len
        );

        if (bytes < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            std::perror("recvfrom");
            return;
        }

        buffer[bytes] = '\0';
        DashboardControl parsed;
        const std::string payload(buffer);
        if (parse_dashboard_csv(payload, parsed)) {
            control_state = parsed;
            std::cout
                << "RX P: autopilot=" << (control_state.autopilot_active ? 1 : 0)
                << " hydraulic_failure=" << (control_state.hydraulic_failure ? 1 : 0)
                << " transducer_position=" << control_state.transducer_position
                << std::endl;
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Uso: " << argv[0] << " <IP_dashboard> <porta_dashboard> [porta_local_escuta]" << std::endl;
        std::cerr << "Exemplo: " << argv[0] << " 192.168.1.100 12345 12346" << std::endl;
        return 1;
    }

    const char* dashboard_ip = argv[1];
    const int dashboard_port = std::atoi(argv[2]);
    const int local_listen_port = (argc >= 4) ? std::atoi(argv[3]) : 12346;

    const int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::perror("socket");
        return 1;
    }

    if (!bind_local_port(sock, local_listen_port)) {
        std::perror("bind");
        close(sock);
        return 1;
    }

    if (!make_socket_non_blocking(sock)) {
        std::perror("fcntl(O_NONBLOCK)");
        close(sock);
        return 1;
    }

    sockaddr_in dashboard_addr;
    std::memset(&dashboard_addr, 0, sizeof(dashboard_addr));
    dashboard_addr.sin_family = AF_INET;
    dashboard_addr.sin_port = htons(static_cast<uint16_t>(dashboard_port));

    if (inet_aton(dashboard_ip, &dashboard_addr.sin_addr) == 0) {
        std::cerr << "IP invalido: " << dashboard_ip << std::endl;
        close(sock);
        return 1;
    }

    std::cout << "UDP CSV ativo. TX -> " << dashboard_ip << ":" << dashboard_port
              << " | RX em 0.0.0.0:" << local_listen_port << std::endl;

    const HardwareConfig hw_cfg = read_hardware_config_from_env();

#if SCCA_HAS_GPIOD
    GpioInputReader gpio_reader;
    bool gpio_mode_enabled = false;
    if (hw_cfg.use_gpio) {
        gpio_mode_enabled = gpio_reader.init(hw_cfg);
        if (!gpio_mode_enabled) {
            std::cerr << "Falha ao iniciar leitura GPIO, mantendo modo simulado." << std::endl;
        } else {
            std::cout
                << "Modo GPIO ativo:"
                << " beep_up=" << hw_cfg.beep_trim_up_pin
                << " beep_down=" << hw_cfg.beep_trim_down_pin
                << " trim_release=" << hw_cfg.trim_release_pin
                << " override=" << hw_cfg.override_pin
                << " hx711_dout=" << hw_cfg.hx711_dout_pin
                << " hx711_sck=" << hw_cfg.hx711_sck_pin
                << std::endl;
        }
    }
#else
    bool gpio_mode_enabled = false;
    if (hw_cfg.use_gpio) {
        std::cerr << "SCCA_USE_GPIO=1, mas libgpiod nao esta disponivel em tempo de compilacao." << std::endl;
        std::cerr << "Instale libgpiod-dev e compile novamente para habilitar GPIO real." << std::endl;
    }
#endif

    DashboardControl control_state{false, false, 0};
    int sample_index = 0;
    int last_load_cell = 0;

    // Loop de baixa latencia: 50 ms (dentro da faixa especificada 50-150 ms).
    while (true) {
        poll_dashboard_commands_non_blocking(sock, control_state);

        int beep_up = 0;
        int beep_down = 0;
        int trim_release = 0;
        int override_state = 1;
        int load_cell = 0;

        if (gpio_mode_enabled) {
#if SCCA_HAS_GPIOD
            int hw_beep_up = 0;
            int hw_beep_down = 0;
            int hw_trim_release = 0;
            int hw_override = 0;

            if (gpio_reader.read_digital_states(hw_beep_up, hw_beep_down, hw_trim_release, hw_override)) {
                beep_up = hw_beep_up;
                beep_down = hw_beep_down;
                trim_release = hw_trim_release;
                override_state = hw_override;
            }

            int hw_load = 0;
            if (gpio_reader.read_hx711_value(hw_load)) {
                last_load_cell = hw_load;
            }
            load_cell = last_load_cell;
#endif
        } else {
            trim_release = control_state.autopilot_active ? 1 : 0;
            override_state = control_state.autopilot_active ? 0 : 1;

            if (control_state.autopilot_active) {
                if ((sample_index % 20) < 10) {
                    beep_up = 1;
                } else {
                    beep_down = 1;
                }
            }

            load_cell = control_state.transducer_position;
            if (control_state.hydraulic_failure) {
                load_cell = 0;
                beep_up = 0;
                beep_down = 1;
            }
        }

        const CollectiveTelemetry telemetry{
            beep_up,
            beep_down,
            trim_release,
            override_state,
            load_cell,
        };

        const std::string payload = build_collective_csv(telemetry);
        const ssize_t sent = sendto(
            sock,
            payload.c_str(),
            payload.size(),
            0,
            reinterpret_cast<sockaddr*>(&dashboard_addr),
            sizeof(dashboard_addr)
        );

        if (sent < 0) {
            std::perror("sendto");
        } else {
            std::cout << "TX C: " << payload << std::endl;
        }

        ++sample_index;
        usleep(50000);
    }

    close(sock);
    return 0;
}
