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
#include <cmath>

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

// Limites de override (célula de carga) — mesmos valores da interface
constexpr int OVERRIDE_STOPPED_LIMIT = 30000;
constexpr int OVERRIDE_UP_LIMIT      = 180000;
constexpr int OVERRIDE_DOWN_LIMIT    = -180000;

// Margem de segurança para área útil calibrada (evita acionar fins de curso)
constexpr int CALIBRATION_MARGIN = 20000;

// IDs de manobra recebidos via UDP (pacote P)
constexpr int MANEUVER_NONE              = 0;
constexpr int MANEUVER_PA_CONSTANT_MOVE  = 1;

std::atomic<bool> running(true);

std::atomic<bool> g_up(false);
std::atomic<bool> g_down(false);
std::atomic<bool> g_trimRelease(false);
std::atomic<bool> g_limitTop(false);
std::atomic<bool> g_limitBottom(false);

std::atomic<int32_t> g_hxRaw(0);
std::atomic<int32_t> g_hxNet(0);
std::atomic<int> g_hxInvalid(0);

//  1 = UP
// -1 = DOWN
//  0 = parado/travado
//  2 = trim release / motor livre
std::atomic<int> g_movement(0);

std::atomic<int> g_override(0);
std::atomic<int> g_autopilotActive(0);
std::atomic<int> g_hydraulicFailure(0);
std::atomic<int> g_transducerPosition(0);
std::atomic<int> g_maneuverId(0);

// Área útil do transdutor (valores hxNet da calibragem automática)
std::atomic<int32_t> g_hxCalMin(0);
std::atomic<int32_t> g_hxCalMax(0);
std::atomic<int> g_hxCalibrated(0);

// Direção do movimento constante do PA: 1 = UP, -1 = DOWN
std::atomic<int> g_paDirection(-1);

void signalHandler(int)
{
    running = false;
}

void updateOverride()
{
    int movement = g_movement.load();
    int32_t hxNet = g_hxNet.load();
    int overrideValue = 0;

    if (movement == 0)
    {
        if (std::abs(hxNet) > OVERRIDE_STOPPED_LIMIT)
            overrideValue = 1;
    }
    else if (movement == 1)
    {
        if (hxNet > OVERRIDE_UP_LIMIT)
            overrideValue = 1;
    }
    else if (movement == -1)
    {
        if (hxNet < OVERRIDE_DOWN_LIMIT)
            overrideValue = 1;
    }

    g_override = overrideValue;
}

// Define a área útil com margem de segurança em relação aos limites de override.
void autoCalibrateTransducerUsefulArea()
{
    g_hxCalMin = OVERRIDE_DOWN_LIMIT + CALIBRATION_MARGIN;
    g_hxCalMax = OVERRIDE_UP_LIMIT - CALIBRATION_MARGIN;
    g_hxCalibrated = 1;

    std::cout
        << "Calibragem automatica: area util hxNet ["
        << g_hxCalMin.load()
        << ", "
        << g_hxCalMax.load()
        << "]"
        << std::endl;
}

void resetPaManeuverState()
{
    g_paDirection = -1;
    g_up = false;
    g_down = false;
    g_movement = 0;
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
        int overrideValue = g_override.load();

        char tx[256];
        std::snprintf(
            tx,
            sizeof(tx),
            "C,%d,%d,%d,%d,%d",
            g_up.load() ? 1 : 0,
            g_down.load() ? 1 : 0,
            g_trimRelease.load() ? 1 : 0,
            overrideValue,
            g_hxNet.load()
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
                int maneuver = 0;

                if (std::sscanf(buffer, "P,%d,%d,%d,%d", &ap, &hyd, &transducer, &maneuver) == 4)
                {
                    g_autopilotActive = ap;
                    g_hydraulicFailure = hyd;
                    g_transducerPosition = transducer;
                    g_maneuverId = maneuver;
                }
                else if (std::sscanf(buffer, "P,%d,%d,%d", &ap, &hyd, &transducer) == 3)
                {
                    g_autopilotActive = ap;
                    g_hydraulicFailure = hyd;
                    g_transducerPosition = transducer;
                    g_maneuverId = ap ? MANEUVER_PA_CONSTANT_MOVE : MANEUVER_NONE;
                }

                if (!ap)
                    resetPaManeuverState();
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

// Movimento do motor sem verificar fins de curso físicos (usado pelo PA).
bool stepMotorWithoutLimits(gpiod::line_request& request, bool direction)
{
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

// Manobra 1: simulação de movimento constante com Piloto Automático.
// Retorna true enquanto a manobra permanece ativa.
bool runPaConstantMovementManeuver(gpiod::line_request& request, bool trimHold)
{
    if (!g_autopilotActive.load()
        || g_maneuverId.load() != MANEUVER_PA_CONSTANT_MOVE
        || g_hydraulicFailure.load())
    {
        return false;
    }

    if (!trimHold)
        return false;

    if (!g_hxCalibrated.load())
        autoCalibrateTransducerUsefulArea();

    int32_t hxNet = g_hxNet.load();
    int32_t hxMin = g_hxCalMin.load();
    int32_t hxMax = g_hxCalMax.load();
    int direction = g_paDirection.load();

    if (direction == 1 && hxNet >= hxMax)
        direction = -1;
    else if (direction == -1 && hxNet <= hxMin)
        direction = 1;

    g_paDirection = direction;

    // Trim hold ativo durante o PA; beep trim físico é ignorado.
    g_trimRelease = false;
    request.set_value(MOTOR_ENA, gpiod::line::value::INACTIVE);

    if (direction == 1)
    {
        if (hxNet >= hxMax)
        {
            g_up = false;
            g_down = false;
            g_movement = 0;
            updateOverride();
            return g_override.load() == 0;
        }

        g_up = true;
        g_down = false;
        stepMotorWithoutLimits(request, false);
        g_movement = 1;
    }
    else
    {
        if (hxNet <= hxMin)
        {
            g_up = false;
            g_down = false;
            g_movement = 0;
            updateOverride();
            return g_override.load() == 0;
        }

        g_up = false;
        g_down = true;
        stepMotorWithoutLimits(request, true);
        g_movement = -1;
    }

    updateOverride();

    if (g_override.load())
        return false;

    return true;
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

    autoCalibrateTransducerUsefulArea();

    while (running)
    {
        bool up = buttonPressed(request, BTN_UP);
        bool down = buttonPressed(request, BTN_DOWN);
        bool trimRelease = buttonPressed(request, BTN_TRIM_RELEASE);

        //leitura dos fins de curso mantidas apenas para telemetria/log
        bool limitTop = buttonPressed(request, LIMIT_TOP);
        bool limitBottom = buttonPressed(request, LIMIT_BOTTOM);

        g_limitTop = limitTop;
        g_limitBottom = limitBottom;

        bool trimHold = trimRelease;
        bool paRunning = runPaConstantMovementManeuver(request, trimHold);

        if (!paRunning)
        {
            if (g_autopilotActive.load()
                && g_maneuverId.load() == MANEUVER_PA_CONSTANT_MOVE
                && (!trimHold || g_override.load()))
            {
                resetPaManeuverState();
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
                    << " OVR:" << g_override.load()
                    << " AP:" << g_autopilotActive.load()
                    << " MAN:" << g_maneuverId.load()
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
                    updateOverride();

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
                        << " OVR:" << g_override.load()
                        << " AP:" << g_autopilotActive.load()
                        << " MAN:" << g_maneuverId.load()
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
                    updateOverride();

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
                        << " OVR:" << g_override.load()
                        << " AP:" << g_autopilotActive.load()
                        << " MAN:" << g_maneuverId.load()
                        << " HYD:" << g_hydraulicFailure.load()
                        << " POS:" << g_transducerPosition.load()
                        << (stepped ? " | DOWN movimentando               "
                                    : " | DOWN bloqueado: fim de curso SUP")
                        << std::flush;
                }
                else
                {
                    g_movement = 0;
                    updateOverride();

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
                        << " OVR:" << g_override.load()
                        << " AP:" << g_autopilotActive.load()
                        << " MAN:" << g_maneuverId.load()
                        << " HYD:" << g_hydraulicFailure.load()
                        << " POS:" << g_transducerPosition.load()
                        << " | Motor parado/travado             "
                        << std::flush;

                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            }
        }
        else
        {
            std::cout
                << "\rUP:" << (g_up.load() ? 1 : 0)
                << " DOWN:" << (g_down.load() ? 1 : 0)
                << " TRIM_RELEASE:0"
                << " TOP:" << limitTop
                << " BOTTOM:" << limitBottom
                << " MOV:" << g_movement.load()
                << " HX_RAW:" << g_hxRaw.load()
                << " HX_NET:" << g_hxNet.load()
                << " INVALID:" << g_hxInvalid.load()
                << " OVR:" << g_override.load()
                << " AP:" << g_autopilotActive.load()
                << " MAN:" << g_maneuverId.load()
                << " HYD:" << g_hydraulicFailure.load()
                << " POS:" << g_transducerPosition.load()
                << " | PA: movimento constante          "
                << std::flush;
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