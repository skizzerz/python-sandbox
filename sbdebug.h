// header file to aid in debugging
#ifdef SB_DEBUG

struct sys_arg_map {
	const char *sys;
	int nargs;
	const char *args;
};

static struct sys_arg_map arg_map[] = {
	{ "open", 3, "soo" },
	{ "fcntl", 3, "ddd" },
	{ "ioctl", 3, "duX" },
	{ "stat", 2, "sX" },
	{ "fstat", 2, "dX" },
	{ "mmap", 6, "XUoodU" },
	{ "brk", 1, "X" },
	{ "sbrk", 1, "X" },
	{ "readlink", 3, "sXU" },
	{ NULL, 0, NULL }
};

#endif /* SB_DEBUG */
