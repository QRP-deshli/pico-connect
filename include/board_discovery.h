/*
 * board_discovery.h -- finding Pico-family boards among the OS's serial
 * ports, letting the user pick one, and re-locating it after a reset.
 * Implemented in src/board_discovery.c.
 */
#ifndef BOARD_DISCOVERY_H
#define BOARD_DISCOVERY_H

#include "config.h"

typedef struct {
    char device[MAX_PATH_LEN];
    char description[MAX_DESC_LEN];
    char manufacturer[MAX_DESC_LEN];
    char product[MAX_DESC_LEN];
    char serial_number[MAX_SERIAL_LEN];
    uint16_t vid;
    uint16_t pid;
    bool has_serial_number;
} port_info_t;

size_t enumerate_all_ports(port_info_t *out_ports, size_t max_ports);
size_t find_boards(port_info_t *out_boards, size_t max_boards);
void describe(const port_info_t *port, char *out, size_t out_size);
/* Returns the chosen board, or NULL if input closes before a choice is made. */
const port_info_t *choose(const port_info_t *boards, size_t board_count);

/* Re-locates the board after it drops off the bus: matches by USB serial
 * number, falling back to the previous device path. */
bool find_port_by_serial(const char *serial_number, const char *fallback_device,
                         port_info_t *out_port);

#endif /* BOARD_DISCOVERY_H */
