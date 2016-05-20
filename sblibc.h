#include <stdint.h>
#include <stdarg.h>

// syscall wrappers
#define SYS(name) intptr_t sb_##name(va_list args)
#define ESYS(name) extern SYS(name)
#define ASYS(name, nargs) { #name, nargs, sb_##name }

ESYS(open);
ESYS(fcntl);
ESYS(close);
ESYS(read);
ESYS(stat);
ESYS(fstat);
ESYS(lstat);
ESYS(readlink);
ESYS(openat);
ESYS(getdents);
ESYS(lseek);
ESYS(dup);
ESYS(mmap);

struct sys_arg_map {
	const char *sys;
	int nargs;
	intptr_t (*func)(va_list);
};

extern const struct sys_arg_map arg_map[];
extern const int nsyscalls;
extern const char *syscalls[];
