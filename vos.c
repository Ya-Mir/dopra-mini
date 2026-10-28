/* vos.c — реализация VOS поверх pthreads. */
#include "vos.h"

static uint32_t         g_startSec;
static VOS_MailBox     *g_fidMbox[VOS_MAX_FID];
static pthread_mutex_t  g_timerLock;

/* ---------------- время ---------------- */
void VOS_Init(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    g_startSec = (uint32_t)ts.tv_sec;
    pthread_mutex_init(&g_timerLock, NULL);   /* под нашим crt нет .init_array */
}

uint32_t VOS_GetTick(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec - g_startSec) * 1000u + (uint32_t)(ts.tv_nsec / 1000000);
}

void VOS_Sleep(uint32_t ms)
{
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

/* ---------------- почтовый ящик ---------------- */
void VOS_MbInit(VOS_MailBox *mb)
{
    pthread_mutex_init(&mb->lock, NULL);
    pthread_cond_init(&mb->cond, NULL);
    mb->head = mb->tail = NULL;
    mb->count = mb->peak = 0;
}

static void mb_push(VOS_MailBox *mb, VOS_Msg *m)
{
    pthread_mutex_lock(&mb->lock);
    m->next = NULL;
    if (mb->tail) mb->tail->next = m; else mb->head = m;
    mb->tail = m;
    if (++mb->count > mb->peak) mb->peak = mb->count;
    pthread_cond_signal(&mb->cond);
    pthread_mutex_unlock(&mb->lock);
}

VOS_Msg *VOS_MbRecv(VOS_MailBox *mb)
{
    VOS_Msg *m;
    pthread_mutex_lock(&mb->lock);
    while (mb->head == NULL)
        pthread_cond_wait(&mb->cond, &mb->lock);
    m = mb->head;
    mb->head = m->next;
    if (mb->head == NULL) mb->tail = NULL;
    mb->count--;
    pthread_mutex_unlock(&mb->lock);
    return m;
}

void VOS_BindFidMailbox(VOS_Fid fid, VOS_MailBox *mb)
{
    if (fid < VOS_MAX_FID) g_fidMbox[fid] = mb;
}

VOS_Msg *VOS_AllocMsg(uint16_t len)
{
    VOS_Msg *m = (VOS_Msg *)malloc(sizeof(VOS_Msg));
    if (!m) return NULL;
    memset(m, 0, sizeof(*m));
    m->len = (len > VOS_MSG_MAX) ? VOS_MSG_MAX : len;
    return m;
}

void VOS_FreeMsg(VOS_Msg *m) { free(m); }

int VOS_SendMsg(VOS_Fid dst, VOS_Msg *m)
{
    if (dst >= VOS_MAX_FID || !g_fidMbox[dst]) { VOS_FreeMsg(m); return -1; }
    m->dstFid = dst;
    mb_push(g_fidMbox[dst], m);
    return 0;
}

/* ---------------- задачи ---------------- */
typedef struct { VOS_TaskEntry entry; void *arg; } TaskCtx;

static void *task_trampoline(void *p)
{
    TaskCtx *c = (TaskCtx *)p;
    VOS_TaskEntry e = c->entry; void *a = c->arg;
    free(c);
    e(a);
    return NULL;
}

int VOS_CreateTask(const char *name, VOS_TaskEntry entry, void *arg)
{
    pthread_t tid;
    TaskCtx *c = (TaskCtx *)malloc(sizeof(TaskCtx));
    (void)name;
    c->entry = entry; c->arg = arg;
    return pthread_create(&tid, NULL, task_trampoline, c);
}

/* ---------------- семафор (mutex) ---------------- */
void VOS_SmMCreate(VOS_SemId *s) { pthread_mutex_init(s, NULL); }
void VOS_SmP(VOS_SemId *s)       { pthread_mutex_lock(s); }
void VOS_SmV(VOS_SemId *s)       { pthread_mutex_unlock(s); }

/* ---------------- служба таймеров ---------------- */
typedef struct Timer {
    struct Timer *next;
    VOS_Fid   fid;
    uint16_t  timerId;
    int       active;
    int       periodic;
    int32_t   remain;     /* осталось мс */
    uint32_t  reload;
} Timer;

#define TIMER_TICK_MS 5
#define MAX_TIMERS    64
static Timer           g_timers[MAX_TIMERS];

static void timer_task(void *unused)
{
    (void)unused;
    for (;;) {
        VOS_Sleep(TIMER_TICK_MS);
        pthread_mutex_lock(&g_timerLock);
        for (int i = 0; i < MAX_TIMERS; i++) {
            Timer *t = &g_timers[i];
            if (!t->active) continue;
            t->remain -= TIMER_TICK_MS;
            if (t->remain <= 0) {
                VOS_Fid fid = t->fid; uint16_t id = t->timerId;
                if (t->periodic) t->remain = (int32_t)t->reload;
                else             t->active = 0;
                pthread_mutex_unlock(&g_timerLock);
                VOS_Msg *m = VOS_AllocMsg(0);
                if (m) { m->msgType = VOS_MSG_TIMER; m->transId = id; m->srcFid = fid; VOS_SendMsg(fid, m); }
                pthread_mutex_lock(&g_timerLock);
            }
        }
        pthread_mutex_unlock(&g_timerLock);
    }
}

static int g_timerStarted;

int VOS_StartRelTimer(VOS_Fid fid, uint16_t timerId, uint32_t ms, int periodic)
{
    pthread_mutex_lock(&g_timerLock);
    if (!g_timerStarted) { g_timerStarted = 1; VOS_CreateTask("VosTimer", timer_task, NULL); }
    /* переиспользуем слот для этого (fid,timerId) либо берём свободный */
    Timer *slot = NULL;
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (g_timers[i].active && g_timers[i].fid == fid && g_timers[i].timerId == timerId) { slot = &g_timers[i]; break; }
    }
    if (!slot) for (int i = 0; i < MAX_TIMERS; i++) if (!g_timers[i].active) { slot = &g_timers[i]; break; }
    if (slot) {
        slot->fid = fid; slot->timerId = timerId; slot->active = 1;
        slot->periodic = periodic; slot->remain = (int32_t)ms; slot->reload = ms;
    }
    pthread_mutex_unlock(&g_timerLock);
    return slot ? 0 : -1;
}

void VOS_StopRelTimer(VOS_Fid fid, uint16_t timerId)
{
    pthread_mutex_lock(&g_timerLock);
    for (int i = 0; i < MAX_TIMERS; i++)
        if (g_timers[i].active && g_timers[i].fid == fid && g_timers[i].timerId == timerId)
            g_timers[i].active = 0;
    pthread_mutex_unlock(&g_timerLock);
}
