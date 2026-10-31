// Portable UDP (see net.h). IPv4 only: enough for direct connections, LANs
// and VPNs such as Tailscale or ZeroTier.
#include "net.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define BAD_SOCK INVALID_SOCKET
#define close_sock closesocket
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
#define BAD_SOCK (-1)
#define close_sock close
#endif

static sock_t sock = BAD_SOCK;

double net_now_ms(void)
{
    static uint64_t t0;
    uint64_t now = SDL_GetPerformanceCounter();
    if (!t0) t0 = now;
    return (double)(now - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
}

bool net_open(int port)
{
#ifdef _WIN32
    static bool wsa;
    if (!wsa) { WSADATA w; if (WSAStartup(MAKEWORD(2, 2), &w)) return false; wsa = true; }
#endif
    net_close();
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == BAD_SOCK) return false;
    struct sockaddr_in a = { 0 };
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons((uint16_t)port);
    if (bind(sock, (struct sockaddr *)&a, sizeof a) != 0) {
        fprintf(stderr, "net: cannot use UDP port %d\n", port);
        net_close();
        return false;
    }
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(sock, FIONBIO, &nb);
#else
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
#endif
    return true;
}

void net_close(void)
{
    if (sock != BAD_SOCK) close_sock(sock);
    sock = BAD_SOCK;
}

bool net_resolve(const char *host, int port, net_addr_t *out)
{
    struct addrinfo hints = { 0 }, *res = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return false;
    struct sockaddr_in *sa = (struct sockaddr_in *)res->ai_addr;
    memcpy(out->ip, &sa->sin_addr, 4);
    out->port = (uint16_t)port;
    freeaddrinfo(res);
    return true;
}

bool net_addr_eq(const net_addr_t *a, const net_addr_t *b)
{
    return !memcmp(a->ip, b->ip, 4) && a->port == b->port;
}

const char *net_addr_str(const net_addr_t *a)
{
    static char buf[32];
    snprintf(buf, sizeof buf, "%u.%u.%u.%u:%u", a->ip[0], a->ip[1], a->ip[2], a->ip[3], a->port);
    return buf;
}

static void raw_send(const net_addr_t *to, const void *data, int len)
{
    if (sock == BAD_SOCK) return;
    struct sockaddr_in a = { 0 };
    a.sin_family = AF_INET;
    memcpy(&a.sin_addr, to->ip, 4);
    a.sin_port = htons(to->port);
    sendto(sock, (const char *)data, len, 0, (struct sockaddr *)&a, sizeof a);
}

/* ---------------------------------------------------------------- */
/* Simulated network conditions                                     */
/* ---------------------------------------------------------------- */

static int sim_latency, sim_jitter, sim_loss;
#define SIM_QUEUE 1024
static struct { double at; net_addr_t to; int len; uint8_t data[1400]; } simq[SIM_QUEUE];
static int simq_n;

void net_simulate(int latency_ms, int jitter_ms, int loss_pct)
{
    sim_latency = latency_ms; sim_jitter = jitter_ms; sim_loss = loss_pct;
}

static void sim_flush(void)
{
    double now = net_now_ms();
    for (int i = 0; i < simq_n;) {
        if (simq[i].at <= now) {
            raw_send(&simq[i].to, simq[i].data, simq[i].len);
            simq[i] = simq[--simq_n];
        } else i++;
    }
}

void net_send(const net_addr_t *to, const void *data, int len)
{
    if (!sim_latency && !sim_jitter && !sim_loss) { raw_send(to, data, len); return; }
    if (rand() % 100 < sim_loss) return;
    if (simq_n == SIM_QUEUE || len > (int)sizeof simq[0].data) return;
    double j = sim_jitter ? (rand() % (2 * sim_jitter + 1)) - sim_jitter : 0;
    simq[simq_n].at = net_now_ms() + sim_latency + j;
    simq[simq_n].to = *to;
    simq[simq_n].len = len;
    memcpy(simq[simq_n].data, data, (size_t)len);
    simq_n++;
    sim_flush();
}

int net_recv(net_addr_t *from, void *buf, int max)
{
    if (sim_latency || sim_jitter || sim_loss) sim_flush();
    if (sock == BAD_SOCK) return -1;
    struct sockaddr_in a;
    socklen_t al = sizeof a;
    int n = (int)recvfrom(sock, (char *)buf, max, 0, (struct sockaddr *)&a, &al);
    if (n < 0) return -1;
    memcpy(from->ip, &a.sin_addr, 4);
    from->port = ntohs(a.sin_port);
    return n;
}
