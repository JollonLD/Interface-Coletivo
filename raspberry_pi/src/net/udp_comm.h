// src/net/udp_comm.h
#pragma once

#include "../include/telemetry.hpp"

namespace scca {
bool create_udp_socket_and_bind(int& out_sock, int local_port);
bool poll_dashboard_commands_non_blocking(int sock, DashboardControl& control_state);
ssize_t udp_send_payload(int sock, const std::string& ip, int port, const std::string& payload);
} // namespace scca
