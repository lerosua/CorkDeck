#ifndef CORKDECK_SYSTEM_NETWORK_H
#define CORKDECK_SYSTEM_NETWORK_H
#ifdef __SWITCH__
#include <switch.h>
#endif

enum nx_link { NX_LINK_UNKNOWN, NX_LINK_DOWN, NX_LINK_CONNECTING, NX_LINK_WIFI, NX_LINK_ETHERNET, NX_LINK_OTHER };
struct nx_network { enum nx_link link; unsigned int bars; };

/* Local Horizon status only. NIFM reports no available network as a Result
 * error (including airplane mode), separately from service/query failures. */
static inline struct nx_network nx_system_network(void)
{
    struct nx_network net = {NX_LINK_UNKNOWN, 0};
#ifdef __SWITCH__
    NifmInternetConnectionType type;
    NifmInternetConnectionStatus status;
    u32 strength;
    if (R_FAILED(nifmInitialize(NifmServiceType_User))) return net;
    Result rc = nifmGetInternetConnectionStatus(&type, &strength, &status);
    nifmExit();
    if (rc == 0xd46ed) net.link = NX_LINK_DOWN;
    else if (R_SUCCEEDED(rc)) {
        if (status != NifmInternetConnectionStatus_Connected) net.link = NX_LINK_CONNECTING;
        else if (type == NifmInternetConnectionType_WiFi) {
            net.link = NX_LINK_WIFI;
            net.bars = strength > 3 ? 3 : strength;
        } else if (type == NifmInternetConnectionType_Ethernet) net.link = NX_LINK_ETHERNET;
        else net.link = NX_LINK_OTHER;
    }
#endif
    return net;
}
#endif
