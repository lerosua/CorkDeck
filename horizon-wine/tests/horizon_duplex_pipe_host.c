/* Fixture for check_horizon_duplex_pipe.py. Supply newlib descriptors and a
 * small handle table; use the runtime's own creation/open/read/write paths. */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "horizon_async.h"
#include "horizon_file_access.h"

/* WIRE */

enum { FILE_SHARE_READ=1, FILE_SHARE_WRITE=2, FILE_READ_DATA=1, FILE_WRITE_DATA=2,
       FILE_APPEND_DATA=4, FILE_OPEN=1, FILE_CREATE=2, FILE_OPEN_IF=3,
       FILE_SYNCHRONOUS_IO_ALERT=16, FILE_SYNCHRONOUS_IO_NONALERT=32,
       HORIZON_SERVER_OBJECT_NAMED_PIPE=1, HORIZON_SERVER_OBJECT_NSI, HORIZON_SERVER_OBJECT_SOCK };
struct _reent { int _errno; };
struct horizon_server_connection { unsigned pid, tid; int reply_fd; };
struct horizon_server_object {
    unsigned type, file_options, file_access, file_sharing, pipe_flags, rootdir, name_len;
    unsigned char *name;
    int file_fd, file_peer_fd, signaled;
};
struct horizon_server_handle_entry {
    struct horizon_server_handle_entry *next;
    unsigned handle;
    struct horizon_server_object *object;
};
typedef struct { unsigned device; void *fileStruct; } __handle;
static struct horizon_pipe_file *descriptors[6];
static __handle handles[6];
static int horizon_pipe_device = 1;
static struct { int pipes; } horizon_lifecycle;
static struct horizon_server_handle_entry *horizon_server_handles;
static struct horizon_async pending;
static pthread_mutex_t horizon_server_objects_mutex = PTHREAD_MUTEX_INITIALIZER;
static unsigned reply_status, reply_handle;
#define min(a,b) ((a)<(b)?(a):(b))
#define horizon_trace(...) ((void)0)
static void wine_nx_runtime_trace(const char *msg) { puts(msg); }
static int horizon_pipe_install_device(void) { return horizon_pipe_device; }
static int __alloc_handle(unsigned dev) {
    for (int i=0; i<6; i++) if (!handles[i].device) {
        handles[i] = (__handle){ dev, descriptors+i };
        return i;
    }
    return -1;
}
static __handle *__get_handle(int fd) { return &handles[fd]; }
static int horizon_pipe_close_r(struct _reent *, void *);
static int close_fd(int fd) {
    assert(handles[fd].device);
    handles[fd].device=0;
    return horizon_pipe_close_r(NULL, descriptors+fd);
}
#define close close_fd
#define horizon_cond_wait_quit pthread_cond_wait
static int horizon_sock_poller_start(void) { return 1; }
static struct horizon_async *horizon_server_async_create_locked(struct horizon_server_connection *c,
        const struct horizon_async_data *data, int direction, int kind) {
    pending=(struct horizon_async){ .sock=data->handle, .direction=direction, .kind=kind };
    return &pending;
}
static struct horizon_server_handle_entry *horizon_server_create_handle_locked(unsigned type) {
    static unsigned serial;
    struct horizon_server_handle_entry *e=calloc(1,sizeof(*e));
    e->object=calloc(1,sizeof(*e->object));
    e->object->file_fd=e->object->file_peer_fd=-1;
    e->object->type=type;
    e->handle=++serial;
    e->next=horizon_server_handles;
    return horizon_server_handles=e;
}
static struct horizon_server_handle_entry *horizon_server_find_handle_locked(unsigned handle) {
    for (struct horizon_server_handle_entry *e=horizon_server_handles; e; e=e->next)
        if (e->handle==handle) return e;
    return NULL;
}
#define horizon_server_name_root_locked(id) (id)
#define horizon_nsi_device_name(data,size) 0
#define horizon_server_errno_status(err) HORIZON_STATUS_NO_MEMORY
static void horizon_server_unlink_handle_locked(struct horizon_server_handle_entry *e) {
    assert(e==horizon_server_handles);
    horizon_server_handles=e->next;
}
static void horizon_server_free_object(struct horizon_server_object *o) {
    if (o->file_fd>=0) close_fd(o->file_fd);
    if (o->file_peer_fd>=0) close_fd(o->file_peer_fd);
    free(o->name);
    free(o);
}
static int horizon_server_write_reply(int fd, const void *buf, size_t n, const void *data, size_t len) {
    const struct horizon_server_reply_header *h=buf;
    reply_status=h->error;
    memcpy(&reply_handle,h+1,sizeof(reply_handle));
    return 0;
}

static int horizon_habl_tracing(void) { return 1; }

/* IMPLEMENTATION */

void wine_nx_wait_begin(const void *p, int a, int b) {}
void wine_nx_wait_end(void) {}

static void test_process_channel(void)
{
    int fd[2];
    char byte;
    for (int attempt = 0; attempt < 16; attempt++)
    {
        assert(!horizon_process_socketpair(fd));
        assert(!descriptors[fd[0]]->async && !descriptors[fd[1]]->async);
        for (int side = 0; side < 2; side++)
        {
            char expected = 'a' + side;
            assert(horizon_pipe_write_r(NULL, descriptors + fd[side], &expected, 1) == 1);
            assert(horizon_pipe_read_r(NULL, descriptors + fd[1-side], &byte, 1) == 1 && byte == expected);
        }
        close_fd(fd[1]);
        assert(horizon_pipe_read_r(NULL, descriptors + fd[0], &byte, 1) == 0);
        close_fd(fd[0]);
        assert(!horizon_lifecycle.pipes);
    }

    /* Only one free descriptor: allocation of the second end must unwind the first. */
    for (int i = 0; i < 5; i++) assert(__alloc_handle(horizon_pipe_device) == i);
    assert(horizon_process_socketpair(fd) == -1 && errno == EMFILE);
    assert(fd[0] == -1 && fd[1] == -1 && !handles[5].device && !horizon_lifecycle.pipes);
    memset(handles, 0, sizeof(handles));
    puts("Child bootstrap channel: blocking duplex I/O, EOF, retries and descriptor exhaustion passed.");
}

int main(void) {
    test_process_channel();
    struct horizon_server_connection c={1,0x18,0};
    struct { struct horizon_object_attributes attr; unsigned short name[80]; } obj={0};
    const char *name="\\??\\pipe\\mojo.1.24.test";
    size_t chars=strlen(name);
    for (size_t i=0; i<chars; i++) obj.name[i]=name[i];
    obj.attr.name_len=chars*2;
    struct horizon_create_named_pipe_request create={.access=0xc0180000,.sharing=3,.disposition=3,
        .maxinstances=1,.insize=4096,.outsize=4096,.timeout=-50000000};
    horizon_server_handle_create_named_pipe(&c,(void*)&create,(void*)&obj,sizeof(obj));
    assert(!reply_status && reply_handle);
    struct horizon_server_object *server=horizon_server_handles->object;
    struct horizon_async_data op={.handle=reply_handle};
    assert(horizon_server_queue_pipe_async(&c,&op,2,HORIZON_ASYNC_PIPE_LISTEN)==HORIZON_STATUS_PENDING);
    assert(horizon_pipe_async_status(server,&pending)==HORIZON_STATUS_PENDING);
    struct horizon_open_file_object_request open={.access=0xc0000000};
    horizon_server_handle_open_file_object(&c,(void*)&open,(void*)obj.name,chars*2);
    assert(!reply_status && reply_handle!=op.handle);
    struct horizon_server_object *client=horizon_server_handles->object;
    assert(horizon_pipe_async_status(server,&pending)==HORIZON_STATUS_SUCCESS);
    assert(horizon_server_queue_pipe_async(&c,&op,2,HORIZON_ASYNC_PIPE_LISTEN)==HORIZON_STATUS_PIPE_CONNECTED);
    horizon_server_handle_open_file_object(&c,(void*)&open,(void*)obj.name,chars*2);
    assert(reply_status==HORIZON_STATUS_PIPE_NOT_AVAILABLE);
    struct horizon_pipe_file **s=descriptors+server->file_fd, **p=descriptors+client->file_fd;
    for (int direction=0; direction<=1; direction++) {
        assert(horizon_mojo_first_io(server->file_fd, direction)==op.handle);
        assert(horizon_mojo_first_io(client->file_fd, direction)==op.handle);
        assert(!horizon_mojo_first_io(server->file_fd, direction));
        assert(!horizon_mojo_first_io(client->file_fd, direction));
    }
    unsigned char src[HORIZON_PIPE_BUFFER_SIZE+9], dst[sizeof(src)];
    for (size_t i=0; i<sizeof(src); i++) src[i]=(unsigned char)(i*13);
    assert(horizon_server_queue_pipe_async(&c,&op,HORIZON_ASYNC_READ,HORIZON_ASYNC_PIPE_IO)==HORIZON_STATUS_PENDING);
    assert(horizon_pipe_async_status(server,&pending)==HORIZON_STATUS_PENDING);
    assert(horizon_pipe_read_r(NULL,s,(char*)dst,12)==-1 && errno==EAGAIN);
    assert(horizon_pipe_write_r(NULL,s,"pong",4)==4);
    assert(horizon_pipe_async_status(server,&pending)==HORIZON_STATUS_PENDING);
    assert(horizon_pipe_write_r(NULL,p,(char*)src,sizeof(src))==HORIZON_PIPE_BUFFER_SIZE);
    assert(horizon_pipe_async_status(server,&pending)==HORIZON_STATUS_ALERTED);
    assert(horizon_pipe_write_r(NULL,p,"x",1)==-1 && errno==EAGAIN);
    assert(horizon_pipe_read_r(NULL,s,(char*)dst,17)==17);
    assert(horizon_pipe_write_r(NULL,p,(char*)src+HORIZON_PIPE_BUFFER_SIZE,9)==9);
    assert(horizon_pipe_read_r(NULL,s,(char*)dst+17,sizeof(src)-17)==sizeof(src)-17);
    assert(!memcmp(src,dst,sizeof(src)));
    assert(horizon_pipe_read_r(NULL,p,(char*)dst,sizeof(dst))==4 && !memcmp(dst,"pong",4));
    while (horizon_server_handles) {
        struct horizon_server_handle_entry *e=horizon_server_handles;
        horizon_server_unlink_handle_locked(e);
        horizon_server_free_object(e->object);
        free(e);
        if (horizon_server_handles) assert(horizon_pipe_async_status(server,&pending)==HORIZON_STATUS_PIPE_BROKEN);
    }
    assert(!horizon_lifecycle.pipes);
    puts("Duplex Mojo bootstrap, pending listen/read, both directions, wraparound, backpressure and peer close passed.");
}
