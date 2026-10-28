/* vos.h — абстракция VOS (Vendor OS) по образцу API VOS_* из прошивки.
 * Задачи отображаются на pthreads, почтовые ящики — на FIFO с mutex+condvar,
 * таймеры — на задачу-тик, которая шлёт VOS_MSG_TIMER в ящик владельца. */
#ifndef VOS_H
#define VOS_H
#include "fw_abi.h"

#define VOS_MAX_FID    32
#define VOS_MSG_MAX    768
#define VOS_MSG_TIMER  0xFF01   /* зарезервированный msgType от службы таймеров */

typedef uint16_t VOS_Fid;

typedef struct VosMsg {
    struct VosMsg *next;
    VOS_Fid   srcFid;
    VOS_Fid   dstFid;
    uint16_t  msgType;
    uint16_t  transId;        /* id транзакции; у таймеров несёт timerId */
    uint16_t  len;
    uint8_t   data[VOS_MSG_MAX];
} VOS_Msg;

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t  cond;
    VOS_Msg        *head, *tail;
    int             count;
    int             peak;      /* пиковая глубина очереди (для Guarder) */
} VOS_MailBox;

typedef pthread_mutex_t VOS_SemId;
typedef void (*VOS_TaskEntry)(void *arg);

void      VOS_Init(void);
uint32_t  VOS_GetTick(void);                 /* монотонные миллисекунды */
void      VOS_Sleep(uint32_t ms);

/* почтовый ящик + маршрутизация по FID */
void      VOS_MbInit(VOS_MailBox *mb);
VOS_Msg  *VOS_MbRecv(VOS_MailBox *mb);        /* блокирующее извлечение */
void      VOS_BindFidMailbox(VOS_Fid fid, VOS_MailBox *mb);
VOS_Msg  *VOS_AllocMsg(uint16_t len);
void      VOS_FreeMsg(VOS_Msg *m);
int       VOS_SendMsg(VOS_Fid dst, VOS_Msg *m);   /* положить в ящик dst */

/* задачи + семафоры */
int       VOS_CreateTask(const char *name, VOS_TaskEntry entry, void *arg);
void      VOS_SmMCreate(VOS_SemId *s);
void      VOS_SmP(VOS_SemId *s);
void      VOS_SmV(VOS_SemId *s);

/* относительные таймеры: прислать VOS_MSG_TIMER (transId=timerId) в fid через ms */
int       VOS_StartRelTimer(VOS_Fid fid, uint16_t timerId, uint32_t ms, int periodic);
void      VOS_StopRelTimer(VOS_Fid fid, uint16_t timerId);

#endif /* VOS_H */
