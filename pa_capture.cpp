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
static const char * DEVICE_NAME = "alsa_output.pci-0000_09_00.3.analog-stereo.monitor";

template<typename Obj>
using pulse_object_destroy_func = void (*)(Obj *);

// for extremely lazy destructors
template<typename Obj, pulse_object_destroy_func<Obj> ...funcs>
struct pulse_object_destroyer {
	Obj*& v = nullptr;

	pulse_object_destroyer(Obj *& in) : v(in) {}
	pulse_object_destroyer() : v{nullptr} {}

	void reset() {
		if (!v) return;
		(funcs(v), ...);
		v = nullptr;
	}

	~pulse_object_destroyer() {
		reset();
		v = nullptr;
	}
};

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

pa_sample_spec in_sample_spec = {
	.format = PA_SAMPLE_S16NE,
	.rate = SAMPLE_RATE,
	.channels = 2,
};

pa_mainloop_api *mainloop_api = nullptr;
pa_context *context = nullptr;
pa_stream *read_stream = nullptr;


void handle_exit(pa_mainloop_api *m, pa_signal_event *e, int sig, void *) {
	assert(m);

	std::cerr << "got exit signal, killing" << std::endl;
	m->quit(m, 0); // tell run to exit
}

void handle_new_data(pa_stream *s, size_t length, void *) {
	std::cout << "got more read data with len " << length << "\n";
}

void handle_stream_state(pa_stream *s, void*) {
	assert(s);

	switch (pa_stream_get_state(s)) {
		case PA_STREAM_CREATING:
		case PA_STREAM_TERMINATED:
			break;

		case PA_STREAM_READY:
			std::cout << "connected to stream!\n";
			break;

		case PA_STREAM_FAILED:
		default:
			fprintf(stderr, "failed with stream error: %s\n", pa_strerror(pa_context_errno(pa_stream_get_context(s))));
			mainloop_api->quit(mainloop_api, 1);
			break;
	}
}

void handle_context_state_change(pa_context *c, void *) {
	assert(c);

	switch (pa_context_get_state(c)) {
		case PA_CONTEXT_CONNECTING:
		case PA_CONTEXT_AUTHORIZING:
		case PA_CONTEXT_SETTING_NAME:
			break;

		case PA_CONTEXT_READY:
			{
				std::cout << "pulse context ready\n";
				// create a blank stream
				read_stream = pa_stream_new(context, "mictoggle read", &in_sample_spec, nullptr);
				if (!read_stream) {
					std::cerr << "pulse stream failed open\n";
					mainloop_api->quit(mainloop_api, 1);
					return;
				}

				// setup read callback + state callback
				pa_stream_set_read_callback(read_stream, handle_new_data, nullptr);
				pa_stream_set_state_callback(read_stream, handle_stream_state, nullptr);

				// TODO: investigate buffer attributes to reduce/increase fragment sizes?
				// TODO: https://freedesktop.org/software/pulseaudio/doxygen/structpa__buffer__attr.html#a2877c9500727299a2d143ef0af13f908

				// open the recorder stream
				if (pa_stream_connect_record(read_stream, DEVICE_NAME, nullptr, (pa_stream_flags_t)0) < 0) {
					fprintf(stderr, "pa_stream_connect_record() failed: %s\n", pa_strerror(pa_context_errno(c)));
					mainloop_api->quit(mainloop_api, 1);
					return;
				}
			}
			break;

		case PA_CONTEXT_TERMINATED:
			mainloop_api->quit(mainloop_api, 0);
			break;

		case PA_CONTEXT_FAILED:
		default:
			fprintf(stderr, "Connection failure: %s\n", pa_strerror(pa_context_errno(c)));
			mainloop_api->quit(mainloop_api, 1);
			break;
	}
}

int main(int argc, char **argv) {
	int err = -1;

	// pulse loop object
	pa_mainloop *m = pa_mainloop_new();
	if (m == nullptr) {
		std::cerr << "failed to create mainloop\n";
		return 1;
	}

	pulse_object_destroyer<pa_mainloop, pa_mainloop_free> m_dctx(m);

	mainloop_api = pa_mainloop_get_api(m);

	// setup signal handling
	pa_signal_init(mainloop_api);

	// register handlers for int/term
	pa_signal_new(SIGINT, handle_exit, nullptr);
	pa_signal_new(SIGTERM, handle_exit, nullptr);

	// create a new connection context
	if (!(context = pa_context_new(mainloop_api, "mictoggle"))) {
		std::cerr << "failed to context new\n";
		return 1;
	}

	pulse_object_destroyer<pa_context, pa_context_disconnect, pa_context_unref> context_dctx(context);

	// set state callback
	pa_context_set_state_callback(context, handle_context_state_change, nullptr);

	// connect context
	if (pa_context_connect(context, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0) {
		std::cerr << "failed to connect\n";
		return 1;
	}

	pulse_object_destroyer<pa_stream, pa_stream_unref> read_stream_dctx(read_stream); // make sure this is destructed at the right time

	// start mainloop
	std::cout << "ready, starting mainloop\n";
	if (pa_mainloop_run(m, &err) < 0) {
		std::cerr << "mainloop run failed\n";
	}

	// all quit calls come back here, invoking the object destroyers in the right order

	std::cout << "Cleaning up\n";
	pa_signal_done();

	return err;
}
