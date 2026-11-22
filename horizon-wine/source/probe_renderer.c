/* One ElectronProbe renderer per browser. The control handshake precedes raw
 * Chromium bytes on the same loopback connection. Neither peer invents a Mojo
 * packet. The sidecar discovers the ephemeral port on SD, never over the LAN. */
#include "probe_renderer.h"
#include <switch.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define TICKET "sdmc:/switch/wine/logs/ElectronProbe-launch.bin"
#define WAIT_MS 90000
struct ticket { uint64_t nonce; uint16_t port; };
struct hello { uint64_t nonce, pid; };
struct ready { uint32_t pid, tid; uintptr_t peb; };
struct relay { int socket, pipe; unsigned tid; int child; };
struct probe_start probe_child;
static int child_socket = -1, child_pipe = -1, active;
extern void wine_nx_runtime_trace(const char *);

static void note(const char *fmt, ...)
{
    char line[320] = "[EPROBE] ";
    va_list args;
    va_start(args, fmt);
    vsnprintf(line + 9, sizeof(line) - 9, fmt, args);
    va_end(args);
    wine_nx_runtime_trace(line);
}

static uint64_t millis(void) { return armTicksToNs(armGetSystemTick()) / 1000000; }

static int transfer(int fd, void *data, size_t len, int sending)
{
    uint64_t end = millis() + WAIT_MS;
    char *p = data;
    while (len) {
        struct pollfd pollfd = { fd, sending ? POLLOUT : POLLIN, 0 };
        int64_t left = (int64_t)end - (int64_t)millis();
        if (left <= 0 || poll(&pollfd, 1, left) <= 0) return 0;
        ssize_t n = sending ? send(fd, p, len, MSG_DONTWAIT) : recv(fd, p, len, MSG_DONTWAIT);
        if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (n <= 0) return 0;
        p += n;
        len -= n;
    }
    return 1;
}

struct buffer { char bytes[4096]; size_t pos, used; uint64_t total; };
static int pump(int input, int output, int from_socket, struct buffer *b)
{
    if (!b->used) {
        ssize_t n = from_socket ? recv(input, b->bytes, sizeof(b->bytes), MSG_DONTWAIT)
                                : read(input, b->bytes, sizeof(b->bytes));
        if (n == 0) return 0;
        if (n < 0) return errno == EAGAIN || errno == EINTR;
        b->used = n;
        b->pos = 0;
    }
    ssize_t n = from_socket ? write(output, b->bytes + b->pos, b->used)
                            : send(output, b->bytes + b->pos, b->used, MSG_DONTWAIT);
    if (n < 0) return errno == EAGAIN || errno == EINTR;
    if (!n) return 0;
    if (!b->total) note("transport first_%s=%ld", from_socket ? "receive" : "send", (long)n);
    b->total += n;
    b->pos += n;
    b->used -= n;
    return 1;
}

static void *relay_loop(void *arg)
{
    struct relay r = *(struct relay *)arg;
    struct buffer incoming = {0}, outgoing = {0};
    free(arg);
    if (!r.child) {
        uint64_t end = millis() + WAIT_MS;
        int suspended;
        while ((suspended = horizon_probe_suspended(r.tid)) > 0 && millis() < end)
            svcSleepThread(2000000);
        char resume = 'G';
        if (suspended || !transfer(r.socket, &resume, 1, 1)) goto done;
        note("resume wine_tid=%u", r.tid);
    }
    while (pump(r.socket, r.pipe, 1, &incoming) && pump(r.pipe, r.socket, 0, &outgoing))
        svcSleepThread(1000000);
done:
    note("transport closed sent=%llu received=%llu errno=%d",
         (unsigned long long)outgoing.total, (unsigned long long)incoming.total, errno);
    close(r.pipe);
    close(r.socket);
    if (r.child) svcExitProcess();
    horizon_probe_ended(r.tid);
    __atomic_store_n(&active, 0, __ATOMIC_RELEASE);
    return NULL;
}

static int start_relay(int socket, int pipe, unsigned tid, int child)
{
    pthread_t thread;
    struct relay *r = malloc(sizeof(*r));
    if (!r) return 0;
    *r = (struct relay){ socket, pipe, tid, child };
    if (!pthread_create(&thread, NULL, relay_loop, r)) return 1;
    free(r);
    return 0;
}

static int switch_arg(const uint16_t *cmd, unsigned units, const char *key, unsigned *number)
{
    size_t n = strlen(key);
    for (unsigned i = 0; i + n <= units; ++i) {
        if (i && cmd[i-1] != ' ' && cmd[i-1] != '\t') continue;
        size_t j = 0;
        while (j < n && cmd[i+j] == (unsigned char)key[j]) ++j;
        if (j != n) continue;
        unsigned k = i + n, value = 0;
        if (number) {
            if (k == units || cmd[k] < '0' || cmd[k] > '9') return 0;
            while (k < units && cmd[k] >= '0' && cmd[k] <= '9') {
                unsigned digit = cmd[k++] - '0';
                if (value > (INT_MAX-digit)/10) return 0;
                value = value*10 + digit;
            }
        }
        if (number && k < units && cmd[k] == ',' && !strcmp(key, "--field-trial-handle=")) {
            *number = value;
            return 1;
        }
        if (k < units && cmd[k] != ' ' && cmd[k] != '\t') continue;
        if (number) *number = value;
        return 1;
    }
    return 0;
}

/* -1 means the request is outside this experiment. */
int probe_spawn_renderer(const uint16_t *cmd, unsigned units, unsigned pid, unsigned tid,
                         unsigned info, const uintptr_t *handles, size_t handle_count, int *host_pid)
{
    if (probe_child.pid || !switch_arg(cmd, units, "--type=renderer", NULL)) return -1;
    if (__atomic_exchange_n(&active, 1, __ATOMIC_ACQ_REL)) return 0;
    struct probe_start *start = calloc(1, sizeof(*start));
    int listener = -1, peer = -1, pipe = -1, pm = 0, ok = 0, ticket_written = 0;
    u64 launched = 0, self = 0;
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    socklen_t len = sizeof(addr);
    struct ticket ticket = { .nonce = randomGet64() };
    const char *stage = "command";
    if (!start || !units || units >= PROBE_CMD_UNITS ||
        !switch_arg(cmd, units, "--mojo-platform-channel-handle=", &start->channel)) goto done;
    switch_arg(cmd, units, "--field-trial-handle=", &start->trials.handle);
    unsigned required[] = { start->channel, start->trials.handle };
    for (unsigned j = 0; handles && j < 2; ++j) {
        if (!required[j]) continue;
        size_t i = 0;
        while (i < handle_count && handles[i] != required[j]) ++i;
        if (i == handle_count) { stage = "not-in-inherit-list"; goto done; }
    }
    if (start->trials.handle) {
        stage = "field-trial-section";
        if (!horizon_probe_export_trials(&start->trials)) goto done;
        note("field-trial startup snapshot handle=0x%x bytes=%u", start->trials.handle, start->trials.size);
    }
    start->pid = pid;
    start->tid = tid;
    start->units = units;
    memcpy(start->command, cmd, units * 2);
    stage = "export-mojo";
    pipe = horizon_probe_export(start->channel);
    note("channel handle=0x%x exported_fd=%d", start->channel, pipe);
    if (pipe < 0) goto done;
    stage = "listen";
    if ((listener = socket(AF_INET, SOCK_STREAM, 0)) < 0 ||
        bind(listener, (struct sockaddr *)&addr, len) || getsockname(listener, (struct sockaddr *)&addr, &len) ||
        listen(listener, 1)) goto done;
    ticket.port = addr.sin_port;
    stage = "ticket";
    FILE *f = fopen(TICKET, "wb");
    if (!f) goto done;
    ticket_written = 1;
    size_t written = fwrite(&ticket, 1, sizeof(ticket), f);
    if (fclose(f) || written != sizeof(ticket)) goto done;
    stage = "pm-shell";
    Result rc = pmshellInitialize();
    note("pmshellInitialize rc=0x%x", rc);
    if (R_FAILED(rc)) goto done;
    pm = 1;
    stage = "launch";
    NcmProgramLocation loc = { .program_id = PROBE_PROGRAM_ID, .storageID = NcmStorageId_None };
    rc = pmshellLaunchProgram(0, &loc, &launched);
    Result self_rc = svcGetProcessId(&self, CUR_PROCESS_HANDLE);
    note("pmshellLaunchProgram rc=0x%x parent_pid=%llu pid_rc=0x%x returned_pid=%llu", rc, self, self_rc, launched);
    if (R_FAILED(rc) || R_FAILED(self_rc) || !launched || launched > INT_MAX || launched == self) goto done;
    stage = "worker-hello";
    struct pollfd pollfd = { listener, POLLIN, 0 };
    if (poll(&pollfd, 1, WAIT_MS) <= 0 || (peer = accept(listener, NULL, NULL)) < 0) goto done;
    struct hello hello;
    if (!transfer(peer, &hello, sizeof(hello), 0) || hello.nonce != ticket.nonce || hello.pid != launched) goto done;
    note("independent-pid verified=%llu wine_pid=%u wine_tid=%u", launched, pid, tid);
    stage = "renderer-bootstrap";
    struct ready ready;
    if (!transfer(peer, start, sizeof(*start), 1) || !transfer(peer, &ready, sizeof(ready), 0) ||
        ready.pid != pid || ready.tid != tid || !ready.peb) goto done;
    note("renderer bootstrap confirmed native_pid=%llu windows_pid=%u windows_tid=%u", launched, ready.pid, ready.tid);
    stage = "relay";
    if (!start_relay(peer, pipe, tid, 0)) goto done;
    pipe = peer = -1;
    *host_pid = (int)launched;
    horizon_probe_started(info, ready.peb);
    ok = 1;
done:
    note("spawn stage=%s ok=%d errno=%d", stage, ok, errno);
    if (ticket_written) unlink(TICKET);
    if (!ok && pm && launched && launched != self) note("cleanup rc=0x%x", pmshellTerminateProcess(launched));
    if (pm) pmshellExit();
    if (listener >= 0) close(listener);
    if (peer >= 0) close(peer);
    if (pipe >= 0) close(pipe);
    if (!ok) __atomic_store_n(&active, 0, __ATOMIC_RELEASE);
    free(start);
    return ok;
}

int probe_worker_connect(void)
{
    struct ticket t;
    struct hello hello = {0};
    FILE *f = fopen(TICKET, "rb");
    if (!f) return 0;
    int valid = fread(&t, 1, sizeof(t), f) == sizeof(t);
    fclose(f);
    if (!valid || !t.port) return 0;
    hello.nonce = t.nonce;
    Result rc = svcGetProcessId(&hello.pid, CUR_PROCESS_HANDLE);
    note("worker self_pid=%llu rc=0x%x", (unsigned long long)hello.pid, rc);
    if (R_FAILED(rc)) return 0;
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = t.port, .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    if ((child_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0 ||
        connect(child_socket, (struct sockaddr *)&addr, sizeof(addr)) ||
        !transfer(child_socket, &hello, sizeof(hello), 1) ||
        !transfer(child_socket, &probe_child, sizeof(probe_child), 0)) return 0;
    if (!probe_child.pid || !probe_child.tid || probe_child.units >= PROBE_CMD_UNITS ||
        !switch_arg(probe_child.command, probe_child.units, "--type=renderer", NULL)) return 0;
    if (probe_child.trials.handle && !horizon_probe_import_trials(&probe_child.trials)) {
        note("field-trial import failed handle=0x%x size=%u", probe_child.trials.handle, probe_child.trials.size);
        return 0;
    }
    horizon_probe_identity(probe_child.pid, probe_child.tid);
    child_pipe = horizon_probe_import(probe_child.channel);
    note("worker inherited mojo=0x%x fd=%d wine_pid=%u wine_tid=%u", probe_child.channel, child_pipe, probe_child.pid, probe_child.tid);
    return child_pipe >= 0;
}

int probe_worker_enter(unsigned pid, unsigned tid, uintptr_t peb)
{
    struct ready ready = { pid, tid, peb };
    char byte;
    if (!transfer(child_socket, &ready, sizeof(ready), 1) || !transfer(child_socket, &byte, 1, 0) || byte != 'G') return 0;
    note("enter Electron renderer command_units=%u", probe_child.units);
    return start_relay(child_socket, child_pipe, probe_child.tid, 1);
}
