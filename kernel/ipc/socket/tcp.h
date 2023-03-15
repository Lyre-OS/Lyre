#ifndef _IPC__SOCKET__TCP_H
#define _IPC__SOCKET__TCP_H

#include <dev/net/net.h>

struct socket *socket_create_tcp(int type, int protocol);
void tcp_ontcp(struct net_adapter *adapter, struct net_inetheader *inetheader, size_t length);

#endif
