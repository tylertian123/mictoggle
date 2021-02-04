# mictoggle

Tired of moving the mouse to press unmute in an online meeting?
`mictoggle` allows you to mute/unmute the mic by pressing the media button on your wired earbuds!

Partially inspired by [roligheten/AndroidMediaControlsWindows](https://github.com/roligheten/AndroidMediaControlsWindows).

Note `mictoggle` only works on Linux and has only been tested on Ubuntu.

## Features

- Creates a remapped mic so you can choose between muted and unmuted inputs
- Allows you to mute/unmute the mic with a single press of the media button
- Shows desktop notifications (`libnotify`) when muting/unmuting
- Detects when the mic/earbuds are plugged in/removed and mutes the mic automatically
- No false positives or missed detection of presses so far!

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

The remapped device is created with the `module-remap-source` module and will be called `mictoggle_remapped` (you can't change this currently).
When `mictoggle` is killed, the module will be unloaded (even if `mictoggle` did not load it, i.e. it already existed).

## Desktop Entry

You can make `mictoggle` launchable from the desktop environment as an application using the provided `mictoggle.desktop`.

Make sure to replace `<device>` in the file with your device name as mentioned in the Usage section.
Place the `mictoggle` binary in `/usr/local/bin`, or change the `Path` entry in the file to where `mictoggle` is located. Place this file in the correct directory (`/usr/share/applications/` or `/usr/local/share/applications/`).

The desktop entry also includes an "Exit mictoggle" action (requires `pkill`) so you can easily kill mictoggle when you're done with it (since it does not have a window).
