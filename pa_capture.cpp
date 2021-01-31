/*
 * Usage:
 *      TODO
 * 
 * TODO: Can we reduce the CPU usage further?
 * sleep doesn't seem to be helping much, maybe use the async API?
 */

#include <iostream>
#include <limits>
#include <cstdlib>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <pulse/simple.h>
#include <pulse/pulseaudio.h>

constexpr uint32_t SAMPLE_RATE = 4096;
// times 2 for 2 channels
constexpr int BUFFER_LENGTH = 512 * 2;
constexpr int BLOCK_SIZE = 128;

constexpr int THRESHOLD_PERCENT = 95;
constexpr int16_t THRESHOLD_VALUE = std::numeric_limits<int16_t>::max() * THRESHOLD_PERCENT / 100;

// For graceful exit
volatile bool running = true;

void sig_handler(int signal) {
    running = false;
}

int main(int argc, char **argv) {
    signal(SIGINT, &sig_handler);
    signal(SIGTERM, &sig_handler);

    int err;
    // Connect and configure
    pa_simple *stream;
    pa_sample_spec sample_spec = {
        .format = PA_SAMPLE_S16NE,
        .rate = SAMPLE_RATE,
        .channels = 2,
    };
    // Server, app name, direction, device, stream name, sample spec, channel map, buffer attrs, error code
    if (!(stream = pa_simple_new(nullptr, "mictoggle_capture", PA_STREAM_RECORD, nullptr, "record",
                                 &sample_spec, nullptr, nullptr, &err))) {
        std::cout << "Failed to open stream: " << err << "\n";
        return 1;
    }

    int16_t buffer[BUFFER_LENGTH];
    while (running) {
        if (pa_simple_read(stream, buffer, sizeof(buffer), &err) < 0) {
            std::cout << "Failed to read: " << err << "\n";
            break;
        }

        long sum = 0;
        for (int i = 0, j = 0; i < BUFFER_LENGTH; i ++) {
            sum += buffer[i];
            if (++j == BLOCK_SIZE) {
                j = 0;
                sum /= BLOCK_SIZE;

                std::cout << sum << "\n";

                sum = 0;
            }
        }
        //std::cout << "Block average #" << ++block_count << ": " << sum << "\n";

        //usleep(1000 * 50);
    }

    std::cout << "Cleaning up\n";
    pa_simple_free(stream);
    std::cout << "Cleanup ok; goodbye\n";
}
