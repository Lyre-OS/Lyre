#ifndef _IPC__SOCKET__UDP_H
#define _IPC__SOCKET__UDP_H

#include <dev/net/net.h>

void net_onudp(struct net_adapter *adapter, struct net_inetheader *inetheader, size_t length);

#endif