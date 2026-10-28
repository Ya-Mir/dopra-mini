# mini-DOPRA / ROSA — миниатюрный акторный рантайм в духе Huawei, запускающий TFTP поверх UDP

Минимальная, но достоверная реализация (на C) акторного рантайма **DOPRA/ROSA**,
восстановленного по прошивке базовой станции Huawei, с запуском поверх него
реального сетевого протокола — **TFTP (RFC 1350), чтение файла по UDP**. Именно так
RRU Huawei тянет образ прошивки с BBU, и этот протокол задействует все части
рантайма: почтовые ящики акторов, диспетчер, конечные автоматы (FSM), таймеры
ретрансмита и настоящие UDP-сокеты.

Собрано под **реальный ABI прошивки**: ARMv7 **big-endian (BE8)**, Linux,
динамическая линковка с **родной glibc 2.29 из прошивки** (`.so` от БС), запуск
под QEMU — и в user-mode (`qemu-armeb`), и в полноценном госте
`qemu-system-arm -M hi1380` (прошивочный Linux `armv7b`, ядро 4.4.197).

## Слои (достоверные имена `VOS_*` / `CAgent` / FID)

| Файл | Назначение | Соответствие в прошивке |
|------|------------|-------------------------|
| `fw_abi.h` | ABI-прослойка с ручными объявлениями libc (заголовков нет — линкуемся с libc прошивки) | — |
| `start.S` | минимальный crt1: `_start` → `__libc_start_main` (готового crt1.o нет) | glibc start.S |
| `vos.c/.h` | VOS: задачи (pthreads), почтовые ящики, таймеры, семафоры | `VOS_CreateTask`, `VOS_SendMsg`, `VOS_AllocMsg`, `VOS_SmMCreate/SmP/SmV`, `VOS_StartRelTimer`, `VOS_GetTick` |
| `rosa.c/.h` | ROSA: таблица FID, run-loop `CAgent`, диспетчер MarpRoot, Guarder, TLV | `CAgent::PushCmd/Dispatch`, `MarpRootMsgDispatcher`, таблица FID, `BBRD_RawMsgGetTlv` |
| `tftp.c/.h` | акторы TFTP-сервера и клиента + задача NetRx, ретрансмит по таймеру VOS | — |
| `main.c` | старт рантайма + две демо-передачи (чистая и с потерей пакета) | — |

Путь одного пакета: `NetRx (сокет) → VOS_SendMsg → почтовый ящик CAgent →
диспетчер MarpRoot (по dstFid→msgType) → FSM актора → sendto`, а ретрансмит
запускает `VOS_StartRelTimer`, присылающий `VOS_MSG_TIMER`.

## Сборка (в WSL — тулчейн и sysroot прошивки лежат в `packer/tools/`)

```bash
bash build.sh          # clang-18 --target=armv7eb … + GNU ld --be8, линкует
                       # .so из sysroot прошивки  →  dopra_tftp.out (ELF32 MSB ARM)
```

## Запуск

**A. Быстро — user-mode QEMU (`qemu-armeb`), glibc прошивки как sysroot:**
```bash
bin/qemu-armeb -L ../sysroot ./dopra_tftp.out
```

**B. Достоверно — внутри полноценного прошивочного гостя `hi1380`** (в WSL под root):
доставить бинарь через уже готовый NFS-экспорт, загрузить гостя, затем в госте:
```sh
mount -t nfs -o nolock,ro,tcp,port=2049,mountport=20048,nfsvers=3 \
      10.0.2.2:/home/yakov/nfsmptg /mnt/nfs
mount -o remount,exec /tmp          # /tmp по умолчанию noexec
cp /mnt/nfs/dopra_tftp.out /tmp/d && chmod +x /tmp/d && /tmp/d
```
(Механика NFS+загрузки описана в `packer/tools/MPTG_MANUAL_RUN.md`.)

## Ожидаемый вывод (сокращённо)

```
-- FID table --
  2     TftpServer     agent_runloop
  3     TftpClient     agent_runloop
==== Transfer 1: clean read of firmware.img (2600 bytes) ====
  [MarpRoot->Dispatch] PID<3:TftpClient> msgType<0x0001> transId<0>
    [CLI IDLE] START -> send RRQ
    ...
    [CLI WAIT_DATA->DONE] received 2600 bytes, verify=OK
==== result: SUCCESS (rc=0, 21 ms) ====
==== Transfer 2: same read with simulated loss of DATA block #2 ====
    [SRV] (loss-sim) DROPPING DATA block 2 once
    [SRV SENDING] TIMEOUT -> retransmit DATA block 2 (retry 1)
    [CLI WAIT_DATA->DONE] received 2600 bytes, verify=OK
==== result: SUCCESS (rc=0, 524 ms) ====     # ~500 мс = один таймаут ретрансмита
```

## Заметки
- Без системных заголовков: каждый тип/прототип libc объявлен вручную в `fw_abi.h`
  под ABI glibc 2.29 ARM EABI, потому что линкуемся с `.so` прошивки (а они идут
  без заголовков и без crt-объектов). `_start` сам вызывает `__libc_start_main`.
- Порядок байт: цель — BE8, поэтому `htons/htonl` — тождественные (`HTONS` в `fw_abi.h`).
- Это учебная модель структуры, а не полный порт (~700 строк C).
