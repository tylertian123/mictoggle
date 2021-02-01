# mictoggle

Tired of moving the mouse to press unmute in an online meeting?
`mictoggle` allows you to mute/unmute the mic by pressing the media button on your wired earbuds!

Partially inspired by [roligheten/AndroidMediaControlsWindows](https://github.com/roligheten/AndroidMediaControlsWindows).

Note `mictoggle` only works on Linux and has only been tested on Ubuntu.

## Building

You need the following libraries:

- `libpulse` (`sudo apt install libpulse-dev`)
- `libnotify` (`sudo apt install libnotify-dev`)

To build in bash:

```sh
g++ -Wall mictoggle.cpp -o mictoggle -std=c++17 `pkg-config --cflags --libs libpulse libnotify`
```

To build in fish:

```sh
g++ -Wall mictoggle.cpp -o mictoggle -std=c++17 (pkg-config --cflags --libs libpulse libnotify | string split " ")
```

## Usage

`mictoggle` detects button presses by reading the microphone input. To do this, it uses PulseAudio to create a "remapped" microphone separate from the main mic.
*This is the one that will actually be muted/unmuted when you press the button, so make sure you tell your browser/Zoom/other meeting app to use the **remapped microphone** instead.*
The remapped mic will typically have "Remapped" in the name, e.g. "Remapped Built-in Audio Analog Stereo".

To create this remapped mic, you must first give `mictoggle` the device name of the *real* mic (from which button presses will be detected). You can find this with `pactl list sources`, and it might look something like `alsa_input.pci-0000_00_1f.3.analog-stereo`.

When running, pass the device name of the original mic as a command-line argument:

```sh
mictoggle <device name here> 
```

The device name of the remapped device will be `mictoggle_remapped`.
You probably won't need it, and currently it can't be changed.
