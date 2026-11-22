#ifndef CORKDECK_PROBE_RENDERER_H
#define CORKDECK_PROBE_RENDERER_H
#include <stdint.h>
#include <stddef.h>
#define PROBE_PROGRAM_ID UINT64_C(0x0532434400524e44)
#define PROBE_IMAGE "sdmc:/switch/wine/drive_c/ElectronProbe-10.1.5/ElectronProbe.exe"
#define PROBE_CMD_UNITS 8192
/* Chromium 85 passes a read-only field-trial configuration at startup. */
struct probe_trials {
    uint32_t handle, size;
    char data[256 * 1024];
};
struct probe_start {
    uint32_t pid, tid, channel, units;
    uint16_t command[PROBE_CMD_UNITS];
    struct probe_trials trials;
};
extern struct probe_start probe_child;
int probe_worker_connect(void);
int probe_worker_enter(unsigned pid, unsigned tid, uintptr_t peb);
void horizon_probe_identity(unsigned pid, unsigned tid);
int probe_spawn_renderer(const uint16_t *cmd, unsigned units, unsigned pid, unsigned tid,
                         unsigned info, const uintptr_t *handles, size_t handle_count, int *host_pid);
int horizon_probe_export_trials(struct probe_trials *trials);
int horizon_probe_import_trials(const struct probe_trials *trials);
int horizon_probe_export(unsigned handle);
int horizon_probe_import(unsigned handle);
void horizon_probe_started(unsigned info, uintptr_t peb);
int horizon_probe_suspended(unsigned tid);
void horizon_probe_ended(unsigned tid);
#endif
