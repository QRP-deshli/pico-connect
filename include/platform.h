/*
 * platform.h -- everything OS-specific: system headers, sleep, the tiny
 * flag helpers, the raw-mode single-key console, and the serial port.
 * Implemented in src/platform.c.
 */
#ifndef PLATFORM_H
#define PLATFORM_H

#include "config.h"

#ifdef _WIN32
    #include <windows.h>
    #include <conio.h>
    #include <io.h>
    #include <fcntl.h>
    #include <setupapi.h>
    #define STRNCASECMP _strnicmp
#else
    #include <unistd.h>
    #include <fcntl.h>
    #include <termios.h>
    #include <poll.h>
    #include <sys/select.h>
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <dirent.h>
    #include <limits.h>
    #include <strings.h>
    #include <pthread.h>
    #include <sys/ioctl.h>
    #define STRNCASECMP strncasecmp
#endif

/* ─── Types ───────────────────────────────────────────────────────────── */

typedef volatile int flag_t;

typedef struct {
#ifndef _WIN32
    int fd;
    struct termios original_termios;
#endif
} console_t;

#ifdef _WIN32
typedef HANDLE serial_handle_t;
#define INVALID_SERIAL_HANDLE INVALID_HANDLE_VALUE
#else
typedef int serial_handle_t;
#define INVALID_SERIAL_HANDLE (-1)
#endif

/* ─── Small flag helpers ─────────────────────────────────────────────── */

static inline void flag_clear(flag_t *flag) { *flag = 0; }
static inline void flag_set(flag_t *flag) { *flag = 1; }
static inline bool flag_is_set(flag_t *flag) { return *flag != 0; }

/* ─── Non-blocking single-key console (Windows + POSIX) ──────────────── */

void console_init(console_t *console);
void console_cleanup(console_t *console);
bool console_getkey(console_t *console, int timeout_ms, uint8_t *key_out);

/* ─── Serial port I/O ─────────────────────────────────────────────────── */

bool serial_baud_supported(int baud_rate);
serial_handle_t serial_open(const char *device_path, int baud_rate);
void serial_close(serial_handle_t serial);
long serial_read(serial_handle_t serial, uint8_t *buf, size_t buf_size);
bool serial_write_all(serial_handle_t serial, const uint8_t *data, size_t length);

#endif /* PLATFORM_H */
