typedef int (*libc_start_main_fn)(int (*)(int, char **, char **), int, char **,
                                 void (*)(void), void (*)(void), void (*)(void), void *);

int __libc_start_main(int (*main_fn)(int, char **, char **), int argc, char **argv,
                     void (*init)(void), void (*fini)(void), void (*rtld_fini)(void), void *stack_end)
{
    TRACE("__libc_start_main entry");
    report_fd = dup(STDOUT_FILENO);
    if(report_fd < 0) _exit(2);
    if(dup2(STDERR_FILENO, STDOUT_FILENO) < 0) _exit(2);
    original_main_address = (void *)main_fn;
    TRACE("resolve_libc_start_main before");
    libc_start_main_fn start = (libc_start_main_fn)dlsym(RTLD_NEXT, "__libc_start_main");
    if(!start) _exit(2);
    TRACE("resolve_libc_start_main after");
    /* Defer OpenSSL and stdio until libc has initialized and calls our replacement main. */
    return start(probe_main, argc, argv, init, fini, rtld_fini, stack_end);
}
