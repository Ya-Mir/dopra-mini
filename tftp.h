/* tftp.h — чтение по TFTP/UDP (RFC 1350), реализованное как актёры ROSA. */
#ifndef TFTP_H
#define TFTP_H
#include "rosa.h"

#define FID_TFTP_SRV 2
#define FID_TFTP_CLI 3

/* зарегистрировать актёров сервера+клиента, открыть UDP-сокеты, запустить
 * задачи-читатели NetRx. */
void TFTP_SetupDemo(void);

/* сбросить состояние передачи и пнуть клиента (MSG_START), чтобы начать запрос.
 * simulateLoss != 0 заставляет сервер один раз уронить DATA-блок #2 для ретрансмита. */
void TFTP_StartTransfer(int simulateLoss);

/* блокируется до конца передачи. Возвращает 0 при успехе (байты проверены),
 * отрицательное — при ошибке/таймауте. */
int  TFTP_WaitDone(uint32_t timeoutMs);

#endif /* TFTP_H */
