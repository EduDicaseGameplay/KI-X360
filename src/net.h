// Minimal portable UDP (BSD sockets / Winsock), with optional simulated
// latency, jitter and packet loss for testing netplay on one machine.
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t  ip[4];
    uint16_t port;
} net_addr_t;

bool net_open(int port);              // bind a UDP socket (0 = any free port)
void net_close(void);
bool net_resolve(const char *host, int port, net_addr_t *out);
void net_send(const net_addr_t *to, const void *data, int len);
// Non-blocking. Returns the packet length, or -1 when nothing is waiting.
int  net_recv(net_addr_t *from, void *buf, int max);
bool net_addr_eq(const net_addr_t *a, const net_addr_t *b);
const char *net_addr_str(const net_addr_t *a);
double net_now_ms(void);

// Testing: delay every outgoing packet by latency +- jitter ms and drop loss_pct %.
void net_simulate(int latency_ms, int jitter_ms, int loss_pct);
