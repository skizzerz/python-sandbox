// python is compiled with this sometimes, but it wreaks havoc here, so undef it
#undef _FORTIFY_SOURCE
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <dlfcn.h>
#include <unistd.h>
#include <asm/unistd.h>
#include <fcntl.h>
#include <json/json.h>
#include <string.h>
#include <errno.h>
#include <sys/cdefs.h>
#include <sys/mman.h>
#include <sys/vfs.h>
#include <sys/uio.h>
#include <signal.h>
#include <dirent.h>
#include <poll.h>

#include "sbcontext.h"
#include "sblibc.h"

/* when !is_child, all args are void * into a static buffer of size 65537.
 * The first argument is the start of that buffer; we overwrite
 * that buffer with output data, prefixed by the length of said data.
 */

static const int16_t ns_sys = NS_SYS;
static uint16_t callnum;
static uint16_t arglen;
static struct iovec request[9] = {
	{ (void *)&ns_sys, 2 },
	{ &callnum, 2 },
	{ &arglen, 2 }
};

static int syscode;
static int syserrno;
static struct iovec response[8] = {
	{ &syscode, sizeof(int) },
	{ &syserrno, sizeof(int) }
};

SYS(open)
{
	const char *pathname;
	int flags, mode, ret;
	int *len;

	if (is_child) {
		pathname = va_arg(args, const char *);
		flags = va_arg(args, int);
		mode = va_arg(args, int);

		request[3].iov_base = (void *)pathname;
		request[3].iov_len = strlen(pathname);
		request[4].iov_base = &flags;
		request[4].iov_len = sizeof(int);
		request[5].iov_base = &mode;
		request[5].iov_len = sizeof(int);
		callnum = __NR_open;
		arglen = request[3].iov_len + request[4].iov_len + request[5].iov_len;

		ret = writev(RPCSOCK, request, 6);
		if (ret < 0) {
			debug_error("writev failed: %s", strerror(errno));
			exit(EIO);
		}

		ret = readv(RPCSOCK, response, 2);
		if (ret < 0) {
			debug_error("read failed: %s", strerror(errno));
			exit(EIO);
		}

		errno = syserrno;
	} else {
		len = va_arg(args, int *);

		pathname = (const char *)len;
		flags = *va_arg(args, int *);
		mode = *va_arg(args, int *);

		syscode = open_node(pathname, flags, mode);
		*len = 0;
	}

	return syscode;
}

SYS(fcntl)
{
	int fd = va_arg(args, int);
	int cmd = va_arg(args, int);
	int numargs = 2;
	char arg3type = '\0';

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

	json_object *arg1 = json_object_new_int(fd);
	json_object *arg2 = json_object_new_int(cmd);
	json_object *arg3 = NULL;

	if (numargs == 3) {
		switch (arg3type) {
		case 'i':
			arg3 = json_object_new_int(va_arg(args, int));
			break;
		case 'l':
		{
			struct flock *fl = va_arg(args, struct flock *);
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
			struct f_owner_ex *fo = va_arg(args, struct f_owner_ex *);
			arg3 = json_object_new_object();

			json_object *type = json_object_new_int(fo->type);
			json_object *pid = json_object_new_int(fo->pid);

			json_object_object_add(arg3, "type", type);
			json_object_object_add(arg3, "pid", pid);
			break;
		}
		}
	}

	return trampoline(NULL, NS_SYS, "fcntl", numargs, arg1, arg2, arg3);
}

SYS(close)
{
	int fd = va_arg(args, int);
	json_object *arg1 = json_object_new_int(fd);

	return trampoline(NULL, NS_SYS, "close", 1, arg1);
}

SYS(read)
{
	int fd = va_arg(args, int);
	void *buf = va_arg(args, void *);
	size_t count = va_arg(args, size_t);

	int code = 0;
	int ret = 0;

	json_object *arg1 = json_object_new_int(fd);
	json_object *arg2 = json_object_new_int64(count);
	json_object *out = NULL;
	json_object *data = NULL;

	code = trampoline(&out, NS_SYS, "read", 2, arg1, arg2);
	if (code <= 0) {
		json_object_put(out);
		return code;
	}

	if (!json_object_object_get_ex(out, "data", &data)) {
		debug_error("data expected\n");
		exit(EPROTO);
	}

	if (!json_object_is_type(data, json_type_string)) {
		debug_error("data is not a string\n");
		exit(EPROTO);
	}

	const char *str = json_object_get_string(data);
	ret = json_object_get_string_len(data);
	if (ret > 0 && (size_t)ret > count) {
		debug_error("data string larger than buffer\n");
		exit(EPROTO);
	}
	if (ret != code) {
		debug_error("data length mismatch\n");
		exit(EPROTO);
	}

	memcpy(buf, str, ret);
	json_object_put(out);
	return code;
}

SYS(stat)
{
#define ST_GET(type, field) if (!json_object_object_get_ex(data, #field, &fld)) {\
								debug_error("data." #field " expected\n");\
								exit(EPROTO);\
							}\
							if (!json_object_is_type(fld, json_type_int)) {\
								debug_error("data." #field " is not an int\n");\
								exit(EPROTO);\
							}\
							buf->field = (type)json_object_get_int64(fld)
	const char *path = va_arg(args, const char *);
	struct stat *buf = va_arg(args, struct stat *);

	int ret = 0;

	json_object *arg1 = json_object_new_string(path);
	json_object *out = NULL;
	json_object *data = NULL;
	json_object *fld = NULL;

	ret = trampoline(&out, NS_SYS, "stat", 1, arg1);
	if (ret != 0) {
		json_object_put(out);
		return ret;
	}

	if (!json_object_object_get_ex(out, "data", &data)) {
		debug_error("data expected\n");
		exit(EPROTO);
	}

	if (!json_object_is_type(data, json_type_object)) {
		debug_error("data is not an object\n");
		exit(EPROTO);
	}

	ST_GET(dev_t, st_dev);
	ST_GET(ino_t, st_ino);
	ST_GET(mode_t, st_mode);
	ST_GET(nlink_t, st_nlink);
	ST_GET(uid_t, st_uid);
	ST_GET(gid_t, st_gid);
	ST_GET(dev_t, st_rdev);
	ST_GET(off_t, st_size);
	ST_GET(blksize_t, st_blksize);
	ST_GET(blkcnt_t, st_blocks);

	if (!json_object_object_get_ex(data, "st_atime", &fld)) {
		debug_error("data.st_atime expected\n");
		exit(EPROTO);
	}

	if (!json_object_is_type(fld, json_type_int)) {
		debug_error("data.st_atime is not an int\n");
		exit(EPROTO);
	}

	buf->st_atim.tv_sec = (time_t)json_object_get_int64(fld);

	if (!json_object_object_get_ex(data, "st_mtime", &fld)) {
		debug_error("data.st_mtime expected\n");
		exit(EPROTO);
	}

	if (!json_object_is_type(fld, json_type_int)) {
		debug_error("data.st_mtime is not an int\n");
		exit(EPROTO);
	}

	buf->st_mtim.tv_sec = (time_t)json_object_get_int64(fld);

	if (!json_object_object_get_ex(data, "st_ctime", &fld)) {
		debug_error("data.st_ctime expected\n");
		exit(EPROTO);
	}

	if (!json_object_is_type(fld, json_type_int)) {
		debug_error("data.st_ctime is not an int\n");
		exit(EPROTO);
	}

	buf->st_ctim.tv_sec = (time_t)json_object_get_int64(fld);

	json_object_put(out);
	return ret;
}

SYS(lstat)
{
	const char *path = va_arg(args, const char *);
	struct stat *buf = va_arg(args, struct stat *);

	int ret = 0;
	json_object *arg1 = json_object_new_string(path);
	json_object *out = NULL;
	json_object *data = NULL;
	json_object *fld = NULL;

	ret = trampoline(&out, NS_SYS, "lstat", 1, arg1);
	if (ret != 0) {
		json_object_put(out);
		return ret;
	}

	if (!json_object_object_get_ex(out, "data", &data)) {
		debug_error("data expected\n");
		exit(EPROTO);
	}

	if (!json_object_is_type(data, json_type_object)) {
		debug_error("data is not an object\n");
		exit(EPROTO);
	}

	ST_GET(dev_t, st_dev);
	ST_GET(ino_t, st_ino);
	ST_GET(mode_t, st_mode);
	ST_GET(nlink_t, st_nlink);
	ST_GET(uid_t, st_uid);
	ST_GET(gid_t, st_gid);
	ST_GET(dev_t, st_rdev);
	ST_GET(off_t, st_size);
	ST_GET(blksize_t, st_blksize);
	ST_GET(blkcnt_t, st_blocks);

	if (!json_object_object_get_ex(data, "st_atime", &fld)) {
		debug_error("data.st_atime expected\n");
		exit(EPROTO);
	}

	if (!json_object_is_type(fld, json_type_int)) {
		debug_error("data.st_atime is not an int\n");
		exit(EPROTO);
	}

	buf->st_atim.tv_sec = (time_t)json_object_get_int64(fld);

	if (!json_object_object_get_ex(data, "st_mtime", &fld)) {
		debug_error("data.st_mtime expected\n");
		exit(EPROTO);
	}

	if (!json_object_is_type(fld, json_type_int)) {
		debug_error("data.st_mtime is not an int\n");
		exit(EPROTO);
	}

	buf->st_mtim.tv_sec = (time_t)json_object_get_int64(fld);

	if (!json_object_object_get_ex(data, "st_ctime", &fld)) {
		debug_error("data.st_ctime expected\n");
		exit(EPROTO);
	}

	if (!json_object_is_type(fld, json_type_int)) {
		debug_error("data.st_ctime is not an int\n");
		exit(EPROTO);
	}

	buf->st_ctim.tv_sec = (time_t)json_object_get_int64(fld);

	json_object_put(out);
	return ret;
}

SYS(fstat)
{
	int fd = va_arg(args, int);
	struct stat *buf = va_arg(args, struct stat *);

	int ret = 0;
	json_object *arg1 = json_object_new_int(fd);
	json_object *out = NULL;
	json_object *data = NULL;
	json_object *fld = NULL;

	ret = trampoline(&out, NS_SYS, "fstat", 1, arg1);
	if (ret != 0) {
		json_object_put(out);
		return ret;
	}

	if (!json_object_object_get_ex(out, "data", &data)) {
		debug_error("data expected\n");
		exit(EPROTO);
	}

	if (!json_object_is_type(data, json_type_object)) {
		debug_error("data is not an object\n");
		exit(EPROTO);
	}

	ST_GET(dev_t, st_dev);
	ST_GET(ino_t, st_ino);
	ST_GET(mode_t, st_mode);
	ST_GET(nlink_t, st_nlink);
	ST_GET(uid_t, st_uid);
	ST_GET(gid_t, st_gid);
	ST_GET(dev_t, st_rdev);
	ST_GET(off_t, st_size);
	ST_GET(blksize_t, st_blksize);
	ST_GET(blkcnt_t, st_blocks);

	if (!json_object_object_get_ex(data, "st_atime", &fld)) {
		debug_error("data.st_atime expected\n");
		exit(EPROTO);
	}

	if (!json_object_is_type(fld, json_type_int)) {
		debug_error("data.st_atime is not an int\n");
		exit(EPROTO);
	}

	buf->st_atim.tv_sec = (time_t)json_object_get_int64(fld);

	if (!json_object_object_get_ex(data, "st_mtime", &fld)) {
		debug_error("data.st_mtime expected\n");
		exit(EPROTO);
	}

	if (!json_object_is_type(fld, json_type_int)) {
		debug_error("data.st_mtime is not an int\n");
		exit(EPROTO);
	}

	buf->st_mtim.tv_sec = (time_t)json_object_get_int64(fld);

	if (!json_object_object_get_ex(data, "st_ctime", &fld)) {
		debug_error("data.st_ctime expected\n");
		exit(EPROTO);
	}

	if (!json_object_is_type(fld, json_type_int)) {
		debug_error("data.st_ctime is not an int\n");
		exit(EPROTO);
	}

	buf->st_ctim.tv_sec = (time_t)json_object_get_int64(fld);

	json_object_put(out);
	return ret;
}

SYS(readlink)
{
	const char *path = va_arg(args, const char *);
	char *buf = va_arg(args, char *);
	size_t bufsiz = va_arg(args, size_t);

	int ret = 0, len = 0;
	json_object *arg1 = json_object_new_string(path);
	json_object *out = NULL;
	json_object *data = NULL;
	const char *str;

	ret = trampoline(&out, NS_SYS, "readlink", 1, arg1);
	if (ret <= 0) {
		json_object_put(out);
		return ret;
	}

	if (!json_object_object_get_ex(out, "data", &data)) {
		debug_error("data expected\n");
		exit(EPROTO);
	}

	if (!json_object_is_type(data, json_type_string)) {
		debug_error("data is not a string\n");
		exit(EPROTO);
	}

	str = json_object_get_string(data);
	len = json_object_get_string_len(data);
	if (ret != len) {
		debug_error("data length mismatch\n");
		exit(EPROTO);
	}

	// len is guaranteed to be positive, so converting to unsigned is safe
	if ((size_t)len > bufsiz) {
		len = bufsiz;
	}

	memcpy(buf, str, len);
	return ret;
}

SYS(openat)
{
	int dirfd = va_arg(args, int);
	const char *pathname = va_arg(args, const char *);
	int flags = va_arg(args, int);
	int mode = va_arg(args, int);

	
	int numargs = 3;
	json_object *arg1 = json_object_new_int(dirfd);
	json_object *arg2 = json_object_new_string(pathname);
	json_object *arg3 = json_object_new_int(flags);
	json_object *arg4 = NULL;

	if (flags & (O_CREAT | O_TMPFILE)) {
		arg4 = json_object_new_int(mode);
	}

	return trampoline(NULL, NS_SYS, "openat", numargs, arg1, arg2, arg3, arg4);
}

struct linux_dirent {
	unsigned long d_ino;
	unsigned long d_off;
	unsigned short d_reclen;
	char d_name[];
	/* these fields are at the end of d_name
	char pad;
	char d_type;
	*/
};

SYS(getdents)
{
	unsigned int fd = va_arg(args, unsigned int);
	char *dirp = va_arg(args, char *);
	unsigned int count = va_arg(args, unsigned int);

	// size of the struct not counting the name, we pass this along
	// so the parent side can calculate how many strings it can pass
	size_t misc = offsetof(struct linux_dirent, d_name) + 2;
	int ret, i, off = 0;
	const char *name;
	struct linux_dirent *d = NULL;

	json_object *arg1 = json_object_new_int64(fd);
	json_object *arg2 = json_object_new_int64(count);
	json_object *arg3 = json_object_new_int64(misc);
	json_object *out = NULL;
	json_object *data = NULL;
	json_object *obj = NULL;
	json_object *fld = NULL;

	ret = trampoline(&out, NS_SYS, "getdents", 3, arg1, arg2, arg3);
	if (ret <= 0) {
		json_object_put(out);
		return ret;
	}

	if (!json_object_object_get_ex(out, "data", &data)) {
		debug_error("data expected\n");
		exit(EPROTO);
	}

	if (!json_object_is_type(data, json_type_array)) {
		debug_error("data is not an array\n");
		exit(EPROTO);
	}

	int len = json_object_array_length(data);
	if (len == 0) {
		debug_error("0 length array but nonzero return value\n");
		exit(EPROTO);
	}

	for (i = 0; i < len; ++i) {
		d = (struct linux_dirent *)(dirp + off);
		obj = json_object_array_get_idx(data, i);
		if (!json_object_is_type(obj, json_type_object)) {
			debug_error("array member not an object\n");
			exit(EPROTO);
		}

		if (!json_object_object_get_ex(obj, "d_ino", &fld)) {
			debug_error("missing field d_ino\n");
			exit(EPROTO);
		}

		if (!json_object_is_type(fld, json_type_int)) {
			debug_error("d_ino is not an int\n");
			exit(EPROTO);
		}

		d->d_ino = (unsigned int)json_object_get_int(fld);

		if (!json_object_object_get_ex(obj, "d_name", &fld)) {
			debug_error("missing field d_name\n");
			exit(EPROTO);
		}

		if (!json_object_is_type(fld, json_type_string)) {
			debug_error("d_name is not a string\n");
			exit(EPROTO);
		}

		name = json_object_get_string(fld);
		d->d_reclen = misc + json_object_get_string_len(fld);
		d->d_off = off + d->d_reclen;
		if (d->d_off > count) {
			// this indicates a bug in the parent process, it
			// should not return more data than fits in our buffer
			debug_error("getdents() buffer overflow\n");
			exit(EPROTO);
		}

		// strcpy fills in the "pad" byte as well
		strcpy(d->d_name, name);

		if (!json_object_object_get_ex(obj, "d_type", &fld)) {
			debug_error("missing field d_type\n");
			exit(EPROTO);
		}

		if (!json_object_is_type(fld, json_type_int)) {
			debug_error("d_type is not an int\n");
			exit(EPROTO);
		}

		// fill in the d_type field
		*(dirp + d->d_off - 1) = (char)json_object_get_int(fld);

		off = d->d_off;
	}

	json_object_put(out);
	return d->d_off;
}

SYS(lseek)
{
	int fd = va_arg(args, int);
	off_t offset = va_arg(args, off_t);
	int whence = va_arg(args, int);

	json_object *arg1 = json_object_new_int(fd);
	json_object *arg2 = json_object_new_int64(offset);
	json_object *arg3 = json_object_new_int(whence);

	return trampoline(NULL, NS_SYS, "lseek", 3, arg1, arg2, arg3);
}

SYS(dup)
{
	int oldfd = va_arg(args, int);

	json_object *arg1 = json_object_new_int(oldfd);

	return trampoline(NULL, NS_SYS, "dup", 1, arg1);
}

SYS(statfs)
{
	const char *path = va_arg(args, const char *);
	struct statfs *buf = va_arg(args, struct statfs *);

	json_object *arg1 = json_object_new_string(path);
	json_object *out = NULL;
	json_object *data = NULL;
	json_object *fld = NULL;

	int ret = trampoline(&out, NS_SYS, "statfs", 1, arg1);
	if (ret < 0) {
		json_object_put(out);
		return ret;
	}

	if (!json_object_object_get_ex(out, "data", &data)) {
		debug_error("data expected\n");
		exit(EPROTO);
	}

	if (!json_object_is_type(data, json_type_object)) {
		debug_error("data is not an object\n");
		exit(EPROTO);
	}

	ST_GET(__SWORD_TYPE, f_type);
	ST_GET(__SWORD_TYPE, f_bsize);
	ST_GET(fsblkcnt_t, f_blocks);
	ST_GET(fsblkcnt_t, f_bfree);
	ST_GET(fsblkcnt_t, f_bavail);
	ST_GET(fsfilcnt_t, f_files);
	ST_GET(fsfilcnt_t, f_ffree);
	ST_GET(__SWORD_TYPE, f_namelen);
	ST_GET(__SWORD_TYPE, f_frsize);

	// fsid_t is a struct apparently so we need to do special stuff here
	if (!json_object_object_get_ex(data, "f_fsid", &fld)) {
		debug_error("data.f_fsid expected\n");
		exit(EPROTO);
	}

	if (!json_object_is_type(fld, json_type_int)) {
		debug_error("data.f_fsid is not an int\n");
		exit(EPROTO);
	}

	int64_t fsid = json_object_get_int64(fld);
	buf->f_fsid.__val[0] = (int32_t)(fsid >> 32);
	buf->f_fsid.__val[1] = (int32_t)fsid;

	json_object_put(out);
	return ret;
}

SYS(access)
{
	const char *pathname = va_arg(args, const char *);
	int mode = va_arg(args, int);

	json_object *arg1 = json_object_new_string(pathname);
	json_object *arg2 = json_object_new_int(mode);

	return trampoline(NULL, NS_SYS, "access", 2, arg1, arg2);
}

SYS(poll)
{
	struct pollfd *fds = va_arg(args, struct pollfd *);
	nfds_t nfds = va_arg(args, nfds_t);
	int timeout = va_arg(args, int);

	json_object *arg1 = json_object_new_array();
	json_object *arg2 = json_object_new_int(timeout);
	json_object *obj = NULL;
	json_object *out = NULL;
	json_object *data = NULL;

	size_t i = 0;
	int ret;

	for (i = 0; i < nfds; ++i) {
		obj = json_object_new_object();
		json_object_object_add(obj, "fd", json_object_new_int(fds[i].fd));
		json_object_object_add(obj, "events", json_object_new_int(fds[i].events));
		json_object_array_add(arg1, obj);
	}

	ret = trampoline(&out, NS_SYS, "poll", 2, arg1, arg2);
	if (ret <= 0) {
		json_object_put(out);
		return ret;
	}

	if (!json_object_object_get_ex(out, "data", &data)) {
		debug_error("data expected\n");
		exit(EPROTO);
	}

	if (!json_object_is_type(data, json_type_array)) {
		debug_error("data is not an array\n");
		exit(EPROTO);
	}

	size_t len = (size_t)json_object_array_length(data);
	if (len == 0) {
		debug_error("0 length array but nonzero return value\n");
		exit(EPROTO);
	} else if (len != nfds) {
		debug_error("input and output array length mismatch\n");
		exit(EPROTO);
	}

	for (i = 0; i < nfds; ++i) {
		obj = json_object_array_get_idx(data, i);
		if (!json_object_is_type(obj, json_type_int)) {
			debug_error("array value not an int\n");
			exit(EPROTO);
		}

		fds[i].revents = json_object_get_int(obj);
	}

	json_object_put(out);
	return ret;
}

// we do not forward mmap calls as-is
// anonymous mappings are directly allowed, so this is only called when
// we are asked to mmap a file. As such, we perform an anonymous mmap,
// read in the file to it, then fix the flags on it and return that address.
// This is doable because when python asks us to mmap a file for a .so,
// it opens the file first so the fd passed to us is also open in our parent.
// Therefore, we lseek() it to the beginning, re-read() it, and return it.
SYS(mmap)
{
	void *addr = va_arg(args, void *);
	size_t length = va_arg(args, size_t);
	int prot = va_arg(args, int);
	int flags = va_arg(args, int);
	int fd = va_arg(args, int);
	off_t offset = va_arg(args, int);

	if (flags & (MAP_SHARED | MAP_GROWSDOWN | MAP_STACK)) {
		errno = EPERM;
		debug_error("mmap flags has disallowed values\n");
		return (intptr_t)MAP_FAILED;
	}

	void *mem = mmap(addr, length, PROT_READ | PROT_WRITE, flags | MAP_ANONYMOUS, -1, 0);
	if (mem == MAP_FAILED) {
		debug_error("mmap call failed with errno %d\n", errno);
		return (intptr_t)MAP_FAILED;
	}

	if (lseek(fd, offset, SEEK_SET) == -1) {
		debug_error("lseek call failed\n");
		munmap(mem, length);
		return (intptr_t)MAP_FAILED;
	}

	size_t num_read = 0;
	int ret;
	while (num_read < length) {
		ret = read(fd, mem + num_read, length - num_read);
		if (ret == 0) {
			break;
		} else if (ret < 0) {
			debug_error("read call failed\n");
			munmap(mem, length);
			return (intptr_t)MAP_FAILED;
		}

		num_read += ret;
	}

	if (mprotect(mem, length, prot) != 0) {
		debug_error("mprotect call failed\n");
		munmap(mem, length);
		return (intptr_t)MAP_FAILED;
	}

	return (intptr_t)mem;
}
