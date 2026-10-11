/*
 * main.c
 *
 * Generic serial terminal for Raspberry Pi Pico-family boards
 * (RP2040/RP2350) and WIZnet EVB-Pico boards that enumerate as a USB CDC
 * serial device. Discovers the boards plugged into this PC, opens an
 * interactive terminal to the chosen one, and stays connected across
 * board resets: when the board reboots (reset button, watchdog, USB
 * replug) the CDC device disappears and re-enumerates, so this detects
 * that, waits for the same board (matched by USB serial number) to come
 * back, and reopens the session automatically instead of dying.
 *
 * Layout:
 *   src/main.c              terminal loop, reconnect logic, CLI
 *   src/platform.c          sleep, raw console, serial port (Win + POSIX)
 *   src/board_discovery.c   finding / choosing boards
 *   src/reader.c            reader thread that echoes board output
 *   include/                the matching headers (.h)
 *
 * Build: see the Makefile (make / make linux / make windows).
 *
 * Usage:
 *   connect_pico             scan, pick, connect at 115200
 *   connect_pico --baud 9600
 *   connect_pico --list      just list candidate boards, don't connect
 */

#include "config.h"
#include "platform.h"
#include "board_discovery.h"
#include "reader.h"

static void finish_terminal(console_t *console) {
    printf("\n\nExiting.\n");
    console_cleanup(console);
}

/* Waits up to timeout_ms and returns true as soon as the user presses
 * Ctrl+]. Other keys are discarded: no board is connected to receive
 * them. Used for every wait that happens while no session is running, so
 * the program stays quittable (on POSIX, raw mode turns Ctrl+C into a
 * plain keystroke, so this is the only way out). */
static bool quit_pressed_within(console_t *console, int timeout_ms) {
    uint8_t key;
    if (!console_getkey(console, timeout_ms, &key)) {
        return false;
    }
    return key == EXIT_CHAR;
}

/* Blocks until the board re-enumerates. Returns false if the user pressed
 * Ctrl+] first. */
static bool wait_for_reconnect(console_t *console, const char *serial_number,
                               const char *fallback_device, port_info_t *out_port) {
    printf("\n[!] Connection lost -- waiting for the board to come back "
           "(reset/replug is fine, Ctrl+] to quit)...\n");
    while (true) {
        if (find_port_by_serial(serial_number, fallback_device, out_port)) {
            printf("[!] Board is back on %s. Reconnecting...\n", out_port->device);
            return true;
        }
        if (quit_pressed_within(console, RECONNECT_POLL_MS)) {
            return false;
        }
    }
}

/* ─── The resilient terminal loop ─────────────────────────────────────── */

/* Runs the interactive terminal, and whenever the link drops for any
 * reason other than the user pressing Ctrl+], waits for the same board
 * to reappear and reopens the session -- no restart needed. */
static void run_resilient_terminal(port_info_t port, int baud) {
    console_t console;
    console_init(&console);

    char serial_number[MAX_SERIAL_LEN];
    strncpy(serial_number, port.serial_number, sizeof(serial_number) - 1);
    serial_number[sizeof(serial_number) - 1] = '\0';

    while (true) {
        printf("\nOpening %s @ %d baud...\n", port.device, baud);

        serial_handle_t serial = INVALID_SERIAL_HANDLE;
        bool quit_requested = false;
        for (int attempt = 0; attempt < OPEN_RETRY_ATTEMPTS && !quit_requested; attempt++) {
            serial = serial_open(port.device, baud);
            if (serial != INVALID_SERIAL_HANDLE) {
                break;
            }
            quit_requested = quit_pressed_within(&console, OPEN_RETRY_GAP_MS);
        }
        if (quit_requested) {
            finish_terminal(&console);
            return;
        }

        if (serial == INVALID_SERIAL_HANDLE) {
            printf("Could not open %s (still busy after %d tries) -- "
                   "will keep watching for it.\n", port.device, OPEN_RETRY_ATTEMPTS);
            if (!wait_for_reconnect(&console, serial_number, port.device, &port)) {
                finish_terminal(&console);
                return;
            }
            continue;
        }

        printf("--- Connected on %s  %d,8,N,1 ---\n", port.device, baud);
        printf("--- Ctrl+] to quit. Board resets are handled automatically. ---\n\n");

        flag_t link_lost;
        flag_t user_quit;
        flag_clear(&link_lost);
        flag_clear(&user_quit);

        reader_context_t reader_context;
        reader_context.serial = serial;
        reader_context.link_lost = &link_lost;
        reader_context.user_quit = &user_quit;

#ifdef _WIN32
        HANDLE reader_thread = CreateThread(NULL, 0, reader_thread_main, &reader_context, 0, NULL);
#else
        pthread_t reader_thread;
        pthread_create(&reader_thread, NULL, reader_thread_main, &reader_context);
#endif

        while (!flag_is_set(&link_lost)) {
            uint8_t key;
            bool got_key = console_getkey(&console, CONSOLE_KEY_POLL_MS, &key);
            if (!got_key) {
                continue;
            }
            if (key == EXIT_CHAR) {
                flag_set(&user_quit);
                break;
            }
            if (!serial_write_all(serial, &key, 1)) {
                flag_set(&link_lost);
                break;
            }
        }

        /* The reader thread notices link_lost/user_quit on its own within
         * about SERIAL_READ_TIMEOUT_MS (that's what the read timeout is
         * for), so join it BEFORE closing the handle -- closing a handle
         * a synchronous read is still blocked on is undefined behavior,
         * and Windows in particular tends to recycle a just-closed
         * handle's numeric value almost immediately. If the old reader
         * thread hasn't actually exited yet when the next connection's
         * CreateFile() reuses that value, it can end up silently
         * stealing bytes meant for the new reader thread. */
#ifdef _WIN32
        WaitForSingleObject(reader_thread, READER_JOIN_TIMEOUT_MS);
        CloseHandle(reader_thread);
#else
        pthread_join(reader_thread, NULL);
#endif
        serial_close(serial);

        if (flag_is_set(&user_quit)) {
            finish_terminal(&console);
            return;
        }

        if (!wait_for_reconnect(&console, serial_number, port.device, &port)) {
            finish_terminal(&console);
            return;
        }
    }
}

/* ─── main ────────────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    int baud = DEFAULT_BAUD;
    bool list_only = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--baud") == 0 && i + 1 < argc) {
            baud = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "--list") == 0) {
            list_only = true;
        } else {
            fprintf(stderr, "Usage: %s [--baud RATE] [--list]\n", argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (!serial_baud_supported(baud)) {
        fprintf(stderr, "Unsupported baud rate: %d\n", baud);
        return EXIT_FAILURE;
    }

    port_info_t boards[MAX_BOARDS];
    size_t board_count = find_boards(boards, MAX_BOARDS);

    if (board_count == 0) {
        printf("No Pico-family boards found. Is it plugged in and enumerated as USB CDC?\n");
        printf("All serial ports currently visible to the OS:\n");

        port_info_t all_ports[MAX_BOARDS];
        size_t all_count = enumerate_all_ports(all_ports, MAX_BOARDS);
        char description_buf[DESCRIBE_BUF_LEN];
        for (size_t i = 0; i < all_count; i++) {
            describe(&all_ports[i], description_buf, sizeof(description_buf));
            printf("  %s\n", description_buf);
        }
        return EXIT_FAILURE;
    }

    if (list_only) {
        char description_buf[DESCRIBE_BUF_LEN];
        for (size_t i = 0; i < board_count; i++) {
            describe(&boards[i], description_buf, sizeof(description_buf));
            printf("%s\n", description_buf);
        }
        return EXIT_SUCCESS;
    }

    const port_info_t *chosen_port = choose(boards, board_count);
    if (chosen_port == NULL) {
        fprintf(stderr, "\nNo board selected (input closed).\n");
        return EXIT_FAILURE;
    }
    port_info_t port = *chosen_port;

    if (!port.has_serial_number) {
        printf("\n[note] This board doesn't report a USB serial number, so if you "
               "have several identical boards plugged in, reconnect-after-reset "
               "will fall back to matching the same COM port path -- fine for a "
               "single board, less certain with multiple.\n");
    }

    run_resilient_terminal(port, baud);
    return EXIT_SUCCESS;
}