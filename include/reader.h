/*
 * reader.h -- the serial reader thread that echoes everything the board
 * sends to the local console. Implemented in src/reader.c.
 */
#ifndef READER_H
#define READER_H

#include "config.h"
#include "platform.h"

typedef struct {
    serial_handle_t serial;
    flag_t *link_lost;
    flag_t *user_quit;
} reader_context_t;

/* Thread entry point -- signature differs per platform. */
#ifdef _WIN32
DWORD WINAPI reader_thread_main(LPVOID arg);
#else
void *reader_thread_main(void *arg);
#endif

#endif /* READER_H */
