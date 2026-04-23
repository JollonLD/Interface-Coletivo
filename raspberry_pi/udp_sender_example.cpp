/*
 * EXEMPLO DE CÓDIGO C++ PARA RASPBERRY PI 5
 * ==========================================
 * 
 * Enviar dados TELEMETRIA DE COMANDO COLETIVO da aeronave
 * para o dashboard rodando em um computador.
 * 
 * Compile com: g++ -std=c++17 -o sender udp_sender_example.cpp
 * Execute com: ./sender 192.168.1.100 12345
 */

#include <iostream>
#include <string>
#include <cstring>
#include <cmath>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ============================================================================
// EXEMPLO 1: Enviar dados telemetria de COMANDO COLETIVO em JSON
// ============================================================================

void send_collective_control_json(int sock, const struct sockaddr_in& dest) {
    static int packet_num = 0;
    
    while (packet_num < 20) {
        // Simular leitura de telemetria de comando coletivo
        float position = 30.0 + 20.0 * sin(packet_num * 3.14159f / 10.0f);
        bool trim_hold = (packet_num % 3) != 0;
        std::string beep_trim = (packet_num % 2) == 0 ? "NEUTRAL" : "UP";
        bool pa_active = true;
        bool hydraulic_failure = false;
        float pilot_force = 2.0 + 1.5f * sin(packet_num * 3.14159f / 5.0f);
        
        // Construir JSON com estrutura esperada pelo dashboard
        json data;
        data["position_percent"] = position;
        data["trim_hold"] = trim_hold;
        data["beep_trim"] = beep_trim;
        data["pa_active"] = pa_active;
        data["hydraulic_failure"] = hydraulic_failure;
        data["pilot_force_kg"] = pilot_force;
        data["udp_connected"] = true;
        data["usb_connected"] = true;
        data["selected_maneuver"] = "Circuito Classico";
        data["maneuver_active"] = false;
        data["maneuver_state"] = "IDLE";
        data["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count() / 1e9;
        
        // Converter para string
        std::string payload = data.dump();
        
        // Enviar
        ssize_t bytes_sent = sendto(
            sock,
            payload.c_str(),
            payload.length(),
            0,
            (struct sockaddr*)&dest,
            sizeof(dest)
        );
        
        if (bytes_sent < 0) {
            perror("sendto");
        } else {
            std::cout << "Pacote #" << packet_num << ": " << payload << std::endl;
        }
        
        packet_num++;
        sleep(1);
    }
}

// ============================================================================
// EXEMPLO 2: Enviar dados em formato struct binário (mais eficiente)
// ============================================================================

struct __attribute__((packed)) CollectiveControlData {
    float position_percent;     // 4 bytes - posição coletiva 0-100%
    uint8_t trim_hold;          // 1 byte  - trim sarado (0 ou 1)
    uint8_t beep_trim;          // 1 byte  - 0=NEUTRAL, 1=UP, 2=DOWN
    uint8_t pa_active;          // 1 byte  - Power Assist (0 ou 1)
    uint8_t hydraulic_failure;  // 1 byte  - falha hidráulica (0 ou 1)
    float pilot_force_kg;       // 4 bytes - força piloto
    uint8_t udp_connected;      // 1 byte  - UDP connected (0 ou 1)
    uint8_t usb_connected;      // 1 byte  - USB connected (0 ou 1)
    uint32_t timestamp;         // 4 bytes - timestamp
};

void send_binary_example(int sock, const struct sockaddr_in& dest) {
    static int packet_num = 0;
    
    while (packet_num < 20) {
        CollectiveControlData data;
        
        // Simular leitura de telemetria
        data.position_percent = 30.0f + 20.0f * sin(packet_num * 3.14159f / 10.0f);
        data.trim_hold = (packet_num % 3) != 0 ? 1 : 0;
        data.beep_trim = (packet_num % 2) == 0 ? 0 : 1;  // 0=NEUTRAL, 1=UP
        data.pa_active = 1;
        data.hydraulic_failure = 0;
        data.pilot_force_kg = 2.0f + 1.5f * sin(packet_num * 3.14159f / 5.0f);
        data.udp_connected = 1;
        data.usb_connected = 1;
        data.timestamp = (uint32_t)time(NULL);
        
        // Enviar como struct binário
        ssize_t bytes_sent = sendto(
            sock,
            &data,
            sizeof(CollectiveControlData),
            0,
            (struct sockaddr*)&dest,
            sizeof(dest)
        );
        
        if (bytes_sent < 0) {
            perror("sendto");
        } else {
            std::cout << "Pacote #" << packet_num << " (Binary): "
                      << data.position_percent << "%, Force: "
                      << data.pilot_force_kg << " kg" << std::endl;
        }
        
        packet_num++;
        sleep(1);
    }
}

// ============================================================================
// EXEMPLO 3: Enviar dados em formato string (ASCII delimitado)
// ============================================================================

void send_string_example(int sock, const struct sockaddr_in& dest) {
    static int packet_num = 0;
    char buffer[512];
    
    while (packet_num < 20) {
        // Simular leitura de telemetria de comando coletivo
        float position = 30.0f + 20.0f * sin(packet_num * 3.14159f / 10.0f);
        int trim_hold = (packet_num % 3) != 0 ? 1 : 0;
        int beep_trim = (packet_num % 2) == 0 ? 0 : 1;  // 0=NEUTRAL, 1=UP
        int pa_active = 1;
        int hyd_failure = 0;
        float pilot_force = 2.0f + 1.5f * sin(packet_num * 3.14159f / 5.0f);
        
        // Formatar como string com delimitador '|'
        // Formato: POS=valor|TRIM=valor|BEEP=valor|PA=valor|HYD=valor|FORCE=valor|STATE=valor
        snprintf(buffer, sizeof(buffer),
                 "POS:%.1f|TRIM:%d|BEEP:%s|PA:%d|HYD:%d|FORCE:%.2f|STATE:IDLE|PKT:%d",
                 position, trim_hold, beep_trim == 0 ? "NEUTRAL" : "UP",
                 pa_active, hyd_failure, pilot_force, packet_num);
        
        // Enviar
        ssize_t bytes_sent = sendto(
            sock,
            buffer,
            strlen(buffer),
            0,
            (struct sockaddr*)&dest,
            sizeof(dest)
        );
        
        if (bytes_sent < 0) {
            perror("sendto");
        } else {
            std::cout << "Pacote #" << packet_num << " (String): " << buffer << std::endl;
        }
        
        packet_num++;
        sleep(1);
    }
}

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Uso: " << argv[0] << " <IP_destino> <porta>" << std::endl;
        std::cerr << "Exemplo: " << argv[0] << " 192.168.1.100 12345" << std::endl;
        return 1;
    }
    
    const char* dest_ip = argv[1];
    int dest_port = atoi(argv[2]);
    
    // Criar socket UDP
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }
    
    // Configurar destino
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(dest_port);
    
    if (inet_aton(dest_ip, &dest_addr.sin_addr) == 0) {
        std::cerr << "IP inválido: " << dest_ip << std::endl;
        close(sock);
        return 1;
    }
    
    std::cout << "Conectando a " << dest_ip << ":" << dest_port << std::endl;
    
    // Escolher exemplo (descomente um)
    std::cout << "\n=== Enviando telemetria de COMANDO COLETIVO (JSON) ===" << std::endl;
    send_collective_control_json(sock, dest_addr);
    
    // std::cout << "\n=== Enviando telemetria (Binary) ===" << std::endl;
    // send_binary_example(sock, dest_addr);
    
    // std::cout << "\n=== Enviando telemetria (String ASCII) ===" << std::endl;
    // send_string_example(sock, dest_addr);
    
    close(sock);
    std::cout << "Concluído!" << std::endl;
    
    return 0;
}

/*
 * INSTALAÇÃO DE DEPENDÊNCIAS NO RASPBERRY PI
 * ===========================================
 * 
 * sudo apt-get update
 * sudo apt-get install nlohmann-json3-dev
 * 
 * COMPILAÇÃO
 * ==========
 * 
 * g++ -std=c++17 -o udp_sender udp_sender_example.cpp
 * 
 * EXECUÇÃO
 * ========
 * 
 * ./udp_sender 192.168.1.100 12345
 * 
 * onde 192.168.1.100 é o IP do computador rodando o dashboard
 * 
 * DADOS ESPERADOS PELO DASHBOARD
 * ==============================
 * 
 * JSON:
 * {
 *   "position_percent": 42.5,    // Posição coletiva 0-100%
 *   "trim_hold": true,            // Trim sarado (true/false)
 *   "beep_trim": "NEUTRAL",       // "UP", "DOWN", "NEUTRAL"
 *   "pa_active": true,            // Power Assist ativo
 *   "hydraulic_failure": false,   // Falha hidráulica
 *   "pilot_force_kg": 2.3,        // Força do piloto em kg
 *   "udp_connected": true,        // UDP conectado
 *   "usb_connected": true,        // USB conectado
 *   "selected_maneuver": "Circuito", // Nome da manobra
 *   "maneuver_active": false,     // Manobra em execução
 *   "maneuver_state": "IDLE",     // Estado: IDLE, RUNNING, COMPLETED, ABORTED
 *   "timestamp": 1234567890.5
 * }
 * 
 * STRING ASCII:
 * POS:42.5|TRIM:1|BEEP:NEUTRAL|PA:1|HYD:0|FORCE:2.3|STATE:IDLE
 */
