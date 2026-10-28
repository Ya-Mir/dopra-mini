/* rosa.c — реализация акторного фреймворка. */
#include "rosa.h"

static CAgent *g_fidTable[VOS_MAX_FID];

void ROSA_Init(void)
{
    for (int i = 0; i < VOS_MAX_FID; i++) g_fidTable[i] = NULL;
}

CAgent *ROSA_RegisterFid(VOS_Fid fid, const char *name, ROSA_MsgProc proc, void *ctx)
{
    if (fid >= VOS_MAX_FID) return NULL;
    CAgent *a = (CAgent *)malloc(sizeof(CAgent));
    memset(a, 0, sizeof(*a));
    a->fid = fid; a->fidName = name; a->msgProc = proc; a->ctx = ctx; a->fsmState = 0;
    VOS_MbInit(&a->mbox);
    VOS_BindFidMailbox(fid, &a->mbox);      /* чтобы VOS_SendMsg(fid,..) доходил до нас */
    g_fidTable[fid] = a;
    return a;
}

CAgent *ROSA_GetAgent(VOS_Fid fid)
{
    return (fid < VOS_MAX_FID) ? g_fidTable[fid] : NULL;
}

/* run-loop = задача актора CAgent: достать сообщение, прогнать Guarder, дать
 * диспетчеру MarpRoot направить его в MsgProcTask актора. */
static void agent_runloop(void *arg)
{
    CAgent *a = (CAgent *)arg;
    for (;;) {
        VOS_Msg *m = VOS_MbRecv(&a->mbox);          /* его положил CAgent::PushCmd */
        if (a->mbox.count >= ROSA_GUARD_THRESHOLD)
            LOG("  [Guarder] FID<%u:%s> MsgQueueNum<%d> Maybe msg overload!!!\n",
                a->fid, a->fidName, a->mbox.count);
        /* MarpRootMsgDispatcher: маршрут по dstFid -> msgType */
        LOG("  [MarpRoot->Dispatch] PID<%u:%-10s> msgType<0x%04x> transId<%u>\n",
            a->fid, a->fidName, m->msgType, m->transId);
        if (a->msgProc) a->msgProc(a, m);
        VOS_FreeMsg(m);
    }
}

void ROSA_StartAll(void)
{
    for (int i = 0; i < VOS_MAX_FID; i++)
        if (g_fidTable[i])
            VOS_CreateTask(g_fidTable[i]->fidName, agent_runloop, g_fidTable[i]);
}

void ROSA_DumpFidTable(void)
{
    LOG("  FID   FIDName        MsgProcTask\n");
    LOG("  ---   -------------  -----------\n");
    for (int i = 0; i < VOS_MAX_FID; i++)
        if (g_fidTable[i])
            LOG("  %-5u %-13s  agent_runloop\n", g_fidTable[i]->fid, g_fidTable[i]->fidName);
}

const char *ROSA_StateName(const char *const *names, int n, int st)
{
    return (st >= 0 && st < n) ? names[st] : "?";
}

/* ---------------- TLV ---------------- */
int TLV_Put(uint8_t *buf, int cap, int *off, uint8_t type, const void *val, uint16_t len)
{
    if (*off + 3 + len > cap) return 0;
    buf[*off]     = type;
    buf[*off + 1] = (uint8_t)(len >> 8);        /* длина в big-endian */
    buf[*off + 2] = (uint8_t)(len & 0xff);
    if (len && val) memcpy(buf + *off + 3, val, len);
    *off += 3 + len;
    return 3 + len;
}

int TLV_Get(const uint8_t *buf, int buflen, uint8_t type, const uint8_t **val, uint16_t *len)
{
    int i = 0;
    while (i + 3 <= buflen) {
        uint8_t  t = buf[i];
        uint16_t l = (uint16_t)((buf[i + 1] << 8) | buf[i + 2]);
        if (i + 3 + l > buflen) break;
        if (t == type) { if (val) *val = buf + i + 3; if (len) *len = l; return 1; }
        i += 3 + l;
    }
    return 0;
}
