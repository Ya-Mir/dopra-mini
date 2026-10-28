/* rosa.h — актёрный фреймворк ROSA/MARP поверх VOS, по образцу прошивки:
 *   - таблица FID (FID | FIDName | MsgProcTask)
 *   - актёр CAgent: свой почтовый ящик, состояние FSM, run-loop PushCmd->Dispatch
 *   - MarpRootMsgDispatcher: маршрутизация по (dstFid -> msgType)
 *   - Guarder: предупреждает при затоваривании ящика ("Maybe msg overload!!!")
 *   - TLV-тело сообщений (ср. BBRD_RawMsgGetTlv) */
#ifndef ROSA_H
#define ROSA_H
#include "vos.h"

#define ROSA_GUARD_THRESHOLD 16   /* глубина очереди, при которой срабатывает Guarder */

typedef struct CAgent CAgent;
typedef void (*ROSA_MsgProc)(CAgent *self, VOS_Msg *m);   /* MsgProcTask */

struct CAgent {
    VOS_Fid       fid;
    const char   *fidName;
    VOS_MailBox   mbox;
    int           fsmState;
    ROSA_MsgProc  msgProc;
    void         *ctx;            /* приватное состояние актёра */
};

void    ROSA_Init(void);
CAgent *ROSA_RegisterFid(VOS_Fid fid, const char *name, ROSA_MsgProc proc, void *ctx);
CAgent *ROSA_GetAgent(VOS_Fid fid);
void    ROSA_StartAll(void);           /* по одной run-loop задаче на каждый актёр */
void    ROSA_DumpFidTable(void);

const char *ROSA_StateName(const char *const *names, int n, int st);

/* TLV-кодек: [type u8][len u16 BE][value]. Возвращает число байт / 0 при промахе. */
int  TLV_Put(uint8_t *buf, int cap, int *off, uint8_t type, const void *val, uint16_t len);
int  TLV_Get(const uint8_t *buf, int buflen, uint8_t type, const uint8_t **val, uint16_t *len);

#endif /* ROSA_H */
