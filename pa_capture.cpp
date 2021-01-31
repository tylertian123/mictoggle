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
#include <pulse/simple.h>
#include <pulse/pulseaudio.h>

constexpr uint32_t SAMPLE_RATE = 4096;
// times 2 for 2 channels
constexpr int BUFFER_LENGTH = 512 * 2;
constexpr int BLOCK_SIZE = 64;

constexpr int THRESHOLD_PERCENT = 95;
constexpr int PEAK_BLOCK_COUNT = 5;
constexpr int16_t THRESHOLD_VALUE = std::numeric_limits<int16_t>::max() * THRESHOLD_PERCENT / 100;

// For graceful exit
volatile bool running = true;

void sig_handler(int signal) {
    running = false;
}

void handle_block(int16_t average) {
    /*
     * When the button is pressed quickly we see:
     *   - Wide + peak
     *   - Wide - peak
     *   - Wide + peak
     * There may also be a brief + peak between the two wide + peaks.
     * 
     * When the button is held down then released:
     *   - Wide + peak
     *   - Wide - peak
     *   - Flat 0 until button is released
     *   - Wide - peak
     *   - Wide + peak
     * 
     * Therefore we can count the number of wide + peaks.
     * Every first peak is button down, every second peak is button up.
     */
    static int block_count = 0;
    static int peak_count = 0;
    // Count the number of blocks for which the average exceeds the threshold
    // to filter out only the wide peaks
    if (average >= THRESHOLD_VALUE) {
        block_count ++;
    }
    else {
        if (block_count >= PEAK_BLOCK_COUNT) {
            peak_count ++;
            if (peak_count == 1) {
                std::cout << "Button down" << std::endl;
            }
            // Peak count 2
            else {
                std::cout << "Button up" << std::endl;
                peak_count = 0;
            }
        }
        block_count = 0;
    }
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
                handle_block(static_cast<int16_t>(sum / BLOCK_SIZE));
                sum = 0;
            }
        }
    }

    std::cout << "Cleaning up\n";
    pa_simple_free(stream);
    std::cout << "Cleanup ok; goodbye\n";
}
