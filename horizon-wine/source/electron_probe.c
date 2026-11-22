/* Diagnostics for the Electron 10 x64 window probe on Horizon. */
#include <switch.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winternl.h"
#include "thread_profile.h"
#include "electron_probe.h"

extern BOOL wine_nx_image_at( const void *address, void **base, char *name, size_t size );
extern void wine_nx_runtime_trace( const char *line );
extern const char *wine_nx_syscall_name( unsigned int id );

int wine_nx_probe_window_phase; /* 1 inside JS constructor, 2 after its native window returned */
static unsigned int snapshot_id;
static __thread int reporting, exit_recorded;
static __thread unsigned int seen_exception, seen_error, seen_fatal, debug_lines;
static __thread struct { unsigned int id; uintptr_t result; } recent[32];
static __thread unsigned int completed;
static __thread struct { unsigned int id, status; } failed[8];
static __thread unsigned int failed_count;

void wine_nx_probe_set_window_phase( int phase )
{
    __atomic_store_n( &wine_nx_probe_window_phase, phase, __ATOMIC_RELEASE );
}

void wine_nx_probe_sysret( unsigned int id, uintptr_t result )
{
    unsigned int slot = completed++ % ARRAY_SIZE(recent);
    recent[slot].id = id;
    recent[slot].result = result;
    /* NT table failures only. win32u results also include pointers and BOOLs. */
    if (id < 0x1000 && (int32_t)result < 0)
    {
        slot = failed_count++ % ARRAY_SIZE(failed);
        failed[slot].id = id;
        failed[slot].status = (unsigned int)result;
    }
}

static uint64_t read_slot( uint64_t address )
{
    uint64_t value;
    memcpy( &value, (const void *)(uintptr_t)address, sizeof(value) );
    return value;
}

static void probe_stack( unsigned int id, const char *kind, uintptr_t pc, uintptr_t sp, uintptr_t fp )
{
    MemoryInfo mem;
    u32 page;
    char line[256], name[80];
    uint64_t callers[32];
    uint64_t end;
    unsigned int i, n;
    void *base;

    snprintf( line, sizeof(line), "[EPROBE] stack id=%u kind=%s pc=%p sp=%p fp=%p",
              id, kind, (void *)pc, (void *)sp, (void *)fp );
    wine_nx_runtime_trace( line );
    if (!sp || R_FAILED(svcQueryMemory( &mem, &page, sp )) || !(mem.perm & Perm_R) ||
        sp < mem.addr || sp - mem.addr >= mem.size)
    {
        wine_nx_runtime_trace( "[EPROBE] stack mapping unreadable" );
        return;
    }
    end = mem.addr + mem.size;
    n = nx_prof_callers( pc, fp, sp, end, read_slot, callers, ARRAY_SIZE(callers) );
    for (i = 0; i < n; ++i)
    {
        uint64_t addr = callers[i];
        int mapped = wine_nx_image_at( (void *)(uintptr_t)addr, &base, name, sizeof(name) );
        if (mapped)
            snprintf( line, sizeof(line), "[EPROBE] frame id=%u kind=%s n=%u pc=%p image=%s rva=0x%lx",
                      id, kind, i, (void *)(uintptr_t)addr, name[0] ? name : "(unnamed PE)",
                      (unsigned long)(addr - (uintptr_t)base) );
        else
            snprintf( line, sizeof(line), "[EPROBE] frame id=%u kind=%s n=%u pc=%p",
                      id, kind, i, (void *)(uintptr_t)addr );
        wine_nx_runtime_trace( line );
    }

    /* Optimized x64 callers can omit RBP entirely. Preserve possible return
     * sites in the main executable's code range separately from walked frames. */
    TEB *teb = NtCurrentTeb();
    if (!teb || !teb->Peb || !(base = teb->Peb->ImageBaseAddress)) return;
    const IMAGE_DOS_HEADER *dos = base;
    const IMAGE_NT_HEADERS64 *nt = (const void *)((const char *)base + dos->e_lfanew);
    if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) return;
    const IMAGE_SECTION_HEADER *sections = IMAGE_FIRST_SECTION(nt);
    unsigned int count = 0;
    for (uintptr_t slot = sp; !(slot & 7) && end - slot >= 8 && slot - sp < 16384 && count < 48; slot += 8)
    {
        uint64_t addr = read_slot( slot );
        uintptr_t rva = addr - (uintptr_t)base;
        if (rva >= nt->OptionalHeader.SizeOfImage) continue;
        for (i = 0; i < nt->FileHeader.NumberOfSections; i++)
        {
            if (!(sections[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) ||
                rva - sections[i].VirtualAddress >= sections[i].Misc.VirtualSize) continue;
            snprintf( line, sizeof(line), "[EPROBE] stack_candidate id=%u kind=%s slot=sp+0x%lx ElectronProbe.exe+0x%lx",
                      id, kind, (unsigned long)(slot - sp), (unsigned long)rva );
            wine_nx_runtime_trace( line );
            count++;
            break;
        }
    }
}

unsigned int wine_nx_probe_snapshot( const char *reason, unsigned int code, uintptr_t stack )
{
    unsigned int phase = __atomic_load_n( &wine_nx_probe_window_phase, __ATOMIC_ACQUIRE );
    char line[224];
    TEB *teb = NtCurrentTeb();
    if (!phase || reporting || !teb) return 0;
    if (!strcmp( reason, "self-terminate" ) && exit_recorded++) return 0;
    int saved_errno = errno;
    unsigned int id = __atomic_add_fetch( &snapshot_id, 1, __ATOMIC_RELAXED );
    reporting = 1;
    snprintf( line, sizeof(line), "[EPROBE] snapshot id=%u phase=%u tid=%u reason=%s code=0x%08x",
              id, phase, (unsigned int)(uintptr_t)teb->ClientId.UniqueThread, reason, code );
    wine_nx_runtime_trace( line );
    for (unsigned int back = min( completed, (unsigned int)ARRAY_SIZE(recent) ); back; --back)
    {
        unsigned int slot = (completed - back) % ARRAY_SIZE(recent);
        snprintf( line, sizeof(line), "[EPROBE] recent id=%u back=%u api=%s syscall=0x%x ret=0x%lx",
                  id, back, wine_nx_syscall_name( recent[slot].id ), recent[slot].id, (unsigned long)recent[slot].result );
        wine_nx_runtime_trace( line );
    }
    for (unsigned int back = min( failed_count, (unsigned int)ARRAY_SIZE(failed) ); back; --back)
    {
        unsigned int slot = (failed_count - back) % ARRAY_SIZE(failed);
        snprintf( line, sizeof(line), "[EPROBE] recent_nt_failure id=%u back=%u api=%s ntstatus=0x%08x",
                  id, back, wine_nx_syscall_name( failed[slot].id ), failed[slot].status );
        wine_nx_runtime_trace( line );
    }
    uintptr_t fp = (uintptr_t)__builtin_frame_address( 0 );
    probe_stack( id, "native-fp", (uintptr_t)__builtin_return_address(0), fp, fp );
    if (stack && stack != fp) probe_stack( id, "PE-syscall-entry", 0, stack, 0 );
    reporting = 0;
    errno = saved_errno;
    return id;
}

void wine_nx_probe_message( const char *source, const char *msg )
{
    if (!__atomic_load_n( &wine_nx_probe_window_phase, __ATOMIC_ACQUIRE ) || reporting) return;
    TEB *teb = NtCurrentTeb();
    int fatal = strstr( msg, "FATAL" ) || strstr( msg, "Check failed" ) || strstr( msg, "CHECK failed" );
    int error = strstr( msg, ":ERROR" ) != NULL;
    /* Chromium's stack dump spans multiple stderr writes. Keep those lines
     * even without a severity prefix; always retain an error past the limit. */
    if (debug_lines++ < 200 || fatal || error)
    {
        char line[1200];
        snprintf( line, sizeof(line), "[EPROBE] diagnostic tid=%u source=%s %s",
                  teb ? (unsigned int)(uintptr_t)teb->ClientId.UniqueThread : 0, source, msg );
        wine_nx_runtime_trace( line );
    }
    if (!teb) return; /* A partial stderr line can also be flushed by a host thread. */
    if (fatal && !seen_fatal++) wine_nx_probe_snapshot( "chromium-fatal", 0, 0 );
    else if (error && !seen_error++) wine_nx_probe_snapshot( "chromium-error", 0, 0 );
    else if (strstr( msg, ":seh:RaiseFailFastException " ))
        wine_nx_probe_snapshot( "RaiseFailFastException", STATUS_FAIL_FAST_EXCEPTION, 0 );
}

void wine_nx_probe_seh( const char *msg )
{
    static __thread int want_context;
    static __thread unsigned int exception_id;
    const char *dispatch = strstr( msg, ":seh:dispatch_exception " );
    if (!__atomic_load_n( &wine_nx_probe_window_phase, __ATOMIC_ACQUIRE ) || !dispatch || reporting) return;
    dispatch += sizeof(":seh:dispatch_exception ") - 1;
    unsigned int code;
    unsigned long long pc, sp, fp, lr;
    if (sscanf( dispatch, "code=%x", &code ) == 1)
    {
        want_context = code != DBG_PRINTEXCEPTION_C && code != DBG_PRINTEXCEPTION_WIDE_C && code != 0x406d1388;
        if (want_context)
        {
            wine_nx_probe_message( "guest-first-chance", msg );
            /* Earlier RPC setup exceptions cannot consume this phase's budget. */
            want_context = seen_exception++ < 16;
            if (want_context)
            {
                exception_id = wine_nx_probe_snapshot( "guest-first-chance", code, 0 );
            }
        }
    }
    else if (want_context)
    {
        const char *kind;
        if (sscanf( dispatch, "rip=%llx rsp=%llx rbp=%llx", &pc, &sp, &fp ) == 3) kind = "guest-x64-context";
        else if (sscanf( dispatch, " pc=%llx sp=%llx lr=%llx fp=%llx", &pc, &sp, &lr, &fp ) == 4) kind = "guest-arm64-context";
        else
        {
            if (strstr( dispatch, "info[" )) wine_nx_probe_message( "guest-exception-parameter", msg );
            return;
        }
        want_context = 0;
        wine_nx_probe_message( "guest-context", msg );
        reporting = 1;
        probe_stack( exception_id, kind, pc, sp, fp );
        reporting = 0;
    }
}
