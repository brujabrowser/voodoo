#ifndef WHP_C_NET_H_
#define WHP_C_NET_H_

#include "whp/c/types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Independent of Mojo handles. Sockets are a public layer of their own.
typedef uint32_t WhpNetSocket;
#define WHP_NET_SOCKET_INVALID ((WhpNetSocket)0)

WhpResult WhpNetInit(void);
WhpResult WhpNetUdpOpen(WhpNetSocket* out);
WhpResult WhpNetClose(WhpNetSocket socket);
WhpResult WhpNetBind(WhpNetSocket socket, const char* host, uint16_t port);
WhpResult WhpNetSetNonBlocking(WhpNetSocket socket, int enabled);
WhpResult WhpNetSetReuseAddr(WhpNetSocket socket, int enabled);
int WhpNetSendTo(WhpNetSocket socket,
                 const void* data,
                 uint32_t n,
                 const char* host,
                 uint16_t port);
int WhpNetRecvFrom(WhpNetSocket socket,
                   void* data,
                   uint32_t n,
                   char* host,
                   uint32_t host_len,
                   uint16_t* port);
WhpResult WhpNetLocalEndpoint(WhpNetSocket socket,
                              char* host,
                              uint32_t host_len,
                              uint16_t* port);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // WHP_C_NET_H_
