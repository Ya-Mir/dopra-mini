/* tftp.c — чтение по TFTP (RRQ) поверх UDP как два актора DOPRA + задачи NetRx.
 * Сервер отдаёт файл из памяти блоками DATA по 512 байт; клиент подтверждает
 * каждый и собирает обратно. Ретрансмит ведёт относительный таймер VOS. */
#include "tftp.h"

/* ---- типы сообщений приложения / теги TLV / id таймеров ---- */
#define MSG_START   0x0001
#define MSG_NET_RX  0x0002
#define TIMER_RETX  1

#define T_FROM      1          /* TLV: struct sockaddr_in отправителя */
#define T_PAYLOAD   2          /* TLV: сырые байты UDP-датаграммы */

/* ---- опкоды TFTP ---- */
#define OP_RRQ 1
#define OP_DATA 3
#define OP_ACK 4
#define OP_ERROR 5
#define BLK 512
#define RETX_MS 500
#define MAX_RETRY 5

/* ---- состояния FSM ---- */
enum { SRV_IDLE, SRV_SENDING, SRV_DONE };
enum { CLI_IDLE, CLI_WAIT_DATA, CLI_DONE, CLI_FAIL };
static const char *const SRV_NM[] = { "IDLE", "SENDING", "DONE" };
static const char *const CLI_NM[] = { "IDLE", "WAIT_DATA", "DONE", "FAIL" };

typedef struct {
    int                fd;
    VOS_Fid            self;
    struct sockaddr_in peer;          /* srv: TID клиента ; cli: адрес сервера */
    const uint8_t     *file; int filelen;     /* сервер: отдаваемые данные */
    uint8_t           *rbuf;  int rlen; int rcap;  /* клиент: сборка */
    int                block;         /* srv: отправляемый блок; cli: ожидаемый */
    int                lastlen;       /* srv: длина полезной нагрузки текущего блока */
    int                retries;
    uint8_t            last[BLK + 4]; int lastn;   /* последний отправленный пакет (ретрансмит) */
    int                dropBlock;     /* srv: уронить этот DATA-блок один раз (-1=никогда) */
    int                dropped;
} TftpCtx;

static TftpCtx g_srv, g_cli;
static volatile int g_done;           /* 0 идёт, 1 ок, <0 ошибка */
static const uint8_t *g_serveFile; static int g_serveLen;   /* для проверки */

/* ---- вспомогательные ---- */
static void put16(uint8_t *p, int v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static int  get16(const uint8_t *p)  { return (p[0] << 8) | p[1]; }

static void udp_send(TftpCtx *c, const uint8_t *buf, int n, const char *tag)
{
    sendto(c->fd, buf, (size_t)n, 0, (const struct sockaddr *)&c->peer, sizeof(c->peer));
    LOG("      [%s] TX %s %d bytes to port %u\n",
        c->self == FID_TFTP_SRV ? "SRV" : "CLI", tag, n, NTOHS(c->peer.sin_port));
}

/* сервер: (пере)слать DATA текущего блока, учитывая одноразовую симуляцию потери */
static void srv_send_data(CAgent *a, TftpCtx *c)
{
    uint8_t pkt[BLK + 4];
    int off = (c->block - 1) * BLK;
    int n = c->filelen - off; if (n > BLK) n = BLK; if (n < 0) n = 0;
    c->lastlen = n;
    put16(pkt, OP_DATA); put16(pkt + 2, c->block);
    memcpy(pkt + 4, c->file + off, n);
    memcpy(c->last, pkt, 4 + n); c->lastn = 4 + n;
    if (c->dropBlock == c->block && !c->dropped) {
        c->dropped = 1;
        LOG("      [SRV] (loss-sim) DROPPING DATA block %d once\n", c->block);
    } else {
        udp_send(c, pkt, 4 + n, "DATA");
    }
    VOS_StartRelTimer(a->fid, TIMER_RETX, RETX_MS, 0);
}

/* ---- MsgProcTask сервера ---- */
static void srv_proc(CAgent *a, VOS_Msg *m)
{
    TftpCtx *c = (TftpCtx *)a->ctx;
    if (m->msgType == MSG_NET_RX) {
        const uint8_t *from, *pl; uint16_t fl, pn;
        if (!TLV_Get(m->data, m->len, T_FROM, &from, &fl)) return;
        if (!TLV_Get(m->data, m->len, T_PAYLOAD, &pl, &pn)) return;
        int op = get16(pl);
        if (op == OP_RRQ) {
            memcpy(&c->peer, from, sizeof(c->peer));
            const char *fn = (const char *)(pl + 2);
            LOG("    [SRV %s] RRQ \"%s\" -> start, %d bytes in %d blocks\n",
                SRV_NM[a->fsmState], fn, c->filelen, (c->filelen / BLK) + 1);
            a->fsmState = SRV_SENDING; c->block = 1; c->retries = 0;
            srv_send_data(a, c);
        } else if (op == OP_ACK && a->fsmState == SRV_SENDING) {
            int ack = get16(pl + 2);
            LOG("    [SRV %s] ACK block %d\n", SRV_NM[a->fsmState], ack);
            if (ack == c->block) {
                VOS_StopRelTimer(a->fid, TIMER_RETX);
                if (c->lastlen < BLK) {       /* это был последний блок */
                    a->fsmState = SRV_DONE;
                    LOG("    [SRV %s->DONE] transfer complete\n", SRV_NM[SRV_SENDING]);
                } else {
                    c->block++; c->retries = 0; srv_send_data(a, c);
                }
            }
        }
    } else if (m->msgType == VOS_MSG_TIMER && a->fsmState == SRV_SENDING) {
        if (++c->retries > MAX_RETRY) { LOG("    [SRV] giving up (max retries)\n"); return; }
        LOG("    [SRV %s] TIMEOUT -> retransmit DATA block %d (retry %d)\n",
            SRV_NM[a->fsmState], c->block, c->retries);
        udp_send(c, c->last, c->lastn, "DATA(retx)");
        VOS_StartRelTimer(a->fid, TIMER_RETX, RETX_MS, 0);
    }
}

/* ---- MsgProcTask клиента ---- */
static void cli_send_rrq(CAgent *a, TftpCtx *c)
{
    uint8_t pkt[64]; int n = 0;
    put16(pkt, OP_RRQ); n = 2;
    const char *fn = "firmware.img"; int fl = (int)strlen(fn);
    memcpy(pkt + n, fn, fl + 1); n += fl + 1;
    memcpy(pkt + n, "octet", 6); n += 6;
    memcpy(c->last, pkt, n); c->lastn = n;
    udp_send(c, pkt, n, "RRQ");
    VOS_StartRelTimer(a->fid, TIMER_RETX, RETX_MS, 0);
}

static void cli_send_ack(CAgent *a, TftpCtx *c, int block)
{
    uint8_t pkt[4]; put16(pkt, OP_ACK); put16(pkt + 2, block);
    memcpy(c->last, pkt, 4); c->lastn = 4;
    udp_send(c, pkt, 4, "ACK");
}

static void cli_proc(CAgent *a, VOS_Msg *m)
{
    TftpCtx *c = (TftpCtx *)a->ctx;
    if (m->msgType == MSG_START) {
        LOG("    [CLI %s] START -> send RRQ\n", CLI_NM[a->fsmState]);
        a->fsmState = CLI_WAIT_DATA; c->block = 1; c->retries = 0; c->rlen = 0;
        cli_send_rrq(a, c);
    } else if (m->msgType == MSG_NET_RX && a->fsmState == CLI_WAIT_DATA) {
        const uint8_t *pl; uint16_t pn;
        if (!TLV_Get(m->data, m->len, T_PAYLOAD, &pl, &pn)) return;
        int op = get16(pl);
        if (op == OP_DATA) {
            int blk = get16(pl + 2); int dn = pn - 4;
            if (blk == c->block) {
                VOS_StopRelTimer(a->fid, TIMER_RETX);
                if (c->rlen + dn <= c->rcap) { memcpy(c->rbuf + c->rlen, pl + 4, dn); c->rlen += dn; }
                LOG("    [CLI %s] DATA block %d (%d bytes) -> ACK, total %d\n",
                    CLI_NM[a->fsmState], blk, dn, c->rlen);
                cli_send_ack(a, c, blk);
                if (dn < BLK) {        /* последний блок */
                    a->fsmState = CLI_DONE;
                    int ok = (c->rlen == g_serveLen) && (memcmp(c->rbuf, g_serveFile, c->rlen) == 0);
                    LOG("    [CLI %s->DONE] received %d bytes, verify=%s\n",
                        CLI_NM[CLI_WAIT_DATA], c->rlen, ok ? "OK" : "MISMATCH");
                    g_done = ok ? 1 : -1;
                } else {
                    c->block++; c->retries = 0;
                    VOS_StartRelTimer(a->fid, TIMER_RETX, RETX_MS, 0);
                }
            } else if (blk < c->block) {
                LOG("    [CLI %s] duplicate DATA block %d -> re-ACK\n", CLI_NM[a->fsmState], blk);
                cli_send_ack(a, c, blk);
            }
        } else if (op == OP_ERROR) {
            LOG("    [CLI] ERROR from server\n"); a->fsmState = CLI_FAIL; g_done = -1;
        }
    } else if (m->msgType == VOS_MSG_TIMER && a->fsmState == CLI_WAIT_DATA) {
        if (++c->retries > MAX_RETRY) { LOG("    [CLI] giving up\n"); a->fsmState = CLI_FAIL; g_done = -1; return; }
        LOG("    [CLI %s] TIMEOUT -> retransmit last pkt (retry %d)\n", CLI_NM[a->fsmState], c->retries);
        udp_send(c, c->last, c->lastn, "retx");
        VOS_StartRelTimer(a->fid, TIMER_RETX, RETX_MS, 0);
    }
}

/* ---- задача-читатель NetRx: сокет -> шина ---- */
typedef struct { int fd; VOS_Fid fid; } NetArg;
static void netrx_task(void *arg)
{
    NetArg *na = (NetArg *)arg;
    for (;;) {
        uint8_t buf[VOS_MSG_MAX]; struct sockaddr_in from; socklen_t fl = sizeof(from);
        ssize_t n = recvfrom(na->fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
        if (n <= 0) continue;
        VOS_Msg *m = VOS_AllocMsg(0);
        if (!m) continue;
        m->msgType = MSG_NET_RX; m->srcFid = FID_TFTP_SRV; /* для информации */
        int off = 0;
        TLV_Put(m->data, VOS_MSG_MAX, &off, T_FROM, &from, sizeof(from));
        TLV_Put(m->data, VOS_MSG_MAX, &off, T_PAYLOAD, buf, (uint16_t)n);
        m->len = (uint16_t)off;
        VOS_SendMsg(na->fid, m);
    }
}

/* ---- инициализация ---- */
static int open_udp(uint32_t ip, uint16_t port)
{
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) return -1;
    struct sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_port = HTONS(port); a.sin_addr.s_addr = HTONL(ip);
    if (bind(fd, (const struct sockaddr *)&a, sizeof(a)) < 0) { close(fd); return -1; }
    return fd;
}

void TFTP_SetupDemo(void)
{
    static uint8_t filebuf[2600];
    static uint8_t clibuf[4096];
    for (int i = 0; i < (int)sizeof(filebuf); i++) filebuf[i] = (uint8_t)(i * 7 + (i >> 3));
    g_serveFile = filebuf; g_serveLen = sizeof(filebuf);

    int sfd = open_udp(INADDR_LOOPBACK, 6969);
    int cfd = open_udp(INADDR_LOOPBACK, 0);        /* эфемерный порт */

    memset(&g_srv, 0, sizeof(g_srv));
    g_srv.fd = sfd; g_srv.self = FID_TFTP_SRV; g_srv.file = filebuf; g_srv.filelen = sizeof(filebuf);
    g_srv.dropBlock = -1;

    memset(&g_cli, 0, sizeof(g_cli));
    g_cli.fd = cfd; g_cli.self = FID_TFTP_CLI; g_cli.rbuf = clibuf; g_cli.rcap = sizeof(clibuf);
    g_cli.peer.sin_family = AF_INET; g_cli.peer.sin_port = HTONS(6969);
    g_cli.peer.sin_addr.s_addr = HTONL(INADDR_LOOPBACK);

    ROSA_RegisterFid(FID_TFTP_SRV, "TftpServer", srv_proc, &g_srv);
    ROSA_RegisterFid(FID_TFTP_CLI, "TftpClient", cli_proc, &g_cli);

    static NetArg sa, ca;
    sa.fd = sfd; sa.fid = FID_TFTP_SRV;
    ca.fd = cfd; ca.fid = FID_TFTP_CLI;
    VOS_CreateTask("NetRxSrv", netrx_task, &sa);
    VOS_CreateTask("NetRxCli", netrx_task, &ca);
}

void TFTP_StartTransfer(int simulateLoss)
{
    g_done = 0;
    g_srv.dropBlock = simulateLoss ? 2 : -1; g_srv.dropped = 0;
    g_srv.block = 0; g_srv.retries = 0;
    g_cli.rlen = 0; g_cli.retries = 0;
    CAgent *s = ROSA_GetAgent(FID_TFTP_SRV); if (s) s->fsmState = SRV_IDLE;
    CAgent *c = ROSA_GetAgent(FID_TFTP_CLI); if (c) c->fsmState = CLI_IDLE;
    VOS_Msg *m = VOS_AllocMsg(0);
    m->msgType = MSG_START;
    VOS_SendMsg(FID_TFTP_CLI, m);
}

int TFTP_WaitDone(uint32_t timeoutMs)
{
    uint32_t t0 = VOS_GetTick();
    while (g_done == 0) {
        if (VOS_GetTick() - t0 > timeoutMs) return -2;
        VOS_Sleep(20);
    }
    return g_done == 1 ? 0 : -1;
}
