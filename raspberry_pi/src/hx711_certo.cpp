#include <gpiod.hpp>
#include <iostream>
#include <chrono>
#include <thread>
#include <cstdint>
#include <fstream>
#include <cstdlib>

#define CHIP_PATH "/dev/gpiochip0"

constexpr int HX711_DATA  = 5;
constexpr int HX711_CLOCK = 6;

constexpr int HX_SPIKE_DELTA = 50000;
constexpr int HX_MAX_HIGH_US = 60;

bool hx711Ready(gpiod::line_request& request)
{
    return request.get_value(HX711_DATA) == gpiod::line::value::INACTIVE;
}

int32_t hx711ReadRawValidated(gpiod::line_request& request, bool& valid)
{
    valid = true;
    int32_t value = 0;
    int timeout = 0;

    while (!hx711Ready(request))
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
        {
            value++;
        }

        request.set_value(HX711_CLOCK, gpiod::line::value::INACTIVE);

        auto highEnd = std::chrono::steady_clock::now();

        auto highUs = std::chrono::duration_cast<std::chrono::microseconds>(
            highEnd - highStart
        ).count();

        if (highUs >= HX_MAX_HIGH_US)
        {
            valid = false;
        }
    }

    // Pulso extra: canal A, ganho 128
    auto highStart = std::chrono::steady_clock::now();

    request.set_value(HX711_CLOCK, gpiod::line::value::ACTIVE);
    request.set_value(HX711_CLOCK, gpiod::line::value::INACTIVE);

    auto highEnd = std::chrono::steady_clock::now();

    auto highUs = std::chrono::duration_cast<std::chrono::microseconds>(
        highEnd - highStart
    ).count();

    if (highUs >= HX_MAX_HIGH_US)
    {
        valid = false;
    }

    if (value & 0x800000)
    {
        value |= 0xFF000000;
    }

    return value;
}

int32_t hx711ReadValidAverage(gpiod::line_request& request, int samples, int& invalidCount)
{
    int64_t sum = 0;
    int validSamples = 0;

    while (validSamples < samples)
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

    return static_cast<int32_t>(sum / samples);
}

int main()
{
    std::cout << "TESTE HX711 - VALIDACAO DE CLOCK HIGH" << std::endl;

    auto chip = gpiod::chip(CHIP_PATH);

    auto inputSettings = gpiod::line_settings()
        .set_direction(gpiod::line::direction::INPUT)
        .set_bias(gpiod::line::bias::DISABLED);

    auto outputSettings = gpiod::line_settings()
        .set_direction(gpiod::line::direction::OUTPUT)
        .set_output_value(gpiod::line::value::INACTIVE);

    gpiod::line::offsets inputLines = {
        HX711_DATA
    };

    gpiod::line::offsets outputLines = {
        HX711_CLOCK
    };

    auto request = chip.prepare_request()
        .set_consumer("hx711_validado")
        .add_line_settings(inputLines, inputSettings)
        .add_line_settings(outputLines, outputSettings)
        .do_request();

    request.set_value(HX711_CLOCK, gpiod::line::value::INACTIVE);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    int invalidReads = 0;

    int32_t hxOffset = hx711ReadValidAverage(request, 10, invalidReads);
    int32_t lastRaw = hxOffset;
    uint32_t spikes = 0;

    std::ofstream logFile("/home/coletivo/ColetivoMotor/hx711_validado_log.csv");
    logFile << "tempo_ms,hx_raw,hx_net,delta,spikes,invalid\n";

    auto startTime = std::chrono::steady_clock::now();

    std::cout << "Offset HX711 = " << hxOffset << std::endl;
    std::cout << "Log: /home/coletivo/ColetivoMotor/hx711_validado_log.csv" << std::endl;

    while (true)
    {
        int32_t hxRaw = hx711ReadValidAverage(request, 3, invalidReads);
        int32_t hxNet = hxRaw - hxOffset;
        int32_t delta = hxRaw - lastRaw;

        if (std::abs(delta) > HX_SPIKE_DELTA)
        {
            spikes++;
        }

        lastRaw = hxRaw;

        auto now = std::chrono::steady_clock::now();

        auto tempoMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - startTime
        ).count();

        logFile
            << tempoMs << ","
            << hxRaw << ","
            << hxNet << ","
            << delta << ","
            << spikes << ","
            << invalidReads
            << "\n";

        logFile.flush();

        std::cout
            << "\rHX_RAW:" << hxRaw
            << " HX_NET:" << hxNet
            << " DELTA:" << delta
            << " SPIKES:" << spikes
            << " INVALID:" << invalidReads
            << "          "
            << std::flush;

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    return 0;
}