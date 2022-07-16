# wii2gamepad

Convert the default xwiimote inputs to a different gamepad using xwiimote and evdev.

Currently, `wii2gamepad` will roughly emulate a wireless XBox 360 gamepad.

Since this is a personal project, the current project is in a rough but usable state.

## Building

First install the necessary dependencies:
```
sudo apt install libevdev-dev libxwiimote-dev
```

To build the wii2gamepad binary, run
```
make
```

## Usage

```
wii2gamepad [-m <keymap>] [-r <max retries>] <wiimote number>
```

Use the `-m <keymap>` option to specify a keymap to use. When no keymap is specified, the keymap at `default.cfg` will be used.

Use the `-r <max retries>` option to specify a maximum number of times to retry when failing to open a wiimote or wiimote peripheral. By default, this is 3. Negative numbers will be treated as 0.

Note that wiimotes must be connected via Bluetooth before running `wii2gamepad`.
