#include <gpiod.hpp>
#include <iostream>
#include <chrono>
#include <thread>
#include <cstdint>
#include <atomic>
#include <csignal>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <algorithm>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#define CHIP_PATH "/dev/gpiochip0"

// Rede UDP
constexpr const char* DASHBOARD_IP = "10.0.0.5";
constexpr int UDP_RX_PORT = 5005;
constexpr int UDP_TX_PORT = 5006;

// HX711
constexpr int HX711_DATA  = 5;
constexpr int HX711_CLOCK = 6;
constexpr int HX_MAX_HIGH_US = 60;

// Botões
constexpr int BTN_UP           = 17;
constexpr int BTN_DOWN         = 27;
constexpr int BTN_TRIM_RELEASE = 22;

// Fins de curso
constexpr int LIMIT_TOP        = 24;
constexpr int LIMIT_BOTTOM     = 25;

// Motor
constexpr int MOTOR_PUL = 18;
constexpr int MOTOR_DIR = 23;
constexpr int MOTOR_ENA = 16;

constexpr int STEP_DELAY_US = 7000;

std::atomic<bool> running(true);

std::atomic<bool> g_up(false);
std::atomic<bool> g_down(false);
std::atomic<bool> g_trimRelease(false);
std::atomic<bool> g_limitTop(false);
std::atomic<bool> g_limitBottom(false);

std::atomic<int32_t> g_hxRaw(0);
std::atomic<int32_t> g_hxNet(0);
std::atomic<int> g_hxInvalid(0);

std::atomic<int> g_posTop(0);
std::atomic<int> g_posBottom(0);
std::atomic<bool> g_calibrated(false);

//  1 = UP
// -1 = DOWN
//  0 = parado/travado
//  2 = trim release / motor livre
std::atomic<int> g_movement(0);

std::atomic<int> g_autopilotActive(0);
std::atomic<int> g_hydraulicFailure(0);
std::atomic<int> g_transducerPosition(0);

void signalHandler(int)
{
    running = false;
}

bool buttonPressed(gpiod::line_request& request, int gpio)
{
    return request.get_value(gpio) == gpiod::line::value::INACTIVE;
}

bool hx711Ready(gpiod::line_request& request)
{
    return request.get_value(HX711_DATA) == gpiod::line::value::INACTIVE;
}

int32_t hx711ReadRawValidated(gpiod::line_request& request, bool& valid)
{
    valid = true;
    int32_t value = 0;
    int timeout = 0;

    while (!hx711Ready(request) && running)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        if (++timeout > 1000)
        {
            valid = false;
            return 0;
        }
    }

    for (int i = 0; i < 24; i++)
    {
        auto highStart = std::chrono::steady_clock::now();

        request.set_value(HX711_CLOCK, gpiod::line::value::ACTIVE);

        value <<= 1;

        if (request.get_value(HX711_DATA) == gpiod::line::value::ACTIVE)
            value++;

        request.set_value(HX711_CLOCK, gpiod::line::value::INACTIVE);

        auto highEnd = std::chrono::steady_clock::now();

        auto highUs = std::chrono::duration_cast<std::chrono::microseconds>(
            highEnd - highStart
        ).count();

        if (highUs >= HX_MAX_HIGH_US)
            valid = false;
    }

    auto highStart = std::chrono::steady_clock::now();

    request.set_value(HX711_CLOCK, gpiod::line::value::ACTIVE);
    request.set_value(HX711_CLOCK, gpiod::line::value::INACTIVE);

    auto highEnd = std::chrono::steady_clock::now();

    auto highUs = std::chrono::duration_cast<std::chrono::microseconds>(
        highEnd - highStart
    ).count();

    if (highUs >= HX_MAX_HIGH_US)
        valid = false;

    if (value & 0x800000)
        value |= 0xFF000000;

    return value;
}

int32_t hx711ReadValidAverage(gpiod::line_request& request, int samples, int& invalidCount)
{
    int64_t sum = 0;
    int validSamples = 0;

    while (validSamples < samples && running)
    {
        bool valid = true;
        int32_t raw = hx711ReadRawValidated(request, valid);

        if (valid)
        {
            sum += raw;
            validSamples++;
        }
        else
        {
            invalidCount++;
        }
    }

    if (validSamples == 0)
        return 0;

    return static_cast<int32_t>(sum / validSamples);
}

void hx711Thread()
{
    auto chip = gpiod::chip(CHIP_PATH);

    auto inputSettings = gpiod::line_settings()
        .set_direction(gpiod::line::direction::INPUT)
        .set_bias(gpiod::line::bias::DISABLED);

    auto outputSettings = gpiod::line_settings()
        .set_direction(gpiod::line::direction::OUTPUT)
        .set_output_value(gpiod::line::value::INACTIVE);

    gpiod::line::offsets inputLines = { HX711_DATA };
    gpiod::line::offsets outputLines = { HX711_CLOCK };

    auto request = chip.prepare_request()
        .set_consumer("hx711")
        .add_line_settings(inputLines, inputSettings)
        .add_line_settings(outputLines, outputSettings)
        .do_request();

    request.set_value(HX711_CLOCK, gpiod::line::value::INACTIVE);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    int invalidReads = 0;
    int32_t hxOffset = hx711ReadValidAverage(request, 10, invalidReads);

    std::ofstream logFile("/home/coletivo/ColetivoMotor/loadcell_motion_log.csv");

    logFile
        << "tempo_ms,"
        << "hx_raw,"
        << "hx_net,"
        << "invalid,"
        << "beep_up,"
        << "beep_down,"
        << "trim_release,"
        << "limit_top,"
        << "limit_bottom,"
        << "movement,"
        << "ap,"
        << "hyd,"
        << "pos"
        << "\n";

    auto startTime = std::chrono::steady_clock::now();

    while (running)
    {
        int32_t hxRaw = hx711ReadValidAverage(request, 3, invalidReads);
        int32_t hxNet = hxRaw - hxOffset;

        g_hxRaw = hxRaw;
        g_hxNet = hxNet;
        g_hxInvalid = invalidReads;

        auto now = std::chrono::steady_clock::now();

        auto tempoMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - startTime
        ).count();

        logFile
            << tempoMs << ","
            << hxRaw << ","
            << hxNet << ","
            << invalidReads << ","
            << (g_up.load() ? 1 : 0) << ","
            << (g_down.load() ? 1 : 0) << ","
            << (g_trimRelease.load() ? 1 : 0) << ","
            << (g_limitTop.load() ? 1 : 0) << ","
            << (g_limitBottom.load() ? 1 : 0) << ","
            << g_movement.load() << ","
            << g_autopilotActive.load() << ","
            << g_hydraulicFailure.load() << ","
            << g_transducerPosition.load()
            << "\n";

        logFile.flush();

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void udpThread()
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (sock < 0)
    {
        std::cerr << "Erro ao criar socket UDP" << std::endl;
        return;
    }

    sockaddr_in localAddr{};
    localAddr.sin_family = AF_INET;
    localAddr.sin_addr.s_addr = INADDR_ANY;
    localAddr.sin_port = htons(UDP_RX_PORT);

    if (bind(sock, (sockaddr*)&localAddr, sizeof(localAddr)) < 0)
    {
        std::cerr << "Erro no bind UDP RX" << std::endl;
        close(sock);
        return;
    }

    sockaddr_in dashboardAddr{};
    dashboardAddr.sin_family = AF_INET;
    dashboardAddr.sin_port = htons(UDP_TX_PORT);
    inet_pton(AF_INET, DASHBOARD_IP, &dashboardAddr.sin_addr);

    char buffer[256];

    while (running)
    {
        int overrideValue = 0;

        //posTop e posBottom serão enviados no pacote com seus valores reais somente após o fim da calibragem completa (após atingir os finais de curso inferior e superior)
        bool isCalibrated = g_calibrated.load();
        int posTopToSend = isCalibrated ? g_posTop.load() : 0;
        int posBottomToSend = isCalibrated ? g_posBottom.load() : 0;

        char tx[256];
        std::snprintf(
            tx,
            sizeof(tx),
            "C,%d,%d,%d,%d,%d,%d,%d",
            g_up.load() ? 1 : 0,
            g_down.load() ? 1 : 0,
            g_trimRelease.load() ? 1 : 0,
            overrideValue,
            g_hxNet.load(),
            posTopToSend,
            posBottomToSend
        );

        sendto(
            sock,
            tx,
            std::strlen(tx),
            0,
            (sockaddr*)&dashboardAddr,
            sizeof(dashboardAddr)
        );

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 1000;

        int ready = select(sock + 1, &readfds, nullptr, nullptr, &tv);

        if (ready > 0 && FD_ISSET(sock, &readfds))
        {
            sockaddr_in senderAddr{};
            socklen_t senderLen = sizeof(senderAddr);

            int len = recvfrom(
                sock,
                buffer,
                sizeof(buffer) - 1,
                0,
                (sockaddr*)&senderAddr,
                &senderLen
            );

            if (len > 0)
            {
                buffer[len] = '\0';

                int ap = 0;
                int hyd = 0;
                int transducer = 0;

                if (std::sscanf(buffer, "P,%d,%d,%d", &ap, &hyd, &transducer) == 3)
                {
                    g_autopilotActive = ap;
                    g_hydraulicFailure = hyd;
                    g_transducerPosition = transducer;
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    close(sock);
}

bool stepMotor(gpiod::line_request& request, bool direction)
{
    bool limitTop = (request.get_value(LIMIT_TOP) == gpiod::line::value::INACTIVE);
    bool limitBottom = (request.get_value(LIMIT_BOTTOM) == gpiod::line::value::INACTIVE);

    if (!direction && limitBottom)
        return false;

    if (direction && limitTop)
        return false;

    request.set_value(
        MOTOR_DIR,
        direction ? gpiod::line::value::ACTIVE
                  : gpiod::line::value::INACTIVE
    );

    request.set_value(MOTOR_PUL, gpiod::line::value::ACTIVE);
    std::this_thread::sleep_for(std::chrono::microseconds(STEP_DELAY_US));

    request.set_value(MOTOR_PUL, gpiod::line::value::INACTIVE);
    std::this_thread::sleep_for(std::chrono::microseconds(STEP_DELAY_US));

    return true;
}

bool handleHydraulicFailure(gpiod::line_request& request)
{
    request.set_value(MOTOR_ENA, gpiod::line::value::INACTIVE);

    bool atBottom = (request.get_value(LIMIT_TOP) == gpiod::line::value::INACTIVE);

    if (!atBottom)
    {
        bool stepped = stepMotor(request, true);
        g_movement = stepped ? -1 : 0;
        return stepped;
    }

    g_movement = 0;
    return false;
}

bool runCalibration(gpiod::line_request& request)
{
    std::cout << "\n[CALIB] Iniciando calibragem automática..." << std::endl;

    while (running)
    {
        bool stepped = stepMotor(request, false);

        if (!stepped)
            break;

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!running)
        return false;

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    g_posBottom = g_transducerPosition.load();
    
    std::cout << "[CALIB] Fim de curso inferior (LIMIT_BOTTOM) atingido. posBottom = "
              << g_posBottom.load() << std::endl;

    while (running)
    {
        bool stepped = stepMotor(request, true);

        if (!stepped)
            break;

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!running)
        return false;

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    g_posTop = g_transducerPosition.load();

    std::cout << "[CALIB] Fim de curso superior (LIMIT_TOP) atingido. posTop = "
              << g_posTop.load() << std::endl;

    if (g_posTop.load() == g_posBottom.load())
    {
        std::cerr << "[CALIB] ERRO: posTop (" <<g_posTop.load() << ") == posBottom (" <<g_posBottom.load() << "). "
                  << "Fins de curso não produziram leituras distintas." << std::endl;
        return false;
    }

    g_calibrated = true;

    std::cout << "[CALIB] Calibragem concluída: posTop (LIMIT_TOP) = "
              << g_posTop.load() << ", posBottom (LIMIT_BOTTOM) = " << g_posBottom.load() << std::endl;

    return true;
}

bool positionWithinCalibration(int targetPosition)
{
    int lowerBound = std::min(g_posTop.load(), g_posBottom.load());
    int upperBound = std::max(g_posTop.load(), g_posBottom.load());
    return (targetPosition >= lowerBound) && (targetPosition <= upperBound);
}

int main()
{
    std::signal(SIGINT, signalHandler);

    std::cout << "COLETIVO - COLETA LOG CELULA DE CARGA + MOVIMENTO" << std::endl;
    std::cout << "Log: /home/coletivo/ColetivoMotor/loadcell_motion_log.csv" << std::endl;

    std::thread hxThread(hx711Thread);
    std::thread udpComThread(udpThread);

    auto chip = gpiod::chip(CHIP_PATH);

    auto inputSettings = gpiod::line_settings()
        .set_direction(gpiod::line::direction::INPUT)
        .set_bias(gpiod::line::bias::PULL_UP);

    auto outputSettings = gpiod::line_settings()
        .set_direction(gpiod::line::direction::OUTPUT)
        .set_output_value(gpiod::line::value::INACTIVE);

    gpiod::line::offsets inputLines = {
        BTN_UP,
        BTN_DOWN,
        BTN_TRIM_RELEASE,
        LIMIT_TOP,
        LIMIT_BOTTOM
    };

    gpiod::line::offsets outputLines = {
        MOTOR_PUL,
        MOTOR_DIR,
        MOTOR_ENA
    };

    auto request = chip.prepare_request()
        .set_consumer("coletivo_motor")
        .add_line_settings(inputLines, inputSettings)
        .add_line_settings(outputLines, outputSettings)
        .do_request();

    request.set_value(MOTOR_ENA, gpiod::line::value::INACTIVE);

    if (!runCalibration(request))
    {
        std::cerr << "[MAIN] Calibragem falhou ou foi interrompida. Encerrando." << std::endl;
        running = false;

        request.set_value(MOTOR_ENA, gpiod::line::value::ACTIVE); //destrava o motor antes de sair

        if (hxThread.joinable())      hxThread.join();
        if (udpComThread.joinable())  udpComThread.join();

        return 1;
    }
    
    while (running)
    {
        bool up = buttonPressed(request, BTN_UP);
        bool down = buttonPressed(request, BTN_DOWN);
        bool trimRelease = buttonPressed(request, BTN_TRIM_RELEASE);

        //leitura dos fins de curso mantidas apenas para telemetria/log
        bool limitTop = buttonPressed(request, LIMIT_TOP);
        bool limitBottom = buttonPressed(request, LIMIT_BOTTOM);

        bool hydraulicFailure = g_hydraulicFailure.load() != 0;

        g_limitTop = limitTop;
        g_limitBottom = limitBottom;

        if (hydraulicFailure)
        {
            g_up = false;
            g_down = false;
            g_trimRelease = false;

            bool descending = handleHydraulicFailure(request);

            std::cout
                << "\rUP:0 DOWN:0 TRIM_RELEASE:0"
                << " TOP:" << limitTop
                << " BOTTOM:" << limitBottom
                << " MOV:" << g_movement.load()
                << " HX_RAW:" << g_hxRaw.load()
                << " HX_NET:" << g_hxNet.load()
                << " INVALID:" << g_hxInvalid.load()
                << " AP:0 HYD:1"
                << " POS:" << g_transducerPosition.load()
                << (descending ? " | PANE: descendo ao limite INF  "
                               : " | PANE: travado no limite INF  ")
                << std::flush;

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        g_up = up;
        g_down = down;
        g_trimRelease = !trimRelease;

        if (!trimRelease)
        {
            request.set_value(MOTOR_ENA, gpiod::line::value::ACTIVE);
            g_movement = 2;

            std::cout
                << "\rUP:" << up
                << " DOWN:" << down
                << " TRIM_RELEASE:1"
                << " TOP:" << limitTop
                << " BOTTOM:" << limitBottom
                << " MOV:2"
                << " HX_RAW:" << g_hxRaw.load()
                << " HX_NET:" << g_hxNet.load()
                << " INVALID:" << g_hxInvalid.load()
                << " AP:" << g_autopilotActive.load()
                << " HYD:" << g_hydraulicFailure.load()
                << " POS:" << g_transducerPosition.load()
                << " | TRIM RELEASE: motor livre        "
                << std::flush;
        }
        else
        {
            request.set_value(MOTOR_ENA, gpiod::line::value::INACTIVE);

            if (up && !down)
            {
                bool stepped = stepMotor(request, false);
                g_movement = stepped ? 1 : 0;

                std::cout
                    << "\rUP:" << up
                    << " DOWN:" << down
                    << " TRIM_RELEASE:0"
                    << " TOP:" << limitTop
                    << " BOTTOM:" << limitBottom
                    << " MOV:" << g_movement.load()
                    << " HX_RAW:" << g_hxRaw.load()
                    << " HX_NET:" << g_hxNet.load()
                    << " INVALID:" << g_hxInvalid.load()
                    << " AP:" << g_autopilotActive.load()
                    << " HYD:" << g_hydraulicFailure.load()
                    << " POS:" << g_transducerPosition.load()
                    << (stepped ? " | UP movimentando                 "
                                : " | UP bloqueado: fim de curso INF  ")
                    << std::flush;
            }
            else if (down && !up)
            {
                bool stepped = stepMotor(request, true);
                g_movement = stepped ? -1 : 0;

                std::cout
                    << "\rUP:" << up
                    << " DOWN:" << down
                    << " TRIM_RELEASE:0"
                    << " TOP:" << limitTop
                    << " BOTTOM:" << limitBottom
                    << " MOV:" << g_movement.load()
                    << " HX_RAW:" << g_hxRaw.load()
                    << " HX_NET:" << g_hxNet.load()
                    << " INVALID:" << g_hxInvalid.load()
                    << " AP:" << g_autopilotActive.load()
                    << " HYD:" << g_hydraulicFailure.load()
                    << " POS:" << g_transducerPosition.load()
                    << (stepped ? " | DOWN movimentando               "
                                : " | DOWN bloqueado: fim de curso SUP")
                    << std::flush;
            }
            else
            {
                g_movement = 0;

                std::cout
                    << "\rUP:" << up
                    << " DOWN:" << down
                    << " TRIM_RELEASE:0"
                    << " TOP:" << limitTop
                    << " BOTTOM:" << limitBottom
                    << " MOV:0"
                    << " HX_RAW:" << g_hxRaw.load()
                    << " HX_NET:" << g_hxNet.load()
                    << " INVALID:" << g_hxInvalid.load()
                    << " AP:" << g_autopilotActive.load()
                    << " HYD:" << g_hydraulicFailure.load()
                    << " POS:" << g_transducerPosition.load()
                    << " | Motor parado/travado             "
                    << std::flush;

                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
    }

    request.set_value(MOTOR_ENA, gpiod::line::value::ACTIVE);

    if (hxThread.joinable())
        hxThread.join();

    if (udpComThread.joinable())
        udpComThread.join();

    std::cout << std::endl << "Programa finalizado." << std::endl;

    return 0;
}