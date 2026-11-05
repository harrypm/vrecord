/*
 * vrecord preview reader
 *
 * Reads fixed-size raw RGB24 frames from a FIFO, holds them in a time-based
 * ring buffer for a software playout delay, and writes each frame to the
 * display file as a PPM (lossless), atomically via rename, on a wall-clock
 * schedule so the output runs at the input's native frame rate regardless
 * of bursts or stalls in the writer.
 *
 * usage: vrecord-preview-reader FIFO DISPLAY WIDTH HEIGHT DELAY_SECONDS
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/select.h>
#include <sys/stat.h>

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int main(int argc, char **argv) {
    if (argc != 6) {
        fprintf(stderr, "usage: %s FIFO DISPLAY WIDTH HEIGHT DELAY_SECONDS\n", argv[0]);
        return 1;
    }
    const char *fifo = argv[1];
    const char *display = argv[2];
    long width = atol(argv[3]);
    long height = atol(argv[4]);
    double delay = atof(argv[5]);
    if (width <= 0 || height <= 0 || delay < 0) {
        fprintf(stderr, "invalid arguments\n");
        return 1;
    }

    size_t frame = (size_t)width * (size_t)height * 3;
    /* buffer capacity: delay seconds at up to 60 fps, plus headroom */
    long capacity = (long)(delay * 60.0) + 4;
    unsigned char *ring = malloc(frame * (size_t)capacity);
    double *arrival = malloc(sizeof(double) * (size_t)capacity);
    char header[64];
    int header_len = snprintf(header, sizeof(header), "P6\n%ld %ld\n255\n", width, height);
    char tmp_path[4096];
    if (snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", display) >= (int)sizeof(tmp_path)) {
        fprintf(stderr, "display path too long\n");
        return 1;
    }
    if (!ring || !arrival) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    int fd = open(fifo, O_RDONLY);
    if (fd < 0) {
        perror("open fifo");
        return 1;
    }

    long head = 0;   /* oldest buffered frame */
    long count = 0;  /* frames currently buffered */
    unsigned char *scratch = malloc(frame);
    if (!scratch) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    for (;;) {
        double t = now_seconds();
        /* write out every frame whose playout time has arrived */
        while (count > 0 && arrival[head] + delay <= t) {
            int out = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (out < 0) {
                perror("open tmp");
                return 1;
            }
            ssize_t w = 0, total = 0;
            const unsigned char *framep = ring + (size_t)head * frame;
            while (total < header_len) {
                w = write(out, header + total, (size_t)(header_len - total));
                if (w < 0) { if (errno == EINTR) continue; perror("write"); return 1; }
                total += w;
            }
            total = 0;
            while (total < (ssize_t)frame) {
                w = write(out, framep + total, frame - (size_t)total);
                if (w < 0) { if (errno == EINTR) continue; perror("write"); return 1; }
                total += w;
            }
            close(out);
            if (rename(tmp_path, display) != 0) {
                perror("rename");
                return 1;
            }
            head = (head + 1) % capacity;
            count--;
            t = now_seconds();
        }

        /* wait for the next frame, or until the next buffered frame is due */
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        struct timeval tv;
        struct timeval *tvp = NULL;
        if (count > 0) {
            double wait = arrival[head] + delay - t;
            if (wait < 0) wait = 0;
            tv.tv_sec = (time_t)wait;
            tv.tv_usec = (suseconds_t)((wait - (double)tv.tv_sec) * 1e6);
            tvp = &tv;
        }
        int ready = select(fd + 1, &rfds, NULL, NULL, tvp);
        if (ready < 0) {
            if (errno == EINTR) continue;
            perror("select");
            return 1;
        }
        if (ready == 0) {
            continue; /* a buffered frame is due: loop and write it out */
        }

        /* read one whole frame (blocking; the first byte is already here) */
        size_t got = 0;
        while (got < frame) {
            ssize_t n = read(fd, scratch + got, frame - got);
            if (n < 0) {
                if (errno == EINTR) continue;
                perror("read");
                return 1;
            }
            if (n == 0) {
                /* writer closed the fifo: flush nothing further, exit cleanly */
                return 0;
            }
            got += (size_t)n;
        }
        if (count >= capacity) {
            /* writer burst beyond the buffer: drop the oldest frame to stay live */
            head = (head + 1) % capacity;
            count--;
        }
        long slot = (head + count) % capacity;
        memcpy(ring + (size_t)slot * frame, scratch, frame);
        arrival[slot] = now_seconds();
        count++;
    }
}
