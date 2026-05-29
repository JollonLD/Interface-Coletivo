// src/net/udp_comm.cpp
#include "udp_comm.h"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>

namespace scca {

bool make_socket_non_blocking(int sock) {
    const int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool create_udp_socket_and_bind(int& out_sock, int local_port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return false;
    sockaddr_in local_addr;
    std::memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(static_cast<uint16_t>(local_port));
    if (bind(sock, reinterpret_cast<sockaddr*>(&local_addr), sizeof(local_addr)) != 0) {
        close(sock);
        return false;
    }
    if (!make_socket_non_blocking(sock)) {
        close(sock);
        return false;
    }
    out_sock = sock;
    return true;
}

ssize_t udp_send_payload(int sock, const std::string& ip, int port, const std::string& payload) {
    sockaddr_in dashboard_addr;
    std::memset(&dashboard_addr, 0, sizeof(dashboard_addr));
    dashboard_addr.sin_family = AF_INET;
    dashboard_addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_aton(ip.c_str(), &dashboard_addr.sin_addr) == 0) return -1;
    return sendto(sock, payload.c_str(), payload.size(), 0, reinterpret_cast<sockaddr*>(&dashboard_addr), sizeof(dashboard_addr));
}

bool poll_dashboard_commands_non_blocking(int sock, scca::DashboardControl& control_state) {
    char buffer[256];
    while (true) {
        sockaddr_in src_addr;
        socklen_t src_len = sizeof(src_addr);
        const ssize_t bytes = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, reinterpret_cast<sockaddr*>(&src_addr), &src_len);
        if (bytes < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
            std::perror("recvfrom");
            return false;
        }
        buffer[bytes] = '\0';
        scca::DashboardControl parsed;
        if (scca::parse_dashboard_csv(std::string(buffer), parsed)) {
            control_state = parsed;
            std::cout << "RX P: autopilot=" << (control_state.autopilot_active ? 1 : 0)
                      << " hydraulic_failure=" << (control_state.hydraulic_failure ? 1 : 0)
                      << " transducer_position=" << control_state.transducer_position << std::endl;
        }
    }
}

} // namespace scca
