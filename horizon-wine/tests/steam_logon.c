/* Offline provider logon fixtures; no real credentials or connection. */
#define _POSIX_C_SOURCE 200809L
#include "../source/steam/steam_provider.c"
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include "../source/steam/steam_device_key.h"
/* Deterministic device for the host fixture only. */
int steam_device_key(const unsigned char salt[16], unsigned char key[32])
{
    memcpy(key, salt, 16);
    memcpy(key + 16, salt, 16);
    return 1;
}
int steam_backend_app(struct steam_backend *b,uint32_t app,struct steam_app *out){(void)b;(void)app;(void)out;return 0;}
int steam_storage_pending(const char *r,uint32_t app,char out[512]){(void)r;(void)app;(void)out;return 0;}
int steam_storage_directory(const char *r,uint32_t app,char out[512]){(void)r;(void)app;(void)out;return 0;}
unsigned int steam_storage_uninstall(const char *r,uint32_t app,int (*t)(void *),void *c){(void)r;(void)app;(void)t;(void)c;return 3104;}
static unsigned char response[64];static size_t response_size;static uint64_t identity;static uint32_t sid;
static int cache_fixture, online, owned_calls, ticks;
static size_t expected_before_connect;
static SDL_atomic_t link_down;
static int reconnect_fixture, mock_timeouts;
int steam_link_connected(void) {return !SDL_AtomicGet(&link_down);}

static void await_state(struct steam_provider *p, enum steam_status wanted, Uint32 timeout)
{
    Uint32 start = SDL_GetTicks();
    for (;;) {
        struct steam_status_view v;
        steam_provider_status(p, &v);
        if (v.state == wanted) return;
        assert(SDL_GetTicks() - start < timeout);
        SDL_Delay(10);
    }
}
const char *steam_cm_endpoint(const struct steam_cm *c){(void)c;return "test.steamserver.net:443";}
void steam_cm_remember(const char *root,const char *endpoint){(void)root;(void)endpoint;}
struct steam_cm *steam_cm_connect(const char *root,steam_cancel_fn f,void *p,unsigned int *e)
{
    (void)root; (void)f;
    if (cache_fixture) {
        struct steam_library_view view;
        steam_provider_library_status(p, &view);
        assert(view.count == expected_before_connect);
        if (mock_timeouts > 0) {
            mock_timeouts--;
            *e = 100 + STEAM_NETWORK;
            return NULL;
        }
        if (online) return (struct steam_cm *)p;
        *e = 2;
    }
    return NULL;
}
unsigned int steam_cm_connect_transport(void){return 0;}
void steam_cm_close(struct steam_cm *c){(void)c;}
unsigned int steam_cm_error(const struct steam_cm *c){(void)c;return cache_fixture ? 3 : 6;}
unsigned int steam_cm_last_type(const struct steam_cm *c){(void)c;return 782;}
int steam_cm_tick(struct steam_cm *c)
{
    if (reconnect_fixture) return !connection_interrupted((struct steam_provider *)c);
    return cache_fixture && ticks++ == 0;
}
int steam_cm_call(struct steam_cm *c,enum steam_service t,uint64_t id,uint32_t s,uint64_t j,const void *b,size_t n,struct steam_cm_message *m)
{
    static const unsigned char owned[] = {8, 1, 18, 7, 8, 98, 18, 3, 'K', 'O', 'F'};
    (void)c;(void)id;(void)s;(void)j;(void)b;(void)n;
    if (!cache_fixture) return 0;
    *m = (struct steam_cm_message){.type = 147, .result = 1};
    if (t == STEAM_SERVICE_OWNED) {
        owned_calls++;
        m->body = owned;
        m->body_size = sizeof(owned);
    } else assert(t == STEAM_SERVICE_FAMILY_GROUP);
    return 1;
}
int steam_install_run(struct steam_backend *b,const char *r,uint32_t a,const struct steam_install_callbacks *c)
{(void)b;(void)r;(void)a;(void)c;return 0;}
static unsigned int discards;
void steam_install_discard(const char *r,struct steam_download_view *v,const struct steam_install_callbacks *c)
{(void)r;++discards;v->state=STEAM_DOWNLOAD_DISCARDED;v->error=v->cleanup_errno=0;if(c->progress)c->progress(c->context,v);}
unsigned int steam_download_diagnostic(const char *r,const struct steam_download_view *v)
{(void)r;(void)v;return 0;}
static unsigned int failures_logged;
void steam_download_failure_log(const char *r,const struct steam_download_view *v,const char *o)
{(void)r;(void)v;(void)o;++failures_logged;}
int steam_install_present(const char *r,uint32_t a){(void)r;(void)a;return 0;}
int steam_cm_send(struct steam_cm *c,uint32_t t,uint64_t id,uint32_t s,const void *b,size_t n)
{(void)c;(void)id;(void)s;(void)b;(void)n;assert(t==5514 || (reconnect_fixture && t==703));return 1;}
int steam_cm_exchange(struct steam_cm *c,const void *b,size_t n,uint64_t j,uint32_t t,struct steam_cm_message *m)
{(void)c;(void)b;(void)n;(void)j;assert(t==751);*m=(struct steam_cm_message){.steam_id=identity,.session_id=sid,.body=response,.body_size=response_size};return 1;}
static void test_cached_relaunch(struct steam_auth *a)
{
    char root[] = "/tmp/corkdeck-library-XXXXXX", path[512];
    const char *pass = "synthetic cache test passphrase";
    struct steam_library list = {0};
    struct steam_library_stamp stamp = {0};
    struct steam_game game;
    size_t total;
    assert(mkdtemp(root) && !steam_session_save(root, pass, a));
    struct steam_wire_writer w = {response, sizeof(response), 0, 0};
    steam_wire_uint(&w, 1, 1);
    response_size = w.size;
    cache_fixture = 1;

    /* First launch fetches, second uses the fresh file, third loses Wi-Fi,
     * fourth refreshes a stale but still visible snapshot. */
    for (int launch = 0; launch < 4; launch++) {
        struct steam_provider *p = steam_provider_create();
        expected_before_connect = launch != 0;
        ticks = owned_calls = 0;
        online = 1;
        reconnect_fixture = launch == 2;
        if (reconnect_fixture) {
            SDL_AtomicSet(&link_down, 1);
            mock_timeouts = 3;
        }
        if (launch == 1) assert(!steam_session_save_automatic(root, a));
        assert(p && steam_provider_set_root(p, root));
        if (launch == 0) assert(steam_provider_unlock_session(p, pass));
        assert(p->thread);
        if (reconnect_fixture) {
            await_state(p, STEAM_WAIT_NETWORK, 3000);
            assert(steam_provider_library_page(p, "", 0, &game, 1, &total) == 1);
            /* Wi-Fi returns, but discovery still times out three times. */
            SDL_AtomicSet(&link_down, 0);
            await_state(p, STEAM_ONLINE, 40000);
            assert(!mock_timeouts);
            /* Drop an established session, recover it again in this worker,
             * then exercise shutdown while the network remains down. */
            SDL_AtomicSet(&link_down, 1);
            await_state(p, STEAM_WAIT_NETWORK, 3000);
            SDL_AtomicSet(&link_down, 0);
            await_state(p, STEAM_ONLINE, 3000);
            SDL_AtomicSet(&link_down, 1);
            await_state(p, STEAM_WAIT_NETWORK, 3000);
            SDL_AtomicSet(&p->cancel, 1);
        }
        SDL_WaitThread(p->thread, NULL);
        p->thread = NULL;
        reconnect_fixture = 0;
        SDL_AtomicSet(&link_down, 0);
        assert(owned_calls == (launch == 0 || launch == 3));
        assert(steam_provider_library_page(p, "", 0, &game, 1, &total) == 1);
        assert(game.app_id == 98 && !strcmp(game.name, "KOF") && total == 1);
        steam_provider_destroy(p);
        assert(steam_library_cache_load(root, a->steam_id, &list, &stamp));
        assert(!steam_library_cache_load(root, a->steam_id + 1, &list, &stamp));
        assert(steam_library_fresh(&stamp, "english", time(NULL)));
        assert(!steam_library_fresh(&stamp, "russian", time(NULL)));
        assert(!steam_library_fresh(&stamp, "english", (time_t)stamp.synced - 1));
        if (launch == 2) {
            stamp.synced -= 86400;
            assert(steam_library_cache_save(root, &list, &stamp));
        }
    }
    snprintf(path, sizeof(path), "%s/steam-library.cache", root);
    assert(!truncate(path, 65));
    assert(!steam_library_cache_load(root, a->steam_id, &list, &stamp));
    assert(list.count == 1 && list.games[0].app_id == 98);
    steam_library_clear(&list);
    list.account = a->steam_id;
    assert(steam_library_cache_save(root, &list, &stamp));
    assert(steam_library_cache_load(root, a->steam_id, &list, &stamp) && list.count == 0);
    assert(!steam_session_save(root, pass, a));
    struct steam_provider *p = steam_provider_create();
    assert(p && steam_provider_set_root(p, root));
    steam_provider_logout(p);
    steam_provider_destroy(p);
    assert(!steam_library_cache_load(root, a->steam_id, &list, &stamp));
    assert(!rmdir(root));
    cache_fixture = 0;
    puts("Library cache: pre-network restore, fresh/stale requests, offline, account isolation, corruption, empty library and logout passed");
}

int main(void)
{
    assert(retry_transport(0,2)&&retry_transport(0,6)&&retry_transport(206,0));
    assert(retry_transport(100 + STEAM_NETWORK, CURLE_OPERATION_TIMEDOUT));
    assert(!retry_transport(100 + STEAM_PROTOCOL, 0));
    assert(!retry_transport(100 + STEAM_TLS, CURLE_PEER_FAILED_VERIFICATION));
    assert(!retry_transport(200 + CURLE_PEER_FAILED_VERIFICATION, 0));
    assert(!retry_transport(1005,2)&&!retry_transport(1065,6)&&!retry_transport(11,2));
    assert(!retry_transport(0,4)&&!retry_transport(0,3)&&!retry_transport(3403,0));
    struct steam_auth a={0};unsigned int error=999,hb=0;uint32_t session=0,cell=999;
    struct steam_wire_writer w={response,sizeof(response),0,0};
    a.steam_id=identity=UINT64_C(76561198000000000);sid=42;
    strcpy(a.refresh_token,"synthetic-token");strcpy(a.account_name,"synthetic-account");
    steam_wire_uint(&w,1,1);steam_wire_uint(&w,2,9);steam_wire_uint(&w,3,30);response_size=w.size;
    assert(logon(NULL,&a,&session,&cell,&hb,&error));assert(!error&&session==42&&hb==9&&cell==0);
    steam_wire_uint(&w,7,47);response_size=w.size;
    assert(logon(NULL,&a,&session,&cell,&hb,&error)&&cell==47);
    {size_t good=w.size;steam_wire_uint(&w,7,48);response_size=w.size;
     assert(!logon(NULL,&a,&session,&cell,&hb,&error)&&error==11);w.size=good;response_size=good;}
    identity++;assert(!logon(NULL,&a,&session,&cell,&hb,&error)&&error==12);identity--;
    sid=0;assert(!logon(NULL,&a,&session,&cell,&hb,&error)&&error==13);sid=42;
    response_size=0;assert(!logon(NULL,&a,&session,&cell,&hb,&error)&&error==11);
    w=(struct steam_wire_writer){response,sizeof(response),0,0};steam_wire_uint(&w,1,5);response_size=w.size;
    assert(!logon(NULL,&a,&session,&cell,&hb,&error)&&error==1005);
    {struct steam_provider *p=steam_provider_create();assert(p);assert(steam_provider_set_root(p,"/tmp"));assert(!steam_provider_install(p,123));
     state(p,STEAM_ONLINE,0,0,a.steam_id);assert(!steam_provider_install(p,123));
     p->library.games=calloc(1,sizeof(*p->library.games));assert(p->library.games);p->library.count=1;p->library.games[0].app_id=123;
     assert(!steam_provider_inspect(p,456));assert(steam_provider_inspect(p,123));assert(p->inspect_app==123&&p->app_view.state==1);assert(!steam_provider_inspect(p,123));
     assert(!steam_provider_install(p,456));assert(steam_provider_install(p,123));assert(!steam_provider_install(p,123));
     assert(!strcmp(p->install_language,p->language));
     p->install_app=0;p->download.state=STEAM_DOWNLOAD_ERROR;
     assert(!steam_provider_install_language(p,123,"../invalid"));
     assert(steam_provider_install_language(p,123,"russian"));
     assert(!strcmp(p->install_language,"russian")&&!strcmp(p->language,"english"));
     p->install_app=0;p->download.state=STEAM_DOWNLOAD_CANCELLED;
     assert(steam_provider_install(p,123)&&!strcmp(p->install_language,"russian"));
     p->install_app=0;p->download.state=STEAM_DOWNLOAD_ERROR;
     assert(steam_provider_install_language(p,123,STEAM_LANGUAGE_BASE_ID));
     assert(!strcmp(p->install_language,STEAM_LANGUAGE_BASE_ID)&&!strcmp(p->language,"english"));
     /* No Cancel: disconnect must retain resumable data. */
     steam_provider_logout(p);assert(!p->library.count&&!p->install_app&&!discards);steam_provider_destroy(p);}
    {struct steam_provider *p=steam_provider_create();assert(p&&steam_provider_set_root(p,"/tmp"));
     state(p,STEAM_ONLINE,0,0,a.steam_id);
     p->download=(struct steam_download_view){.app_id=123,.state=STEAM_DOWNLOAD_PLANNING};p->install_app=123;
     steam_provider_cancel_download(p);assert(SDL_AtomicGet(&p->download_cancel));
     steam_provider_logout(p);assert(discards==1&&!p->install_app);steam_provider_destroy(p);}
    {struct steam_provider *p=steam_provider_create();assert(p&&steam_provider_set_root(p,"/tmp"));
     p->download=(struct steam_download_view){.app_id=123,.state=STEAM_DOWNLOAD_ERROR,.error=3106};
     steam_provider_cancel_download(p);assert(p->thread);SDL_WaitThread(p->thread,NULL);p->thread=NULL;
     assert(p->download.state==STEAM_DOWNLOAD_DISCARDED&&discards==2);
     p->download=(struct steam_download_view){.app_id=123,.state=STEAM_DOWNLOAD_ERROR,.error=3112};
     steam_provider_cancel_download(p);assert(p->thread);SDL_WaitThread(p->thread,NULL);p->thread=NULL;
     assert(p->download.state==STEAM_DOWNLOAD_DISCARDED&&discards==3);
     p->download.state=STEAM_DOWNLOAD_COMMITTING;SDL_AtomicSet(&p->download_cancel,0);steam_provider_cancel_download(p);
     assert(!SDL_AtomicGet(&p->download_cancel));p->download.state=STEAM_DOWNLOAD_COMPLETE;steam_provider_cancel_download(p);
     assert(!SDL_AtomicGet(&p->download_cancel));steam_provider_destroy(p);assert(discards==3);}
    /* Starting over: the files go first, then the same download begins in the language it had. */
    {struct steam_provider *p=steam_provider_create();assert(p&&steam_provider_set_root(p,"/tmp"));
     state(p,STEAM_ONLINE,0,0,a.steam_id);
     p->library.games=calloc(1,sizeof(*p->library.games));assert(p->library.games);p->library.count=1;p->library.games[0].app_id=123;
     assert(!steam_provider_restart_download(p)&&!steam_provider_restart_poll(p));
     p->download=(struct steam_download_view){.app_id=123,.state=STEAM_DOWNLOAD_ERROR,.error=3110,.attempts=2,.stuck=1};
     strcpy(p->download.language,"russian");p->fail_app=123;p->fail_count=2;
     assert(steam_provider_restart_download(p)&&failures_logged==1);
     assert(p->download.state==STEAM_DOWNLOAD_CLEANING&&!p->download.stuck&&!p->fail_count&&p->restart_app==123);
     assert(!steam_provider_restart_poll(p)&&!p->install_app);
     finish_discard(p);assert(p->download.state==STEAM_DOWNLOAD_DISCARDED&&discards==4);
     assert(steam_provider_restart_poll(p)==1&&p->install_app==123&&!p->restart_app);
     assert(!strcmp(p->install_language,"russian")&&p->download.state==STEAM_DOWNLOAD_PLANNING);
     assert(!steam_provider_restart_poll(p));
     /* Cleaning failed: nothing is started. */
     p->install_app=0;p->download=(struct steam_download_view){.app_id=123,.state=STEAM_DOWNLOAD_ERROR,.error=3106};
     assert(steam_provider_restart_download(p));
     p->download.state=STEAM_DOWNLOAD_ERROR;p->download.error=3112;p->download.failed_stage=STEAM_DOWNLOAD_CLEANING;
     assert(steam_provider_restart_poll(p)==-1&&!p->install_app&&!p->restart_app);
     SDL_AtomicSet(&p->download_cancel,0);p->download=(struct steam_download_view){0};steam_provider_destroy(p);}
    {char root[]="/tmp/steam-provider-vault-XXXXXX";struct steam_provider *p;struct steam_session_view v;
     strcpy(a.refresh_token,"e30.eyJzdWIiOiI3NjU2MTE5ODAwMDAwMDAwMCJ9.signature");assert(mkdtemp(root));
     assert(!steam_session_save(root,"synthetic local vault passphrase",&a));p=steam_provider_create();assert(p&&steam_provider_set_root(p,root));
     steam_provider_session_status(p,&v);assert(v.present);steam_provider_destroy(p);assert(steam_session_present(root));
     p=steam_provider_create();assert(p&&steam_provider_set_root(p,root));steam_provider_logout(p);assert(!steam_session_present(root));steam_provider_destroy(p);assert(!rmdir(root));}
    {char root[]="/tmp/steam-auto-invalid-XXXXXX",path[768];struct steam_provider *p;struct steam_status_view status;FILE *f;
     assert(mkdtemp(root));snprintf(path,sizeof(path),"%s/steam-session.vault",root);
     f=fopen(path,"wb");assert(f&&fwrite("ARSTEAM\2",1,8,f)==8&&!fclose(f));
     p=steam_provider_create();assert(p&&steam_provider_set_root(p,root)&&p->thread);
     SDL_WaitThread(p->thread,NULL);p->thread=NULL;steam_provider_status(p,&status);
     assert(status.state==STEAM_ERROR&&status.error==3402&&!status.steam_id);
     steam_provider_destroy(p);assert(steam_session_present(root));
     p=steam_provider_create();assert(p&&steam_provider_set_root(p,root));steam_provider_logout(p);
     assert(!steam_session_present(root));steam_provider_destroy(p);assert(!rmdir(root));}
    test_cached_relaunch(&a);
    steam_auth_clear(&a);puts("CM identity/session, heartbeat, retry gates, automatic startup failure and shutdown/logout vault lifetime tests passed");return 0;
}
