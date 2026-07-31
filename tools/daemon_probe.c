/* Sends one request to meegramd and prints what comes back.
 *
 * The documented way to do this was socat, which is not on the device and not packaged for
 * it. This is the same thing in a form that always builds:
 *
 *   arm-none-linux-gnueabi-gcc -static -O2 -o daemon_probe tools/daemon_probe.c
 *
 * -static on purpose. It has no dependency worth resolving, and a static binary needs
 * none of the loader and rpath flags every other target here carries to run on Harmattan.
 *
 * The daemon broadcasts every line TDLib produces to every client, so a probe sees the
 * whole update stream, not just its own answer. Hence the optional filter: pass the
 * "@extra" you sent and only matching lines are printed.
 *
 *   ./daemon_probe '{"@type":"getOption","name":"version","@extra":"probe"}' probe
 *
 * The daemon only accepts connections from the meegram binary, so it has to be started
 * with --trust-any-peer for this to get in - which is what that flag is for.
 */

#include <sys/socket.h>
#include <sys/un.h>

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Must agree with socketPath() in src/daemon/main.cpp. */
static void socket_path(char *out, size_t size)
{
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (runtime_dir && *runtime_dir)
    {
        snprintf(out, size, "%s/meegram.sock", runtime_dir);
        return;
    }

    const char *home = getenv("HOME");
    snprintf(out, size, "%s/.meegram/sock", home ? home : ".");
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: %s <json request> [filter] [seconds]\n", argv[0]);
        return 2;
    }

    const char *filter = argc > 2 ? argv[2] : NULL;
    const int seconds = argc > 3 ? atoi(argv[3]) : 15;

    char path[128];
    socket_path(path, sizeof(path));

    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        perror("socket");
        return 1;
    }

    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;

    /* Refused rather than truncated. A path silently cut to fit connects to the wrong
     * socket, or to nothing, and reads as "the daemon is not running". */
    if (strlen(path) >= sizeof(address.sun_path))
    {
        fprintf(stderr, "socket path too long: %s\n", path);
        return 1;
    }

    strcpy(address.sun_path, path);

    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        fprintf(stderr, "connect %s: %s\n", path, strerror(errno));
        return 1;
    }

    /* One JSON object per line, which is the whole framing. */
    const size_t length = strlen(argv[1]);

    if (write(fd, argv[1], length) != (ssize_t)length || write(fd, "\n", 1) != 1)
    {
        perror("write");
        return 1;
    }

    fprintf(stderr, "sent, reading for %ds\n", seconds);

    /* Line assembly, because a read boundary is not a line boundary - the same reason the
     * daemon and the client both carry a pending buffer. */
    char pending[1 << 16];
    size_t used = 0;

    for (int elapsed = 0; elapsed < seconds;)
    {
        struct pollfd waiting;
        waiting.fd = fd;
        waiting.events = POLLIN;
        waiting.revents = 0;

        const int ready = poll(&waiting, 1, 1000);
        if (ready < 0)
        {
            if (errno == EINTR)
                continue;

            perror("poll");
            return 1;
        }

        if (ready == 0)
        {
            ++elapsed;
            continue;
        }

        /* A single response can be most of a megabyte - the full language pack is. Rather
         * than grow the buffer, drop what has accumulated and say so: a probe that
         * silently truncates is worse than no probe. Checked before the read so the space
         * left is never zero, which read() would report as a closed connection. */
        if (used + 2 >= sizeof(pending))
        {
            fprintf(stderr, "line longer than %u bytes; dropping it\n", (unsigned)sizeof(pending));
            used = 0;
        }

        const ssize_t n = read(fd, pending + used, sizeof(pending) - used - 1);
        if (n == 0)
        {
            fprintf(stderr, "daemon closed the connection - started without --trust-any-peer?\n");
            return 1;
        }
        if (n < 0)
        {
            if (errno == EINTR)
                continue;

            perror("read");
            return 1;
        }

        used += (size_t)n;
        pending[used] = '\0';

        char *line = pending;
        for (char *newline; (newline = strchr(line, '\n')) != NULL; line = newline + 1)
        {
            *newline = '\0';

            if (!filter || strstr(line, filter))
                printf("%s\n", line);
        }

        /* Whatever is left is half a line; keep it for the next read. */
        used = strlen(line);
        memmove(pending, line, used + 1);
    }

    return 0;
}
