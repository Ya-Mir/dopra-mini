/* Минимальная замена crt1 для динамически слинкованного ARM (BE8) Linux-бинаря,
   линкуемого с glibc 2.29 из прошивки (готового crt1.o нет). При входе в процесс
   динамический загрузчик кладёт rtld_fini в r0, argc по [sp], argv по sp+4.
   Вызываем:
     __libc_start_main(main, argc, argv, init=0, fini=0, rtld_fini, stack_end)
   что выполняет полную инициализацию libc (TLS/malloc/locale), чтобы работали
   pthreads и stdio. */
    .text
    .globl  _start
    .type   _start, %function
_start:
    mov     fp, #0              @ самый внешний кадр
    mov     lr, #0
    mov     r4, r0              @ r4 = rtld_fini (от ld.so)
    ldr     r1, [sp]            @ r1 = argc
    add     r2, sp, #4          @ r2 = argv
    mov     r5, sp              @ r5 = stack_end (sp при входе)
    mov     r3, #0              @ r3 = init = NULL
    mov     r6, #0              @ fini / паддинг = NULL
    push    {r6}                @ [orig-4]  паддинг (держим sp выровненным по 8)
    push    {r5}                @ [orig-8]  stack_end  -> [sp+8] при вызове
    push    {r4}                @ [orig-12] rtld_fini  -> [sp+4]
    push    {r6}                @ [orig-16] fini       -> [sp]
    ldr     r0, =main           @ r0 = main
    bl      __libc_start_main
1:  b       1b                  @ сюда не доходим
    .size   _start, . - _start
