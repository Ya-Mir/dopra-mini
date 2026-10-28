/* main.c — поднимаем рантайм mini-DOPRA/ROSA и запускаем поверх него TFTP/UDP.
 * Две передачи через 127.0.0.1: сначала чистая, затем с имитацией потери пакета,
 * которую восстанавливает ретрансмит по таймеру VOS. */
#include "tftp.h"

static void run(const char *title, int loss)
{
    LOG("\n==== %s ====\n", title);
    uint32_t t0 = VOS_GetTick();
    TFTP_StartTransfer(loss);
    int rc = TFTP_WaitDone(8000);
    uint32_t dt = VOS_GetTick() - t0;
    LOG("==== result: %s (rc=%d, %u ms) ====\n",
        rc == 0 ? "SUCCESS" : "FAILURE", rc, dt);
}

int main(void)
{
    LOG("######################################################\n");
    LOG("#  mini-DOPRA / ROSA  —  ARM big-endian, firmware glibc\n");
    LOG("#  TFTP (RFC 1350) read transfer over UDP, as actors\n");
    LOG("######################################################\n");

    VOS_Init();
    ROSA_Init();
    TFTP_SetupDemo();

    LOG("\n-- FID table --\n");
    ROSA_DumpFidTable();

    ROSA_StartAll();          /* по одной run-loop задаче CAgent на каждый FID */
    VOS_Sleep(100);           /* дать run-loop и задачам NetRx подняться */

    run("Transfer 1: clean read of firmware.img (2600 bytes)", 0);
    run("Transfer 2: same read with simulated loss of DATA block #2", 1);

    LOG("\nAll done.\n");
    return 0;
}
