// syscall wrappers
#define SYS(name) int sb_##name(va_list args)
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

struct sys_arg_map {
	const char *sys;
	int nargs;
	int (*func)(va_list);
};

extern const struct sys_arg_map arg_map[];
extern const int nsyscalls;
extern const char *syscalls[];
