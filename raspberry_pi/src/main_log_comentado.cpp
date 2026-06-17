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
#include <cstdlib>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#define CHIP_PATH "/dev/gpiochip0"

// ============================================================
// REDE UDP
// ============================================================

constexpr const char* DASHBOARD_IP = "10.0.0.5";
constexpr int UDP_RX_PORT = 5005;
constexpr int UDP_TX_PORT = 5006;

// ============================================================
// HX711 - CÉLULA DE CARGA
// ============================================================

constexpr int HX711_DATA  = 5;
constexpr int HX711_CLOCK = 6;
constexpr int HX_MAX_HIGH_US = 60;

// ============================================================
// BOTÕES
// ============================================================

constexpr int BTN_UP           = 17;
constexpr int BTN_DOWN         = 27;
constexpr int BTN_TRIM_RELEASE = 22;

// ============================================================
// FINS DE CURSO
// ============================================================

constexpr int LIMIT_TOP        = 24;
constexpr int LIMIT_BOTTOM     = 25;

// ============================================================
// MOTOR / DRIVER DM860H
// ============================================================

constexpr int MOTOR_PUL = 18;
constexpr int MOTOR_DIR = 23;
constexpr int MOTOR_ENA = 16;

constexpr int STEP_DELAY_US = 7000;

// ============================================================
// LIMITES DE OVERRIDE
// ============================================================

constexpr int OVERRIDE_STOPPED_LIMIT = 30000;
constexpr int OVERRIDE_UP_LIMIT      = 180000;
constexpr int OVERRIDE_DOWN_LIMIT    = -180000;

// ============================================================
// VARIÁVEIS GLOBAIS
// ============================================================

std::atomic<bool> running(true);

std::atomic<bool> g_up(false);
std::atomic<bool> g_down(false);
std::atomic<bool> g_trimRelease(false);
std::atomic<bool> g_limitTop(false);
std::atomic<bool> g_limitBottom(false);

std::atomic<int32_t> g_hxRaw(0);
std::atomic<int32_t> g_hxNet(0);
std::atomic<int> g_hxInvalid(0);

std::atomic<int> g_movement(0);
std::atomic<int> g_override(0);

std::atomic<int> g_autopilotActive(0);
std::atomic<int> g_hydraulicFailure(0);
std::atomic<int> g_transducerPosition(0);

// ============================================================
// FINALIZAÇÃO SEGURA
// ============================================================

void signalHandler(int)
{
    running = false;
}

// ============================================================
// LEITURA DAS ENTRADAS
// ============================================================

bool buttonPressed(gpiod::line_request& request, int gpio)
{
    return request.get_value(gpio) == gpiod::line::value::INACTIVE;
}

// ============================================================
// HX711
// ============================================================

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

int32_t hx711ReadValidAverage(
    gpiod::line_request& request,
    int samples,
    int& invalidCount)
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

// ============================================================
// OVERRIDE
// ============================================================

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
    else
    {
        overrideValue = 0;
    }

    g_override = overrideValue;
}

// ============================================================
// THREAD HX711
// ============================================================

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

    std::this_thread 
    ::sleep_for(std::chrono::milliseconds(500));

    int invalidReads = 0;
    int32_t hxOffset = hx711ReadValidAverage(request, 10, invalidReads);

    std::ofstream logFile("/home/coletivo/ColetivoMotor/loadcell_motion_log.csv");

    logFile
        << "tempo_ms,hx_raw,hx_net,invalid,beep_up,beep_down,"
        << "trim_release,limit_top,limit_bottom,movement,override,"
        << "ap,hyd,pos\n";

    auto startTime = std::chrono::steady_clock::now();

    while (running)
    {
        int32_t hxRaw = hx711ReadValidAverage(request, 3, invalidReads);
        int32_t hxNet = hxRaw - hxOffset;

        g_hxRaw = hxRaw;
        g_hxNet = hxNet;
        g_hxInvalid = invalidReads;

        updateOverride();

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
            << g_override.load() << ","
            << g_autopilotActive.load() << ","
            << g_hydraulicFailure.load() << ","
            << g_transducerPosition.load()
            << "\n";

        logFile.flush();

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

// ============================================================
// THREAD UDP
// ============================================================

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
        char tx[256];

        std::snprintf(
            tx,
            sizeof(tx),
            "C,%d,%d,%d,%d,%d",
            g_up.load() ? 1 : 0,
            g_down.load() ? 1 : 0,
            g_trimRelease.load() ? 1 : 0,
            g_override.load(),
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

// ============================================================
// MOTOR
// ============================================================

void stepMotor(gpiod::line_request& request, bool direction)
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
}

// ============================================================
// PROGRAMA PRINCIPAL
// ============================================================

int main()
{
    // ------------------------------------------------------------
    // Permite finalizar o programa com Ctrl+C.
    // Quando Ctrl+C é pressionado, running passa para false.
    // Isso encerra o loop principal e também as threads.
    // ------------------------------------------------------------
    std::signal(SIGINT, signalHandler);

    std::cout << "COLETIVO - MOTOR + HX711 VALIDADO + UDP + OVERRIDE" << std::endl;
    std::cout << "Log: /home/coletivo/ColetivoMotor/loadcell_motion_log.csv" << std::endl;

    // ------------------------------------------------------------
    // Inicia a thread da célula de carga.
    //
    // Esta thread roda em paralelo ao controle do motor.
    // Ela lê o HX711 continuamente, calcula HX_RAW, HX_NET,
    // invalida leituras com clock alto maior que 60 us,
    // atualiza o override e grava o CSV.
    // ------------------------------------------------------------
    std::thread hxThread(hx711Thread);

    // ------------------------------------------------------------
    // Inicia a thread UDP.
    //
    // Esta thread envia para o Dashboard:
    // C,up,down,trim_release,override,hx_net
    //
    // E recebe do Dashboard:
    // P,autopilot_active,hydraulic_failure,transducer_position
    // ------------------------------------------------------------
    std::thread udpComThread(udpThread);

    // ------------------------------------------------------------
    // Abre o chip GPIO principal do Raspberry.
    // No Raspberry Pi 5, as GPIOs usadas estão no /dev/gpiochip0.
    // ------------------------------------------------------------
    auto chip = gpiod::chip(CHIP_PATH);

    // ------------------------------------------------------------
    // Configuração das entradas digitais.
    //
    // Os botões e fins de curso estão ligados ao GND.
    // Por isso usamos PULL_UP interno:
    //
    // Solto     = nível alto = 1
    // Acionado  = nível baixo = 0
    //
    // A função buttonPressed() já inverte essa lógica e retorna
    // true quando o botão/chave está acionado.
    // ------------------------------------------------------------
    auto inputSettings = gpiod::line_settings()
        .set_direction(gpiod::line::direction::INPUT)
        .set_bias(gpiod::line::bias::PULL_UP);

    // ------------------------------------------------------------
    // Configuração das saídas do motor.
    //
    // MOTOR_PUL -> pulso de passo
    // MOTOR_DIR -> direção
    // MOTOR_ENA -> habilitação/liberação do driver
    //
    // O sinal sai do Raspberry em 3,3 V, entra no SN74HCT541
    // e sai em 5 V para o driver DM860H.
    // ------------------------------------------------------------
    auto outputSettings = gpiod::line_settings()
        .set_direction(gpiod::line::direction::OUTPUT)
        .set_output_value(gpiod::line::value::INACTIVE);

    // ------------------------------------------------------------
    // Lista das entradas usadas pelo controle principal.
    // O HX711 não entra aqui porque ele é lido em thread separada.
    // ------------------------------------------------------------
    gpiod::line::offsets inputLines = {
        BTN_UP,
        BTN_DOWN,
        BTN_TRIM_RELEASE,
        LIMIT_TOP,
        LIMIT_BOTTOM
    };

    // ------------------------------------------------------------
    // Lista das saídas usadas pelo controle do motor.
    // ------------------------------------------------------------
    gpiod::line::offsets outputLines = {
        MOTOR_PUL,
        MOTOR_DIR,
        MOTOR_ENA
    };

    // ------------------------------------------------------------
    // Solicita ao sistema operacional o controle dessas GPIOs.
    // ------------------------------------------------------------
    auto request = chip.prepare_request()
        .set_consumer("coletivo_motor")
        .add_line_settings(inputLines, inputSettings)
        .add_line_settings(outputLines, outputSettings)
        .do_request();

    // ------------------------------------------------------------
    // Estado inicial do ENA.
    //
    // Motor TRAVADO
    // ------------------------------------------------------------
    request.set_value(MOTOR_ENA, gpiod::line::value::INACTIVE);

    // ------------------------------------------------------------
    // Loop principal do controle do coletivo.
    //
    // Este loop:
    // 1. lê botões;
    // 2. lê fins de curso;
    // 3. atualiza variáveis globais;
    // 4. controla ENA, DIR e PUL;
    // 5. define o estado de movimento;
    // 6. mostra diagnóstico na tela.
    // ------------------------------------------------------------
    while (running)
    {
        // --------------------------------------------------------
        // Leitura instantânea dos comandos físicos.
        // A função buttonPressed() retorna true quando a entrada
        // está acionada.
        // --------------------------------------------------------
        bool up = buttonPressed(request, BTN_UP);
        bool down = buttonPressed(request, BTN_DOWN);
        bool trimRelease = buttonPressed(request, BTN_TRIM_RELEASE);

        bool limitTop = buttonPressed(request, LIMIT_TOP);
        bool limitBottom = buttonPressed(request, LIMIT_BOTTOM);

        // --------------------------------------------------------
        // Atualização das variáveis globais usadas pelas threads.
        //
        // g_up, g_down, g_limitTop e g_limitBottom são usados:
        // - pelo log da célula;
        // - pelo pacote UDP enviado ao Dashboard.
        //
        // g_trimRelease recebe !trimRelease porque, na lógica
        // validada do botão normalmente fechado, o valor enviado
        // ao Dashboard é 1 quando o Trim Release está acionado.
        // --------------------------------------------------------
        g_up = up;
        g_down = down;
        g_trimRelease = !trimRelease;
        g_limitTop = limitTop;
        g_limitBottom = limitBottom;

        // --------------------------------------------------------
        // CASO 1: TRIM RELEASE ACIONADO
        //
        // Na lógica validada:
        // trimRelease == false indica botão pressionado.
        //
        // Quando o piloto pressiona Trim Release:
        // - o motor é liberado pelo ENA;
        // - não são gerados pulsos de motor;
        // - movement recebe 2;
        // - override fica 0 nesta versão.
        // --------------------------------------------------------
        if (!trimRelease)
        {
            request.set_value(MOTOR_ENA, gpiod::line::value::ACTIVE);

            g_movement = 2;
            updateOverride();

            std::cout
                << "\rUP:" << up
                << " DOWN:" << down
                << " TRIM_RELEASE:1"
                << " TOP:" << limitTop
                << " BOTTOM:" << limitBottom
                << " MOV:2"
                << " OVERRIDE:" << g_override.load()
                << " HX_RAW:" << g_hxRaw.load()
                << " HX_NET:" << g_hxNet.load()
                << " INVALID:" << g_hxInvalid.load()
                << " AP:" << g_autopilotActive.load()
                << " HYD:" << g_hydraulicFailure.load()
                << " POS:" << g_transducerPosition.load()
                << " | TRIM RELEASE: motor livre        "
                << std::flush;
        }

        // --------------------------------------------------------
        // CASO 2: TRIM RELEASE SOLTO
        //
        // O motor permanece travado/habilitado.
        // Os comandos Beep Trim Up e Beep Trim Down podem movimentar
        // o motor, respeitando os fins de curso.
        // --------------------------------------------------------
        else
        {
            request.set_value(MOTOR_ENA, gpiod::line::value::INACTIVE);

            // ----------------------------------------------------
            // Beep Trim Up acionado.
            //
            // Direção validada:
            // UP -> stepMotor(false)
            //
            // Proteção validada:
            // UP só movimenta se limitBottom == 0.
            // ----------------------------------------------------
            if (up && !down)
            {
                if (limitBottom == 0)
                {
                    g_movement = 1;
                    stepMotor(request, false);
                }
                else
                {
                    g_movement = 0;
                }

                updateOverride();

                std::cout
                    << "\rUP:" << up
                    << " DOWN:" << down
                    << " TRIM_RELEASE:0"
                    << " TOP:" << limitTop
                    << " BOTTOM:" << limitBottom
                    << " MOV:" << g_movement.load()
                    << " OVERRIDE:" << g_override.load()
                    << " HX_RAW:" << g_hxRaw.load()
                    << " HX_NET:" << g_hxNet.load()
                    << " INVALID:" << g_hxInvalid.load()
                    << " AP:" << g_autopilotActive.load()
                    << " HYD:" << g_hydraulicFailure.load()
                    << " POS:" << g_transducerPosition.load()
                    << " | UP movimentando                  "
                    << std::flush;
            }

            // ----------------------------------------------------
            // Beep Trim Down acionado.
            //
            // Direção validada:
            // DOWN -> stepMotor(true)
            //
            // Proteção validada:
            // DOWN só movimenta se limitTop == 0.
            // ----------------------------------------------------
            else if (down && !up)
            {
                if (limitTop == 0)
                {
                    g_movement = -1;
                    stepMotor(request, true);
                }
                else
                {
                    g_movement = 0;
                }

                updateOverride();

                std::cout
                    << "\rUP:" << up
                    << " DOWN:" << down
                    << " TRIM_RELEASE:0"
                    << " TOP:" << limitTop
                    << " BOTTOM:" << limitBottom
                    << " MOV:" << g_movement.load()
                    << " OVERRIDE:" << g_override.load()
                    << " HX_RAW:" << g_hxRaw.load()
                    << " HX_NET:" << g_hxNet.load()
                    << " INVALID:" << g_hxInvalid.load()
                    << " AP:" << g_autopilotActive.load()
                    << " HYD:" << g_hydraulicFailure.load()
                    << " POS:" << g_transducerPosition.load()
                    << " | DOWN movimentando                "
                    << std::flush;
            }

            // ----------------------------------------------------
            // Nenhum comando de movimento ativo.
            //
            // O motor permanece travado/habilitado.
            // O movement recebe 0.
            // O override neste estado detecta esforço manual
            // aplicado ao coletivo parado.
            // ----------------------------------------------------
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
                    << " OVERRIDE:" << g_override.load()
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

    // ------------------------------------------------------------
    // Finalização segura.
    //
    // Ao sair do loop, coloca o ENA em estado seguro conforme a
    // lógica já utilizada no protótipo.
    // ------------------------------------------------------------
    request.set_value(MOTOR_ENA, gpiod::line::value::ACTIVE);

    // ------------------------------------------------------------
    // Aguarda encerramento das threads.
    // ------------------------------------------------------------
    if (hxThread.joinable())
        hxThread.join();

    if (udpComThread.joinable())
        udpComThread.join();

    std::cout << std::endl << "Programa finalizado." << std::endl;

    return 0;
}