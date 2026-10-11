# pico-connect

A small, dependency-free serial terminal for **Raspberry Pi Pico / Pico 2** and **WIZnet EVB-Pico / EVB-Pico2** boards (RP2040 and RP2350) that talk over USB CDC.

It finds your board automatically, opens an interactive terminal to it, and - the main reason it exists - **survives board resets**. When the board reboots (reset button, watchdog, flashing, USB replug) its serial port disappears and comes back. `pico-connect` notices, waits for the same board to re-enumerate, and reconnects on its own, so you never have to restart the terminal.

Written in plain C11, runs on **Linux** and **Windows**.

Unofficial tool: not affiliated with or endorsed by Raspberry Pi or WIZnet.

## Features

- Automatic detection of Pico-family boards among the system's serial ports
- Board picker when more than one board is connected
- Automatic reconnect after reset or replug, matching the board by its USB serial number (falls back to the same port path if the board reports none)
- Raw, unbuffered keystroke forwarding (no line editing, no local echo)
- Asserts DTR/RTS on open, so firmware that waits for a host terminal before printing (the Pico SDK's `stdio_usb`) works out of the box
- Retries opening the port for about 5 seconds, covering the short window right after a reset when the OS still holds the port
- No third-party libraries

## Supported boards

Any board that enumerates as a USB CDC serial device and either:

- reports the Raspberry Pi USB vendor ID `0x2E8A` (all Pico SDK / TinyUSB default builds do, including the WIZnet EVB-Pico boards), or
- has `pico`, `rp2040`, `rp2350`, `wiznet` or `board cdc` in its USB description, manufacturer or product string.

On the firmware side, enable USB stdio in your Pico SDK project:

```cmake
pico_enable_stdio_usb(your_target 1)
```

## Build

You need `make` and a C11 compiler (`gcc`).

### Linux

```sh
make          # builds ./pico-connect
```

### Windows

Native, using MinGW-w64 (for example through [MSYS2](https://www.msys2.org/)):

```sh
make          # builds pico-connect.exe
```

Cross-compile from Linux (needs the `x86_64-w64-mingw32-gcc` package):

```sh
make windows
```

### Other targets

```sh
make linux    # force the POSIX build
make clean    # remove build/ and the built binaries
```

## Usage

```
pico-connect [--baud RATE] [--list]
```

| Option        | Description                                      |
| ------------- | ------------------------------------------------ |
| `--baud RATE` | Baud rate to open the port with (default 115200). Unsupported rates are rejected at startup |
| `--list`      | List detected boards and exit, without connecting |

Run it with no arguments to scan, pick a board (automatic if only one is found) and connect:

```sh
./pico-connect
```

Example `--list` output:

```
/dev/ttyACM0    VID:PID=2E8A:000A  SN=<usb-serial>  Pico
```

Press **Ctrl+]** to quit, including while the program is waiting for the board to reappear. Every other key is sent to the board as-is.

> USB CDC ignores the baud rate on the wire, so `--baud` only matters for devices that actually use it. On Linux the supported rates are 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200 and 230400.

### Linux permissions

Your user needs read/write access to the board's `/dev/ttyACM*` device. On most distributions that means being in the `dialout` group (`uucp` on Arch):

```sh
sudo usermod -aG dialout "$USER"   # then log out and back in
```

## How reconnecting works

1. A reader thread echoes everything the board sends to your terminal.
2. If the read or write fails, the link is considered lost.
3. The program polls the list of boards until the one with the same USB serial number shows up again (or, if the board has no serial number, until the same port path reappears).
4. It reopens the port and resumes the session.

Boards that do not report a USB serial number are still supported. With several identical such boards connected, reconnecting is less certain because only the port path can be matched.

## Project layout

```
src/
  main.c              CLI, terminal loop, reconnect logic
  platform.c          sleep, raw console input, serial port (Windows + POSIX)
  board_discovery.c   finding and choosing boards (SetupAPI on Windows, sysfs on Linux)
  reader.c            reader thread that echoes board output
include/              matching headers
Makefile
```

## Platform notes

- **Linux:** board discovery reads `/sys/class/tty`, so macOS and other non-Linux Unixes are not supported.
- **Windows:** boards are found through SetupAPI and opened as `COMx` ports. Extended keys (arrows, function keys) are not forwarded.
