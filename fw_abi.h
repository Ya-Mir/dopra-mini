/* fw_abi.h — минимальная ABI-прослойка над libc.
 *
 * Линкуемся с родной glibc 2.29 из прошивки (.so от БС), но системных заголовков
 * под нашу big-endian ARM-цель нет, поэтому каждый используемый тип, константа и
 * прототип libc объявлены здесь вручную под ABI glibc 2.29 ARM EABI. Это подход
 * «freestanding поверх libc прошивки»: динамический загрузчик /lib/ld-linux.so.3
 * резолвит эти символы из libc.so.6/libpthread.
 */
#ifndef FW_ABI_H
#define FW_ABI_H

/* ---- типы фиксированной ширины (ARM EABI, 32 бита) ---- */
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef signed char        int8_t;
typedef short              int16_t;
typedef int                int32_t;
typedef unsigned int       size_t;
typedef int                ssize_t;
typedef unsigned int       socklen_t;
typedef unsigned short     sa_family_t;
#ifndef NULL
#define NULL ((void*)0)
#endif

/* ---- порядок байт: цель — ARM BE8, поэтому сетевой порядок == порядку хоста ---- */
#if defined(__ARMEB__) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#  define HTONS(x) ((uint16_t)(x))
#  define HTONL(x) ((uint32_t)(x))
#else
#  define HTONS(x) __builtin_bswap16((uint16_t)(x))
#  define HTONL(x) __builtin_bswap32((uint32_t)(x))
#endif
#define NTOHS(x) HTONS(x)
#define NTOHL(x) HTONL(x)

/* ---- libc: память / строки / ввод-вывод ---- */
void *malloc(size_t);
void  free(void *);
void *calloc(size_t, size_t);
void *memcpy(void *, const void *, size_t);
void *memset(void *, int, size_t);
int   memcmp(const void *, const void *, size_t);
size_t strlen(const char *);
char *strcpy(char *, const char *);
int   strcmp(const char *, const char *);
int   printf(const char *, ...);
int   snprintf(char *, size_t, const char *, ...);
int   fflush(void *);            /* fflush(0) сбрасывает все потоки */
#define LOG(...) do { printf(__VA_ARGS__); fflush(0); } while (0)

/* ---- время ---- */
struct timespec { long tv_sec; long tv_nsec; };   /* 32-битный time_t (glibc 2.29) */
#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1
int clock_gettime(int clk, struct timespec *tp);
int nanosleep(const struct timespec *req, struct timespec *rem);

/* ---- pthreads (размеры по ARM EABI glibc) ---- */
typedef unsigned long pthread_t;
typedef union { char __size[24]; long __align; } pthread_mutex_t;
typedef union { char __size[48]; long long __align; } pthread_cond_t;
int pthread_create(pthread_t *, const void *attr, void *(*fn)(void *), void *arg);
int pthread_join(pthread_t, void **);
int pthread_mutex_init(pthread_mutex_t *, const void *);
int pthread_mutex_lock(pthread_mutex_t *);
int pthread_mutex_unlock(pthread_mutex_t *);
int pthread_cond_init(pthread_cond_t *, const void *);
int pthread_cond_wait(pthread_cond_t *, pthread_mutex_t *);
int pthread_cond_signal(pthread_cond_t *);

/* ---- сокеты (UDP) ---- */
struct in_addr { uint32_t s_addr; };
struct sockaddr { sa_family_t sa_family; char sa_data[14]; };
struct sockaddr_in {
    sa_family_t    sin_family;
    uint16_t       sin_port;
    struct in_addr sin_addr;
    unsigned char  sin_zero[8];
};
#define AF_INET        2
#define SOCK_DGRAM     2
#define IPPROTO_UDP    17
#define INADDR_ANY     ((uint32_t)0x00000000)
#define INADDR_LOOPBACK ((uint32_t)0x7f000001)
int socket(int domain, int type, int protocol);
int bind(int fd, const struct sockaddr *addr, socklen_t len);
ssize_t sendto(int fd, const void *buf, size_t n, int flags,
               const struct sockaddr *to, socklen_t tolen);
ssize_t recvfrom(int fd, void *buf, size_t n, int flags,
                 struct sockaddr *from, socklen_t *fromlen);
int close(int fd);

#endif /* FW_ABI_H */
