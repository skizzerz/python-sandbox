#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>
#include <json/json.h>
#include <string.h>
#include <errno.h>
#include <sys/cdefs.h>

#include "sbcontext.h"
#include "sblibc.h"

// pretend that we are not a tty even if we might be
// (ensures a consistent environment between testing and production)
int isatty(int fd)
{
	errno = EINVAL;
	return 0;
}

char *ttyname(int fd)
{
	errno = ENOTTY;
	return NULL;
}

int ttyname_r(int fd, char *buf, size_t len)
{
	return ENOTTY;
}

int __open(const char *pathname, int flags, ...)
{
	if (!strcmp("/dev/urandom", pathname)) {
		// parent passes us a pipe to /dev/urandom which we allow reading from unsandboxed
		return URANDOM;
	}

	va_list vargs;
	int numargs = 2;
	json_object *arg1 = json_object_new_string(pathname);
	json_object *arg2 = json_object_new_int(flags);
	json_object *arg3 = NULL;

	if (flags & (O_CREAT | O_TMPFILE)) {
		numargs = 3;
		va_start(vargs, flags);
		arg3 = json_object_new_int(va_arg(vargs, int));
		va_end(vargs);
	}

	return trampoline(NULL, "open", numargs, arg1, arg2, arg3);
}

int open(const char *pathname, int flags, ...) __attribute__ ((alias ("__open")));

int __open64(const char *pathname, int flags, ...)
{
	int mode;
	va_list vargs;

	va_start(vargs, flags);
	mode = va_arg(vargs, int);
	va_end(vargs);

	return __open(pathname, flags | O_LARGEFILE, mode);
}

int open64(const char *pathname, int flags, ...) __attribute__ ((alias ("__open64")));

int __fcntl(int fd, int cmd, ...)
{
	va_list vargs;
	int numargs = 2;
	char arg3type = '\0';
	json_object *arg1 = json_object_new_int(fd);
	json_object *arg2 = json_object_new_int(cmd);
	json_object *arg3 = NULL;

	switch (cmd) {
	case F_DUPFD:
	case F_DUPFD_CLOEXEC:
	case F_SETFD:
	case F_SETFL:
	case F_SETOWN:
	case F_SETSIG:
	case F_SETLEASE:
	case F_NOTIFY:
	case F_SETPIPE_SZ:
#ifdef F_ADD_SEALS
	case F_ADD_SEALS:
#endif
		numargs = 3;
		arg3type = 'i'; // int
		break;
	case F_SETLK:
	case F_SETLKW:
	case F_GETLK:
#ifdef F_OFD_SETLK
	case F_OFD_SETLK:
	case F_OFD_SETLKW:
	case F_OFD_GETLK:
#endif		
		numargs = 3;
		arg3type = 'l'; // struct flock *
		break;
	case F_GETOWN_EX:
	case F_SETOWN_EX:
		numargs = 3;
		arg3type = 'o'; // struct f_owner_ex *
		break;
	}

	if (numargs == 3) {
		va_start(vargs, cmd);
		switch (arg3type) {
		case 'i':
			arg3 = json_object_new_int(va_arg(vargs, int));
			break;
		case 'l':
		{
			struct flock *fl = va_arg(vargs, struct flock *);
			arg3 = json_object_new_object();

			json_object *l_type = json_object_new_int(fl->l_type);
			json_object *l_whence = json_object_new_int(fl->l_whence);
			json_object *l_start = json_object_new_int64(fl->l_start);
			json_object *l_len = json_object_new_int64(fl->l_len);
			json_object *l_pid = json_object_new_int(fl->l_pid);

			json_object_object_add(arg3, "l_type", l_type);
			json_object_object_add(arg3, "l_whence", l_whence);
			json_object_object_add(arg3, "l_start", l_start);
			json_object_object_add(arg3, "l_len", l_len);
			json_object_object_add(arg3, "l_pid", l_pid);
			break;
		}
		case 'o':
		{
			struct f_owner_ex *fo = va_arg(vargs, struct f_owner_ex *);
			arg3 = json_object_new_object();

			json_object *type = json_object_new_int(fo->type);
			json_object *pid = json_object_new_int(fo->pid);

			json_object_object_add(arg3, "type", type);
			json_object_object_add(arg3, "pid", pid);
			break;
		}
		}
		va_end(vargs);
	}

	return trampoline(NULL, "fcntl", numargs, arg1, arg2, arg3);
}

int fcntl(int fd, int cmd, ...) __attribute__ ((alias ("__fcntl")));

int close(int fd)
{
	if (fd == URANDOM) {
		return 0;
	}

	json_object *arg1 = json_object_new_int(fd);

	return trampoline(NULL, "close", 1, arg1);
}
