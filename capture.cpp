/*
 * Usage:
 *      capture [device] [sample rate] [buffer frames] [threshold %] [required frames]
 */

#include <iostream>
#include <cstdlib>
#include <cstdint>
#include <signal.h>
#include <alsa/asoundlib.h>

// For graceful exit
volatile bool running = true;

void sig_handler(int signal) {
    running = false;
}

// Handles the error if one exists
// If no error exists (err >= 0), doesn't do anything
// For now simply outputs an error message with the code and exits
void handle_error(const char *action, int err, int exit_code = 1) {
    if (err >= 0) {
        return;
    }
    std::cerr << "Failed: " << action << " (error code: " << err << ")\n";
    exit(exit_code);
}

int main(int argc, char **argv) {
    int err;

    signal(SIGINT, &sig_handler);
    signal(SIGTERM, &sig_handler);

    // Read params
    const char *device_name = argc > 1 ? argv[1] : "hw:0";
    std::cout << "Using device " << device_name << "\n";
    unsigned int sample_rate = 1024;
    if (argc > 2) {
        errno = 0;
        sample_rate = std::strtoul(argv[2], nullptr, 10);
        if (errno != 0) {
            std::cerr << "Invalid sample rate entered; default will be used\n";
        }
        sample_rate = 1024;
    }
    std::cout << "Using sample rate " << sample_rate << "\n";
    int buffer_frames = 128;
    if (argc > 3) {
        errno = 0;
        buffer_frames = std::strtoul(argv[3], nullptr, 10);
        if (errno != 0) {
            std::cerr << "Invalid buffer frame count; default will be used\n";
        }
        buffer_frames = 128;
    }
    std::cout << "Using " << buffer_frames << " buffer frames\n";
    int threshold_percent = 90;
    if (argc > 4) {
        errno = 0;
        threshold_percent = std::strtoul(argv[4], nullptr, 10);
        if (errno != 0) {
            std::cerr << "Invalid threshold %; default will be used\n";
        }
        threshold_percent = 90;
    }
    std::cout << "Using threshold of " << threshold_percent << "%\n";
    int threshold = 0x7FFF * threshold_percent / 100;
    int required_frames = 3;
    if (argc > 5) {
        errno = 0;
        required_frames = std::strtoul(argv[5], nullptr, 10);
        if (errno != 0) {
            std::cerr << "Invalid required number of frames; default will be used\n";
        }
        required_frames = 3;
    }
    std::cout << "Using required frames: " << required_frames << "\n";

    // Open device
    snd_pcm_t *device;
    // Device handle, device name, stream (capture/playback), mode (0, nonblock, async)
    handle_error("open stream", snd_pcm_open(&device, device_name, SND_PCM_STREAM_CAPTURE, 0));
    std::cout << "Device opened\n";

    // Init hardware params
    snd_pcm_hw_params_t *hw_params;
    handle_error("alloc hw params", snd_pcm_hw_params_malloc(&hw_params));
    handle_error("init hw params", snd_pcm_hw_params_any(device, hw_params));
    std::cout << "Hardware params initialized\n";

    // Configure hw params
    handle_error("set access type", snd_pcm_hw_params_set_access(device, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED));
    // Set sample format to signed 16-bit little endian
    // Note: If this is changed, the buffer allocation line also needs to be changed
    handle_error("set sample format", snd_pcm_hw_params_set_format(device, hw_params, SND_PCM_FORMAT_S16_LE));
    handle_error("set sample rate", snd_pcm_hw_params_set_rate_near(device, hw_params, &sample_rate, nullptr));
    handle_error("set channel count", snd_pcm_hw_params_set_channels(device, hw_params, 2));
    handle_error("set hw params", snd_pcm_hw_params(device, hw_params));
    snd_pcm_hw_params_free(hw_params);
    std::cout << "Hardware params set\n";

    handle_error("prepare device", snd_pcm_prepare(device));
    std::cout << "Device prepared\n";

    // Multiply by # buffer frames and 2 channels
    // Signed int16 here corresponds to SND_PCM_FORMAT_S16_LE above (little endian on Intel and AMD)
    std::int16_t *buffer = new int16_t[buffer_frames * 2];
    std::cout << "Buffer allocated\n";

    std::cout << "Starting capture\n";
    int count = 0;
    bool held = false;
    int press_count = 0;
    while (running) {
        int frames_read = snd_pcm_readi(device, buffer, buffer_frames);
        if (frames_read < 0) {
            std::cerr << "Read failed (code: " << frames_read << ")\n";
            break;
        }
        long sum = 0;
        // Multiply by 2 for 2 channels
        for (int i = 0; i < frames_read * 2; i ++) {
            sum += buffer[i];
        }
        sum /= buffer_frames;
        // Signal drops to -1
        if (sum < -threshold) {
            if (count >= required_frames && !held) {
                held = true;
                std::cout << "Button pressed " << ++press_count << "\n";
            }
            else {
                count += 1;
            }
        }
        else {
            if (held) {
                std::cout << "Button released\n";
            }
            count = 0;
            held = false;
        }
    }

    std::cout << "Starting cleanup\n";
    delete[] buffer;
    handle_error("close stream", snd_pcm_close(device));
    std::cout << "Cleanup ok\n";
}
