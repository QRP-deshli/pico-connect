/*
 * platform.c -- OS-specific code: sleep, raw-mode console, serial port.
 */
#include "config.h"
#include "platform.h"

/* ─── Non-blocking single-key console (Windows + POSIX) ──────────────── */

void console_init(console_t *console) {
#ifdef _WIN32
    (void)console;
#else
    console->fd = STDIN_FILENO;
    if (tcgetattr(console->fd, &console->original_termios) != 0) {
        return; /* stdin is not a terminal (pipe/redirect): nothing to do */
    }
    struct termios raw_termios = console->original_termios;
    cfmakeraw(&raw_termios);
    /* cfmakeraw() clears OPOST, so a bare '\n' printed by this app would
     * move the cursor down without returning to column 0 (the "staircase"
     * shifted-text bug). We only want raw INPUT (single keystrokes, no
     * line editing, no echo), so turn output post-processing back on:
     * OPOST enables it, ONLCR maps '\n' -> '\r\n'. */
    raw_termios.c_oflag |= (OPOST | ONLCR);
    tcsetattr(console->fd, TCSANOW, &raw_termios);
#endif
}

void console_cleanup(console_t *console) {
#ifdef _WIN32
    (void)console;
#else
    tcsetattr(console->fd, TCSADRAIN, &console->original_termios);
#endif
}

bool console_getkey(console_t *console, int timeout_ms, uint8_t *key_out) {
#ifdef _WIN32
    (void)console;
    DWORD start_tick = GetTickCount();
    while ((GetTickCount() - start_tick) < (DWORD)timeout_ms) {
        if (_kbhit()) {
            int ch = _getch();
            if (ch == EXTENDED_KEY_PREFIX_1 || ch == EXTENDED_KEY_PREFIX_2) {
                _getch(); /* discard the extended-key second byte */
                continue;
            }
            *key_out = (uint8_t)ch;
            return true;
        }
        Sleep(CONSOLE_POLL_GRANULARITY_MS);
    }
    return false;
#else
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(console->fd, &read_fds);

    struct timeval timeout;
    timeout.tv_sec = timeout_ms / MILLISECONDS_PER_SECOND;
    timeout.tv_usec = (timeout_ms % MILLISECONDS_PER_SECOND) * MICROSECONDS_PER_MILLISECOND;

    int select_result = select(console->fd + 1, &read_fds, NULL, NULL, &timeout);
    if (select_result > 0) {
        uint8_t byte_value;
        ssize_t bytes_read = read(console->fd, &byte_value, 1);
        if (bytes_read == 1) {
            *key_out = byte_value;
            return true;
        }
    }
    return false;
#endif
}

/* ─── Serial port I/O (platform-specific) ────────────────────────────── */

#ifdef _WIN32

bool serial_baud_supported(int baud_rate) {
    return baud_rate > 0;
}

serial_handle_t serial_open(const char *device_path, int baud_rate) {
    char full_path[MAX_PATH_LEN];
    snprintf(full_path, sizeof(full_path), "\\\\.\\%s", device_path);

    HANDLE handle = CreateFileA(full_path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                                 OPEN_EXISTING, 0, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        return INVALID_SERIAL_HANDLE;
    }

    DCB dcb;
    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(handle, &dcb)) {
        CloseHandle(handle);
        return INVALID_SERIAL_HANDLE;
    }
    dcb.BaudRate = (DWORD)baud_rate;
    dcb.ByteSize = SERIAL_DATA_BITS;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    /* USB-CDC firmware (the Pico SDK's stdio_usb backend included) often
     * gates all stdout on tud_cdc_connected(), which just reflects
     * whether the host has DTR asserted -- i.e. whether "a real terminal
     * is attached". Most serial terminals assert DTR/RTS on every
     * open(); explicitly pin the same state here rather than trust
     * whatever this driver happens to default to. Also disable all flow
     * control so a stale XON/XOFF or hardware-handshake setting left
     * over from a previous session can't stall reads/writes. */
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fNull = FALSE;
    dcb.fAbortOnError = FALSE;
    if (!SetCommState(handle, &dcb)) {
        CloseHandle(handle);
        return INVALID_SERIAL_HANDLE;
    }
    /* Some USB-CDC virtual COM drivers only actually toggle the line
     * when explicitly told to via EscapeCommFunction, regardless of what
     * fDtrControl/fRtsControl say -- belt and suspenders. */
    EscapeCommFunction(handle, SETDTR);
    EscapeCommFunction(handle, SETRTS);

    COMMTIMEOUTS timeouts;
    memset(&timeouts, 0, sizeof(timeouts));
    /* Finite, nonzero read timeout: ReadIntervalTimeout and ReadTotalTimeoutMultiplier
     * left at 0, only ReadTotalTimeoutConstant set. Combined with
     * serial_read() below requesting only as many bytes as are already
     * queued (never a large fixed chunk), this gives
     * "return the instant data shows up, else give up after
     * ReadTotalTimeoutConstant ms" behavior --
     * without leaning on the MAXDWORD/MAXDWORD special case, which not
     * every USB-CDC virtual COM driver honors consistently. */
    timeouts.ReadIntervalTimeout = 0;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = SERIAL_READ_TIMEOUT_MS;
    timeouts.WriteTotalTimeoutConstant = SERIAL_WRITE_TIMEOUT_MS;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    if (!SetCommTimeouts(handle, &timeouts)) {
        CloseHandle(handle);
        return INVALID_SERIAL_HANDLE;
    }

    return handle;
}

void serial_close(serial_handle_t serial) {
    CloseHandle(serial);
}

long serial_read(serial_handle_t serial, uint8_t *buf, size_t buf_size) {
    /* Request exactly as many bytes as are already queued (or 1, to
     * still block up to the read timeout waiting for the first one).
     * Requesting a large fixed chunk instead (e.g. always buf_size)
     * makes several USB-CDC virtual COM drivers wait out the whole
     * timeout window hoping to fill more of the request, even after
     * some data has already arrived -- that shows up as every echoed
     * character lagging behind by up to the timeout's length. */
    DWORD error_flags = 0;
    COMSTAT comm_status;
    if (!ClearCommError(serial, &error_flags, &comm_status)) {
        return -1;
    }

    DWORD bytes_to_request = comm_status.cbInQue;
    if (bytes_to_request == 0) {
        bytes_to_request = 1;
    }
    if ((size_t)bytes_to_request > buf_size) {
        bytes_to_request = (DWORD)buf_size;
    }

    DWORD bytes_read = 0;
    BOOL success = ReadFile(serial, buf, bytes_to_request, &bytes_read, NULL);
    if (!success) {
        return -1;
    }
    return (long)bytes_read;
}

bool serial_write_all(serial_handle_t serial, const uint8_t *data, size_t length) {
    DWORD bytes_written = 0;
    BOOL success = WriteFile(serial, data, (DWORD)length, &bytes_written, NULL);
    if (!success || bytes_written != length) {
        return false;
    }
    return true;
}

#else /* POSIX */

typedef struct {
    int rate;
    speed_t constant;
} baud_entry_t;

static const baud_entry_t POSIX_BAUD_TABLE[] = {
    {1200, B1200}, {2400, B2400}, {4800, B4800}, {9600, B9600},
    {19200, B19200}, {38400, B38400}, {57600, B57600},
    {115200, B115200}, {230400, B230400},
};
#define POSIX_BAUD_TABLE_COUNT (sizeof(POSIX_BAUD_TABLE) / sizeof(POSIX_BAUD_TABLE[0]))

static bool posix_baud_constant(int baud_rate, speed_t *out_constant) {
    for (size_t i = 0; i < POSIX_BAUD_TABLE_COUNT; i++) {
        if (POSIX_BAUD_TABLE[i].rate == baud_rate) {
            *out_constant = POSIX_BAUD_TABLE[i].constant;
            return true;
        }
    }
    return false;
}

bool serial_baud_supported(int baud_rate) {
    speed_t unused_constant;
    return posix_baud_constant(baud_rate, &unused_constant);
}

serial_handle_t serial_open(const char *device_path, int baud_rate) {
    int fd = open(device_path, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        return INVALID_SERIAL_HANDLE;
    }

    speed_t baud_constant;
    if (!posix_baud_constant(baud_rate, &baud_constant)) {
        close(fd);
        return INVALID_SERIAL_HANDLE;
    }

    struct termios options;
    if (tcgetattr(fd, &options) != 0) {
        close(fd);
        return INVALID_SERIAL_HANDLE;
    }

    cfsetispeed(&options, baud_constant);
    cfsetospeed(&options, baud_constant);
    cfmakeraw(&options);
    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~(unsigned int)PARENB;
    options.c_cflag &= ~(unsigned int)CSTOPB;
    options.c_cflag &= ~(unsigned int)CSIZE;
    options.c_cflag |= CS8;
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = SERIAL_READ_TIMEOUT_DECISECONDS;

    if (tcsetattr(fd, TCSANOW, &options) != 0) {
        close(fd);
        return INVALID_SERIAL_HANDLE;
    }

    /* See the matching comment in the Windows serial_open(): explicitly
     * assert DTR/RTS, since firmware that gates stdout on tud_cdc_connected() (i.e. on
     * DTR) won't print anything otherwise. */
    int control_bits = 0;
    if (ioctl(fd, TIOCMGET, &control_bits) == 0) {
        control_bits |= TIOCM_DTR | TIOCM_RTS;
        ioctl(fd, TIOCMSET, &control_bits);
    }

    return fd;
}

void serial_close(serial_handle_t serial) {
    close(serial);
}

long serial_read(serial_handle_t serial, uint8_t *buf, size_t buf_size) {
    /* When a USB CDC board is unplugged or resets, Linux hangs up the tty.
     * From then on read() returns 0 (EOF) -- which is indistinguishable
     * from an ordinary VTIME read timeout -- so a plain read() loop would
     * spin forever and never notice the board is gone. Wait with poll()
     * instead, so a hangup/error is reported explicitly as POLLHUP/POLLERR/
     * POLLNVAL, and so "readable but read() returned 0" can only mean EOF. */
    struct pollfd pfd;
    pfd.fd = serial;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int poll_result = poll(&pfd, 1, SERIAL_READ_TIMEOUT_MS);
    if (poll_result < 0) {
        if (errno == EINTR) {
            return 0;
        }
        return -1;
    }
    if (poll_result == 0) {
        return 0; /* plain timeout, no data yet */
    }

    if (pfd.revents & POLLIN) {
        /* Drain any data still queued before reporting a hangup. */
        ssize_t result = read(serial, buf, buf_size);
        if (result > 0) {
            return (long)result;
        }
        if (result == 0) {
            return -1; /* readable but zero bytes: EOF, device is gone */
        }
        if (errno == EAGAIN || errno == EINTR) {
            return 0;
        }
        return -1;     /* EIO / ENODEV / ENXIO: device is gone */
    }

    /* POLLHUP / POLLERR / POLLNVAL with no data left to read. */
    return -1;
}

bool serial_write_all(serial_handle_t serial, const uint8_t *data, size_t length) {
    size_t total_written = 0;
    while (total_written < length) {
        ssize_t written = write(serial, data + total_written, length - total_written);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        total_written += (size_t)written;
    }
    return true;
}

#endif /* POSIX serial I/O */
