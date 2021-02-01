/*
 * Usage:
 *      mictoggle [device name]
 * 
 * Device name can be found through `pactl list sources`.
 * Looks something like "alsa_input.pci-0000_00_1f.3.analog-stereo".
 */

#include <iostream>
#include <limits>
#include <string>
#include <cstdlib>
#include <stdint.h>
#include <signal.h>
#include <pulse/simple.h>
#include <pulse/pulseaudio.h>
#include <libnotify/notify.h>

constexpr uint32_t SAMPLE_RATE = 4096;
constexpr int BLOCK_SIZE = 64;

constexpr int THRESHOLD_PERCENT = 95;
constexpr int PEAK_BLOCK_COUNT = 5;
constexpr int16_t THRESHOLD_VALUE = std::numeric_limits<int16_t>::max() * THRESHOLD_PERCENT / 100;
static const char * REMAPPED_DEVICE_NAME = "mictoggle_remapped";

static const char *device_name = nullptr;
static bool muted = true;

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

pa_sample_spec in_sample_spec = {
	.format = PA_SAMPLE_S16NE,
	.rate = SAMPLE_RATE,
	.channels = 2,
};

pa_mainloop_api *mainloop_api = nullptr;
pa_context *context = nullptr;
pa_stream *read_stream = nullptr;
pa_stream *remapped_stream = nullptr;

int check_press(int16_t average) {
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
     * 
     * Returns 0 if no press occurred, 1 if pressed down, 2 if released.
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
            block_count = 0;
			peak_count ++;
			if (peak_count == 1) {
				return 1;
			}
			// Peak count 2
			else {
				peak_count = 0;
                return 2;
			}
		}
		block_count = 0;
	}
    return 0;
}

void handle_mute_completion(pa_context *c, int success, void *data) {
    bool *muted = reinterpret_cast<bool*>(data);
    if (!success) {
        std::cerr << (*muted ? "Mute" : "Unmute") << " failed: " << pa_strerror(pa_context_errno(context)) << "\n";
        mainloop_api->quit(mainloop_api, 1);
        return;
    }
    std::cout << "Mic is now " << (*muted ? "muted" : "unmuted") << "\n";
	// ID of the last notification
	static gint last_id = -1;
	NotifyNotification *notif = *muted
		? notify_notification_new("mictoggle", "Microphone muted", "microphone-sensitivity-muted-symbolic")
		: notify_notification_new("mictoggle", "Microphone unmuted", "audio-input-microphone-symbolic");
	notify_notification_set_timeout(notif, 1000);
	notify_notification_set_hint_int32(notif, "transient", 1);
	// Set the ID of the new notification if this isn't the first
	// this makes the new notification replace the old one
	if (last_id != -1) {
		g_object_set(notif, "id", last_id, nullptr);
	}
	notify_notification_show(notif, nullptr);
	g_object_get(notif, "id", &last_id, nullptr);
	g_object_unref(notif);
}

void handle_exit(pa_mainloop_api *m, pa_signal_event *e, int sig, void *) {
	assert(m);

	std::cerr << "got exit signal, killing" << std::endl;
	m->quit(m, 0); // tell run to exit
}

void handle_new_data(pa_stream *s, size_t length, void *userdata) {
    static int sample_count = 0;
    static long sample_sum = 0;
	const void *data;
    assert(s);
    assert(length > 0);

    if ((pa_stream_peek(s, &data, &length))) {
        std::cerr << "pa_stream_peek() failed: " << pa_strerror(pa_context_errno(context)) << "\n";
        mainloop_api->quit(mainloop_api, 1);
        return;
    }

    if (!length) {
        return;
    }
    bool toggle = false;
    if (data) {
        const int16_t *idata = reinterpret_cast<const int16_t*>(data);
        for (size_t i = 0; i < length / sizeof(int16_t); i ++) {
            sample_sum += idata[i];
            if (++sample_count == BLOCK_SIZE) {
                sample_count = 0;
                if (check_press(static_cast<int16_t>(sample_sum / BLOCK_SIZE)) == 2) {
                    toggle = true;
                }
                sample_sum = 0;
            }
        }
    }
    pa_stream_drop(s);

    if (toggle) {
        muted = !muted;
        pa_operation_unref(pa_context_set_source_mute_by_name(context, REMAPPED_DEVICE_NAME, muted, handle_mute_completion, &muted));
    }
}

// Connects the read stream to the real mic and mutes the remapped mic
void connect_stream() {
	if (pa_stream_connect_record(read_stream, device_name, nullptr, (pa_stream_flags_t)0) < 0) {
		std::cerr << "pa_stream_connect_record() failed: " << pa_strerror(pa_context_errno(context)) << "\n";
		mainloop_api->quit(mainloop_api, 1);
		return;
	}
	muted = true;
	pa_operation_unref(pa_context_set_source_mute_by_name(context, REMAPPED_DEVICE_NAME, muted, handle_mute_completion, &muted));
}

void handle_load_module(pa_context *c, uint32_t idx, void *userdata) {
	if (idx == PA_INVALID_INDEX) {
		std::cerr << "pa_context_load_module() failed: " << pa_strerror(pa_context_errno(context)) << "\n";
		mainloop_api->quit(mainloop_api, 1);
		return;
	}
	std::cerr << "Remapped device created; connecting stream\n";
	connect_stream();
}

void handle_source_info(pa_context *c, const pa_source_info *i, int eol, void *userdata) {
	// If an error occurred then most likely the remapped device doesn't exist
	if (eol < 0) {
		int err = pa_context_errno(c);
		if (err == PA_ERR_NOENTITY) {
			// Create the device if necessary by loading the module-remap-source module
			std::cerr << "Remapped device (" << REMAPPED_DEVICE_NAME << ") does not exist; loading module-remap-source\n";
			if (!device_name) {
				std::cerr << "Cannot create remapped device because main device name not provided!\n";
				mainloop_api->quit(mainloop_api, 1);
				return;
			}
			using std::literals::string_literals::operator""s;
			std::string args = "source_name="s + REMAPPED_DEVICE_NAME + " master=" + device_name
				+ " master_channel_map=front-left,front-right channel_map=front-left,front-right";
			pa_operation_unref(pa_context_load_module(c, "module-remap-source", args.c_str(), handle_load_module, nullptr));
		}
		else {
			std::cerr << "pa_context_get_source_info_by_name() failed: " << pa_strerror(err) << "\n";
			mainloop_api->quit(mainloop_api, 1);
			return;
		}
	}
	if (eol) {
		return;
	}
	// Success!
	std::cerr << "Remapped device (" << REMAPPED_DEVICE_NAME << ") exists; connecting stream\n";
	connect_stream();
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
				
				// Get info about the remapped mic to see if it exists
				// The callback should also connect the stream
				pa_operation_unref(pa_context_get_source_info_by_name(c, REMAPPED_DEVICE_NAME, handle_source_info, nullptr));
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
	notify_init("mictoggle");
	int err = -1;

	if (argc > 1) {
		device_name = argv[1];
		std::cout << "Using device name: " << device_name << "\n";
	}
	else {
		std::cout << "Warning: No device provided, using default device; if remapped device is not created, please pass in device name as a command line argument\n";
	}

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
	notify_uninit();
	pa_signal_done();

	return err;
}
