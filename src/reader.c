/*
 * reader.c -- the serial reader thread: echoes board output to stdout and
 * raises link_lost when the port goes away.
 */
#include "config.h"
#include "reader.h"
#include "platform.h"

#ifdef _WIN32
DWORD WINAPI reader_thread_main(LPVOID arg) {
#else
void *reader_thread_main(void *arg) {
#endif
    reader_context_t *ctx = (reader_context_t *)arg;
    uint8_t read_buf[READER_CHUNK_SIZE];

    while (!flag_is_set(ctx->link_lost) && !flag_is_set(ctx->user_quit)) {
        long bytes_read = serial_read(ctx->serial, read_buf, sizeof(read_buf));
        if (bytes_read < 0) {
            flag_set(ctx->link_lost);
            break;
        }
        if (bytes_read == 0) {
            continue;
        }

        fwrite(read_buf, 1, (size_t)bytes_read, stdout);
        fflush(stdout);
    }

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}
