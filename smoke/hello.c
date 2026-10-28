/* Дымовой тест: без системных заголовков. puts() резолвится из libc.so.6
   прошивки в рантайме через /lib/ld-linux.so.3. Доказывает кодогенерацию
   clang-18 под BE ARM + линковку с .so прошивки + путь запуска
   (qemu-armeb / гость hi1380). */
extern int puts(const char *);

/* Самопроверка порядка байт: в BE8 старший байт 0x01020304 лежит первым. */
static const unsigned long magic = 0x01020304UL;

int main(void)
{
    const unsigned char *b = (const unsigned char *)&magic;
    puts("hello from mini-DOPRA, big-endian ARM, firmware glibc 2.29");
    if (b[0] == 0x01 && b[3] == 0x04)
        puts("endianness: BIG-ENDIAN confirmed (byte[0]=0x01)");
    else
        puts("endianness: little-endian (unexpected)");
    return 0;
}
