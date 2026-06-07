/*
 * PROTOCOLO UDP CSV - RASPBERRY PI 5 COM MÁQUINA DE ESTADOS
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
#include <cmath>

#if __has_include(<gpiod.h>)
#include <gpiod.h>
#define SCCA_HAS_GPIOD 1
#else
#define SCCA_HAS_GPIOD 0
#endif

// Definição dos estados principais e especializações
enum class SuperEstado { 
    MODO_MANUAL, 
    PILOTO_AUTOMATICO 
};

enum class SubEstadoManual { 
    DEFAULT, 
    BEEP_TRIM_UP, 
    BEEP_TRIM_DOWN, 
    TRIM_PRESSED, 
    OVERRIDE_MANUAL 
};

enum class SubEstadoAutopilot { 
    AGINDO, 
    override_autopilot 
};

// Variáveis com os valores lidos
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
    
    // Variáveis com pinos de entrada
    int beep_trim_up_pin;
    int beep_trim_down_pin;
    int trim_release_pin;
    int override_pin;
    int limit_switch_top_pin;
    int limit_switch_bottom_pin;
    
    // Pinos para célula de carga (Realocados devido ao conflito com o motor)
    int hx711_dout_pin;
    int hx711_sck_pin;
    int hx711_gain_pulses;

    // Pinos para controle do driver do motor (Novas saídas alocadas)
    int motor_step_pin;
    int motor_dir_pin;
    int motor_en_pin;

    // Variáveis de ajustes
    int load_cell_override_threshold;
    int load_cell_return_threshold;
    int beep_trim_step;
    double virtual_spring_k;
};

// Função auxiliar para ler variáveis do ambiente
static int getenv_int_or_default(const char* name, int default_value) {
    const char* value = std::getenv(name);
    if (!value || !*value) return default_value;
    try { return std::stoi(value); } catch (...) { return default_value; }
}

static HardwareConfig read_hardware_config_from_env() {
    return HardwareConfig{
        getenv_int_or_default("SCCA_USE_GPIO", 0) != 0,
        
        // Pinos de Entrada
        getenv_int_or_default("SCCA_GPIO_BEEP_UP", 17),
        getenv_int_or_default("SCCA_GPIO_BEEP_DOWN", 27),
        getenv_int_or_default("SCCA_GPIO_TRIM_RELEASE", 22),
        getenv_int_or_default("SCCA_GPIO_OVERRIDE", 23),
        getenv_int_or_default("SCCA_GPIO_LIMIT_TOP", 24),     
        getenv_int_or_default("SCCA_GPIO_LIMIT_BOTTOM", 25),  
        
        // Pinos da célula de carga
        getenv_int_or_default("SCCA_GPIO_HX711_DOUT", 16),
        getenv_int_or_default("SCCA_GPIO_HX711_SCK", 26),
        getenv_int_or_default("SCCA_GPIO_HX711_GAIN_PULSES", 1),

        // Pinos para controle do driver do motor
        getenv_int_or_default("SCCA_GPIO_MOTOR_STEP", 5),
        getenv_int_or_default("SCCA_GPIO_MOTOR_DIR", 6),
        getenv_int_or_default("SCCA_GPIO_MOTOR_EN", 13),

        // Calibrações padrão
        getenv_int_or_default("SCCA_CALIB_OVERRIDE_THR", 150), 
        getenv_int_or_default("SCCA_CALIB_RETURN_THR", 40),    
        getenv_int_or_default("SCCA_CALIB_BEEP_STEP", 5),      
        static_cast<double>(getenv_int_or_default("SCCA_CALIB_SPRING_K", 12)) / 10.0 
    };
}

// Classe para gerenciar hardware (GPIO)
#if SCCA_HAS_GPIOD
class GpioHardwareManager {
public:
    GpioHardwareManager() = default;
    ~GpioHardwareManager() { close(); }

    bool init(const HardwareConfig& cfg) {
        close();
        cfg_ = cfg;

        chip_ = gpiod_chip_open("/dev/gpiochip4");
        if (!chip_) {
            std::perror("gpiod_chip_open (/dev/gpiochip4)");
            return false;
        }

        // Configuração das Entradas com PULL-UP
        gpiod_line_settings* settings_in = gpiod_line_settings_new();
        gpiod_line_settings_set_direction(settings_in, GPIOD_LINE_DIRECTION_INPUT);
        gpiod_line_settings_set_bias(settings_in, GPIOD_LINE_BIAS_PULL_UP);

        gpiod_line_config* line_cfg_in = gpiod_line_config_new();
        std::vector<unsigned int> input_offsets = {
            static_cast<unsigned int>(cfg_.beep_trim_up_pin),
            static_cast<unsigned int>(cfg_.beep_trim_down_pin),
            static_cast<unsigned int>(cfg_.trim_release_pin),
            static_cast<unsigned int>(cfg_.override_pin),
            static_cast<unsigned int>(cfg_.limit_switch_top_pin),
            static_cast<unsigned int>(cfg_.limit_switch_bottom_pin),
            static_cast<unsigned int>(cfg_.hx711_dout_pin)
        };
        
        for (unsigned int offset : input_offsets) {
            gpiod_line_config_add_line_settings(line_cfg_in, &offset, 1, settings_in);
        }

        gpiod_request_config* req_cfg_in = gpiod_request_config_new();
        gpiod_request_config_set_consumer(req_cfg_in, "scca_inputs");
        inputs_req_ = gpiod_chip_request_lines(chip_, req_cfg_in, line_cfg_in);

        gpiod_line_settings_free(settings_in);
        gpiod_line_config_free(line_cfg_in);
        gpiod_request_config_free(req_cfg_in);

        if (!inputs_req_) return false;

        // Configuração das Saídas (SCK e Driver do Motor)
        gpiod_line_settings* settings_out = gpiod_line_settings_new();
        gpiod_line_settings_set_direction(settings_out, GPIOD_LINE_DIRECTION_OUTPUT);
        gpiod_line_settings_set_output_value(settings_out, GPIOD_LINE_VALUE_INACTIVE);

        gpiod_line_config* line_cfg_out = gpiod_line_config_new();
        std::vector<unsigned int> output_offsets = {
            static_cast<unsigned int>(cfg_.hx711_sck_pin),
            static_cast<unsigned int>(cfg_.motor_step_pin),
            static_cast<unsigned int>(cfg_.motor_dir_pin),
            static_cast<unsigned int>(cfg_.motor_en_pin)
        };

        for (unsigned int offset : output_offsets) {
            gpiod_line_config_add_line_settings(line_cfg_out, &offset, 1, settings_out);
        }

        gpiod_request_config* req_cfg_out = gpiod_request_config_new();
        gpiod_request_config_set_consumer(req_cfg_out, "scca_outputs");
        outputs_req_ = gpiod_chip_request_lines(chip_, req_cfg_out, line_cfg_out);

        gpiod_line_settings_free(settings_out);
        gpiod_line_config_free(line_cfg_out);
        gpiod_request_config_free(req_cfg_out);

        return (outputs_req_ != nullptr);
    }

    void close() {
        if (inputs_req_) { gpiod_line_request_release(inputs_req_); inputs_req_ = nullptr; }
        if (outputs_req_) { gpiod_line_request_release(outputs_req_); outputs_req_ = nullptr; }
        if (chip_) { gpiod_chip_close(chip_); chip_ = nullptr; }
    }

    bool read_inputs(int& b_up, int& b_down, int& t_rel, int& ovr, int& lim_t, int& lim_b) const {
        if (!inputs_req_) return false;

        b_up  = gpiod_line_request_get_value(inputs_req_, cfg_.beep_trim_up_pin);
        b_down= gpiod_line_request_get_value(inputs_req_, cfg_.beep_trim_down_pin);
        t_rel = gpiod_line_request_get_value(inputs_req_, cfg_.trim_release_pin);
        ovr   = gpiod_line_request_get_value(inputs_req_, cfg_.override_pin);
        lim_t = gpiod_line_request_get_value(inputs_req_, cfg_.limit_switch_top_pin);
        lim_b = gpiod_line_request_get_value(inputs_req_, cfg_.limit_switch_bottom_pin);

        // Lógica Inversa (GND ativo)
        b_up  = (b_up == GPIOD_LINE_VALUE_INACTIVE) ? 1 : 0;
        b_down= (b_down == GPIOD_LINE_VALUE_INACTIVE) ? 1 : 0;
        t_rel = (t_rel == GPIOD_LINE_VALUE_INACTIVE) ? 1 : 0;
        ovr   = (ovr == GPIOD_LINE_VALUE_INACTIVE) ? 1 : 0;
        lim_t = (lim_t == GPIOD_LINE_VALUE_INACTIVE) ? 1 : 0;
        lim_b = (lim_b == GPIOD_LINE_VALUE_INACTIVE) ? 1 : 0;

        return true;
    }

    bool read_hx711(int& out_value) {
        if (!inputs_req_ || !outputs_req_) return false;
        if (gpiod_line_request_get_value(inputs_req_, cfg_.hx711_dout_pin) != GPIOD_LINE_VALUE_INACTIVE) return false;

        int32_t raw = 0;
        for (int i = 0; i < 24; ++i) {
            gpiod_line_request_set_value(outputs_req_, cfg_.hx711_sck_pin, GPIOD_LINE_VALUE_ACTIVE);
            usleep(1);
            int bit = gpiod_line_request_get_value(inputs_req_, cfg_.hx711_dout_pin);
            raw = (raw << 1) | ((bit == GPIOD_LINE_VALUE_ACTIVE) ? 1 : 0);
            gpiod_line_request_set_value(outputs_req_, cfg_.hx711_sck_pin, GPIOD_LINE_VALUE_INACTIVE);
            usleep(1);
        }
        for (int i = 0; i < cfg_.hx711_gain_pulses; ++i) {
            gpiod_line_request_set_value(outputs_req_, cfg_.hx711_sck_pin, GPIOD_LINE_VALUE_ACTIVE); usleep(1);
            gpiod_line_request_set_value(outputs_req_, cfg_.hx711_sck_pin, GPIOD_LINE_VALUE_INACTIVE); usleep(1);
        }
        if (raw & 0x00800000) raw |= static_cast<int32_t>(0xFF000000);
        out_value = raw;
        return true;
    }

    // Função para atualizar os sinais físicos enviados ao Driver do Motor
    void write_motor_signals(bool enable, bool direction_up, bool generate_step) {
        if (!outputs_req_) return;
        
        // Controle do pino ENABLE - Ativo em nível BAIXO
        gpiod_line_request_set_value(outputs_req_, cfg_.motor_en_pin, 
            enable ? GPIOD_LINE_VALUE_INACTIVE : GPIOD_LINE_VALUE_ACTIVE);
        
        if (enable) {
            // Define o sentido de rotação baseado no pino DIR
            gpiod_line_request_set_value(outputs_req_, cfg_.motor_dir_pin, 
                direction_up ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE);
            
            // Emite um pulso quadrado de transição de passo no pino STEP
            if (generate_step) {
                gpiod_line_request_set_value(outputs_req_, cfg_.motor_step_pin, GPIOD_LINE_VALUE_ACTIVE);
                usleep(5); // Tempo de pulso mínimo do driver
                gpiod_line_request_set_value(outputs_req_, cfg_.motor_step_pin, GPIOD_LINE_VALUE_INACTIVE);
            }
        }
    }

private:
    HardwareConfig cfg_{};
    gpiod_chip* chip_ = nullptr;
    gpiod_line_request* inputs_req_ = nullptr;
    gpiod_line_request* outputs_req_ = nullptr;
};
#endif

// Função para separar CSV
static std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> out;
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, ',')) out.push_back(item);
    return out;
}

static std::string build_collective_csv(const CollectiveTelemetry& t) {
    std::ostringstream oss;
    oss << "C," << t.beep_trim_up << "," << t.beep_trim_down << ","
        << t.trim_release << "," << t.override_state << "," << t.load_cell;
    return oss.str();
}

static bool parse_dashboard_csv(const std::string& payload, DashboardControl& out) {
    const std::vector<std::string> parts = split_csv(payload);
    if (parts.size() != 4 || parts[0] != "P") return false;
    try {
        int autopilot = std::stoi(parts[1]);
        int hydraulic = std::stoi(parts[2]);
        out.autopilot_active = (autopilot == 1);
        out.hydraulic_failure = (hydraulic == 1);
        out.transducer_position = std::stoi(parts[3]);
        return true;
    } catch (...) { return false; }
}

static bool make_socket_non_blocking(int sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK) == 0;
}

static void poll_dashboard_commands(int sock, DashboardControl& control_state) {
    char buffer[256];
    while (true) {
        sockaddr_in src_addr;
        socklen_t src_len = sizeof(src_addr);
        ssize_t bytes = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, reinterpret_cast<sockaddr*>(&src_addr), &src_len);
        if (bytes < 0) return;
        buffer[bytes] = '\0';
        DashboardControl parsed;
        if (parse_dashboard_csv(std::string(buffer), parsed)) {
            control_state = parsed;
        }
    }
}

// Gerenciamento da máquina de estados
int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Uso: " << argv[0] << " <IP_dashboard> <porta_dashboard> [porta_local]\n";
        return 1;
    }

    const char* dashboard_ip = argv[1];
    int dashboard_port = std::atoi(argv[2]);
    int local_listen_port = (argc >= 4) ? std::atoi(argv[3]) : 12346;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in local_addr{};
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(static_cast<uint16_t>(local_listen_port));
    bind(sock, reinterpret_cast<sockaddr*>(&local_addr), sizeof(local_addr));
    make_socket_non_blocking(sock);

    sockaddr_in dashboard_addr{};
    dashboard_addr.sin_family = AF_INET;
    dashboard_addr.sin_port = htons(static_cast<uint16_t>(dashboard_port));
    inet_aton(dashboard_ip, &dashboard_addr.sin_addr);

    HardwareConfig hw_cfg = read_hardware_config_from_env();
    bool gpio_mode = false;
#if SCCA_HAS_GPIOD
    GpioHardwareManager io;
    if (hw_cfg.use_gpio) gpio_mode = io.init(hw_cfg);
#endif

    SuperEstado estado_atual = SuperEstado::MODO_MANUAL;
    SubEstadoManual sub_manual = SubEstadoManual::DEFAULT;
    SubEstadoAutopilot sub_auto = SubEstadoAutopilot::AGINDO;

    int referencia_trim_posicao = 500;
    int posicao_atual_calculada = 500;
    bool mola_virtual_ativa = true;

    DashboardControl d_control{false, false, 500};

    std::cout << "SCCA Ativo. Executando Maquina de Estados UML...\n";

    while (true) {
        poll_dashboard_commands(sock, d_control);

        int b_up = 0, b_down = 0, t_rel = 0, ovr = 0, lim_top = 0, lim_bottom = 0;
        int raw_load_cell = 0;

        if (gpio_mode) {
#if SCCA_HAS_GPIOD
            io.read_inputs(b_up, b_down, t_rel, ovr, lim_top, lim_bottom);
            int hx_val = 0;
            if (io.read_hx711(hx_val)) raw_load_cell = hx_val;
#endif
        } else {
            posicao_atual_calculada = d_control.transducer_position;
        }

        // Variáveis locais para controle imediato das bobinas do driver do motor no ciclo
        bool m_enable = false;
        bool m_direction_up = true;
        bool m_step = false;

        if (d_control.hydraulic_failure) {
            b_up = 0; 
            b_down = 0;
            mola_virtual_ativa = false;
            std::cout << "[ALERTA] Falha Hidraulica Detectada. Sistema Travado.\n";
            
            // Lógica de atuação do motor: Travar fisicamente o motor ativando o freio estático
#if SCCA_HAS_GPIOD
            if (gpio_mode) io.write_motor_signals(true, true, false); 
#endif

            // Envia telemetria de erro e pula processamento dos modos normais
            CollectiveTelemetry tel{0, 0, 0, 1, 0};
            std::string p = build_collective_csv(tel);
            sendto(sock, p.c_str(), p.size(), 0, reinterpret_cast<sockaddr*>(&dashboard_addr), sizeof(dashboard_addr));
            usleep(50000);
            continue;
        }

        // Alteração controle manual e automático
        if (estado_atual == SuperEstado::MODO_MANUAL && d_control.autopilot_active) {
            estado_atual = SuperEstado::PILOTO_AUTOMATICO;
            sub_auto = SubEstadoAutopilot::AGINDO;
            std::cout << ">> Transicao: MODO_MANUAL -> PILOTO_AUTOMATICO\n";
        } 
        else if (estado_atual == SuperEstado::PILOTO_AUTOMATICO && !d_control.autopilot_active) {
            estado_atual = SuperEstado::MODO_MANUAL;
            sub_manual = SubEstadoManual::DEFAULT;
            mola_virtual_ativa = true;
            std::cout << ">> Transicao: PILOTO_AUTOMATICO -> MODO_MANUAL\n";
        }

        // Lógica de mudança de estado
        if (estado_atual == SuperEstado::MODO_MANUAL) {
            switch (sub_manual) {
                
                case SubEstadoManual::DEFAULT:
                    mola_virtual_ativa = true;
                    // Transições de saída do estado Default
                    if (t_rel == 1) {
                        sub_manual = SubEstadoManual::TRIM_PRESSED;
                    } else if (b_up == 1) {
                        sub_manual = SubEstadoManual::BEEP_TRIM_UP;
                    } else if (b_down == 1) {
                        sub_manual = SubEstadoManual::BEEP_TRIM_DOWN;
                    } else if (std::abs(raw_load_cell) > hw_cfg.load_cell_override_threshold) {
                        sub_manual = SubEstadoManual::OVERRIDE_MANUAL;
                    }

                    // Ação do estado: Executa a força centralizadora da mola virtual no motor
                    if (posicao_atual_calculada != referencia_trim_posicao) {
                        m_enable = true;
                        m_direction_up = (referencia_trim_posicao > posicao_atual_calculada);
                        m_step = true;
                    }
                    break;

                case SubEstadoManual::TRIM_PRESSED:
                    mola_virtual_ativa = false; // entry / Desabilita mola virtual
                    m_enable = false;           // Desliga driver do motor (Eixo solto)
                    
                    if (t_rel == 0) {
                        referencia_trim_posicao = posicao_atual_calculada; // exit / Atualiza a posição
                        sub_manual = SubEstadoManual::DEFAULT;
                    }
                    break;

                case SubEstadoManual::BEEP_TRIM_UP:
                    if (lim_top == 1) {
                        sub_manual = SubEstadoManual::DEFAULT; 
                    } else if (b_up == 0) {
                        sub_manual = SubEstadoManual::DEFAULT; 
                    } else {
                        referencia_trim_posicao += hw_cfg.beep_trim_step;
                        // Ação do estado: Motor move fisicamente o coletivo para cima
                        m_enable = true;
                        m_direction_up = true;
                        m_step = true;
                    }
                    break;

                case SubEstadoManual::BEEP_TRIM_DOWN:
                    if (lim_bottom == 1) {
                        sub_manual = SubEstadoManual::DEFAULT;
                    } else if (b_down == 0) {
                        sub_manual = SubEstadoManual::DEFAULT;
                    } else {
                        referencia_trim_posicao -= hw_cfg.beep_trim_step;
                        // Ação do estado: Motor move fisicamente o coletivo para baixo
                        m_enable = true;
                        m_direction_up = false;
                        m_step = true;
                    }
                    break;

                case SubEstadoManual::OVERRIDE_MANUAL:
                    mola_virtual_ativa = true;
                    if (std::abs(raw_load_cell) < hw_cfg.load_cell_return_threshold) {
                        sub_manual = SubEstadoManual::DEFAULT;
                    }

                    // Ação do estado: Força contra o deslocamento do piloto (Mola operando)
                    if (posicao_atual_calculada != referencia_trim_posicao) {
                        m_enable = true;
                        m_direction_up = (referencia_trim_posicao > posicao_atual_calculada);
                        m_step = true;
                    }
                    break;
            }
        } 
        else if (estado_atual == SuperEstado::PILOTO_AUTOMATICO) {
            switch (sub_auto) {
                
                case SubEstadoAutopilot::AGINDO:
                    referencia_trim_posicao = d_control.transducer_position;
                    mola_virtual_ativa = true;
                    
                    // Ação do estado: Segue ativamente o comando automático enviado da dashboard
                    if (posicao_atual_calculada != referencia_trim_posicao) {
                        m_enable = true;
                        m_direction_up = (referencia_trim_posicao > posicao_atual_calculada);
                        m_step = true;
                    }

                    if (std::abs(raw_load_cell) > hw_cfg.load_cell_override_threshold) {
                        sub_auto = SubEstadoAutopilot::override_autopilot;
                    }
                    break;

                case SubEstadoAutopilot::override_autopilot:
                    mola_virtual_ativa = true; // Mantém a mola ativa em oposição
                    
                    // Ação do estado: Briga mecânica contra a força que o piloto está exercendo
                    if (posicao_atual_calculada != referencia_trim_posicao) {
                        m_enable = true;
                        m_direction_up = (referencia_trim_posicao > posicao_atual_calculada);
                        m_step = true;
                    }

                    if (std::abs(raw_load_cell) < hw_cfg.load_cell_return_threshold) {
                        sub_auto = SubEstadoAutopilot::AGINDO;
                    }
                    break;
            }
        }

        // Atuação física nos pinos do driver do motor no final do processamento do estado
#if SCCA_HAS_GPIOD
        if (gpio_mode) {
            io.write_motor_signals(m_enable, m_direction_up, m_step);
        }
#endif

        // Criação e envio do pacote UDP
        CollectiveTelemetry tx_telemetry{
            b_up,
            b_down,
            t_rel,
            (sub_manual == SubEstadoManual::OVERRIDE_MANUAL || sub_auto == SubEstadoAutopilot::override_autopilot) ? 1 : 0, 
            mola_virtual_ativa ? raw_load_cell : 0 // Se desabilitada por Trim, zera leitura nominal de mola 
        };

        std::string out_payload = build_collective_csv(tx_telemetry);
        sendto(sock, out_payload.c_str(), out_payload.size(), 0, 
               reinterpret_cast<sockaddr*>(&dashboard_addr), sizeof(dashboard_addr));

        std::cout << "TX -> " << out_payload << " | Modos: " 
                  << (estado_atual == SuperEstado::MODO_MANUAL ? "MANUAL" : "AUTOPILOT") 
                  << " | Motor_EN: " << m_enable
                  << std::endl;

        usleep(50000);
    }

    close(sock);
    return 0;
}