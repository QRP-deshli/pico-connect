/*
 * config.h -- feature-test macros, standard headers and the general
 * sizing / timing constants shared by every module.
 *
 * MUST be the first header included by every .c file, because the
 * feature-test macros below have to be defined before any system header.
 */
#ifndef CONFIG_H
#define CONFIG_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

/* ─── General sizing / timing constants ──────────────────────────────── */

#define MAX_PATH_LEN            260
#define MAX_DESC_LEN            256
#define MAX_SERIAL_LEN          128
#define MAX_BOARDS              64
#define DESCRIBE_BUF_LEN        320
#define INPUT_LINE_LEN          64
#define READER_CHUNK_SIZE       512
#define COM_PORT_NAME_LEN       16

#define DEFAULT_BAUD            115200
#define DECIMAL_BASE            10
#define HEXADECIMAL_BASE        16

#define MILLISECONDS_PER_SECOND      1000
#define MICROSECONDS_PER_MILLISECOND 1000

#define EXIT_CHAR               0x1D  /* Ctrl+] */
#define OPEN_RETRY_ATTEMPTS     20    /* covers the brief "Access is denied" window */
#define OPEN_RETRY_GAP_MS       250   /* right after a reset, before the OS finishes */
#define RECONNECT_POLL_MS       300   /* rebinding to the re-enumerated device */
#define CONSOLE_KEY_POLL_MS     100
#define CONSOLE_POLL_GRANULARITY_MS 20
#define READER_JOIN_TIMEOUT_MS  1000
#define SERIAL_READ_TIMEOUT_MS  100   /* Windows COMMTIMEOUTS */
#define SERIAL_WRITE_TIMEOUT_MS 1000
#define SERIAL_READ_TIMEOUT_DECISECONDS 1 /* POSIX VTIME, units of 0.1s */
#define SERIAL_DATA_BITS        8

#define EXTENDED_KEY_PREFIX_1   0x00
#define EXTENDED_KEY_PREFIX_2   0xE0

#define USB_DIR_SEARCH_MAX_LEVELS 6
#define SYSFS_HEX_FIELD_LEN     8
#define GENERATED_LOCATION_ID_AMPERSAND_COUNT 3

#endif /* CONFIG_H */
