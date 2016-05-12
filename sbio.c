// python is compiled with this sometimes, but it wreaks havoc here, so undef it
#undef _FORTIFY_SOURCE
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
#include <signal.h>
#include <dirent.h>

#include "sbcontext.h"
#include "sblibc.h"

static int urandom_fd = -1;

SYS(open)
{
	const char *pathname = va_arg(args, const char *);
	int flags = va_arg(args, int);
	int mode = va_arg(args, int);

	if (!strcmp("/dev/urandom", pathname) && flags == O_RDONLY) {
		if (urandom_fd > -1) {
			return urandom_fd;
		}

		// json-c init tries to open /dev/urandom with O_RDONLY
		// as such, we need to hardcode the json string request since
		// we cannot rely on json-c to do it for us
		int ret = writejson("{\"name\":\"open\",\"args\":[\"/dev/urandom\",0],\"raw\":true}");

		if (ret < 0) {
			debug_error("writejson failed with errno %d\n", errno);
			exit(errno);
		}

		ret = readraw("%d %d", &urandom_fd, &errno);
		if (ret < 0) {
			debug_error("readraw failed with errno %d\n", errno);
			exit(errno);
		}

		return urandom_fd;
	}

	int numargs = 2;
	json_object *arg1 = json_object_new_string(pathname);
	json_object *arg2 = json_object_new_int(flags);
	json_object *arg3 = NULL;

	if (flags & (O_CREAT | O_TMPFILE)) {
		arg3 = json_object_new_int(mode);
	}

	return trampoline(NULL, "open", numargs, arg1, arg2, arg3);
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

	return trampoline(NULL, "fcntl", numargs, arg1, arg2, arg3);
}

SYS(close)
{
	int fd = va_arg(args, int);

	if (fd == urandom_fd) {
		char buf[64] = {0};
		int ret = 0;
		int code = 0;

		urandom_fd = -1;

		snprintf(buf, 64, "{\"name\":\"close\",\"args\":[%d],\"raw\":true}", fd);
		ret = writejson(buf);
		if (ret < 0) {
			debug_error("writejson failed with errno %d\n", errno);
			exit(errno);
		}

		ret = readraw("%d %d", &code, &errno);
		if (ret < 0) {
			debug_error("readraw failed with errno %d\n", errno);
			exit(errno);
		}

		return code;
	}

	json_object *arg1 = json_object_new_int(fd);

	return trampoline(NULL, "close", 1, arg1);
}

SYS(read)
{
	int fd = va_arg(args, int);
	void *buf = va_arg(args, void *);
	size_t count = va_arg(args, size_t);

	int code = 0;
	int ret = 0;

	if (fd == urandom_fd) {
		// still in json init, so hardcode a raw read
		char *buf2 = (char *)malloc(65536);

		snprintf(buf2, 65536, "{\"name\":\"read\",\"args\":[%d,%u],\"raw\":true}", fd, (unsigned int)count);
		ret = writejson(buf2);
		if (ret < 0) {
			debug_error("writejson failed with errno %d\n", errno);
			exit(errno);
		}

		ret = readraw("%d %d %65535s", &code, &errno, buf2);
		if (ret < 0) {
			debug_error("readraw failed with errno %d\n", errno);
			exit(errno);
		}

		if (code <= 0) {
			free(buf2);
			return code;
		}

		size_t count2 = count;
		ret = base64decode(buf2, strlen(buf2), buf, &count2);
		if (ret != 0) {
			debug_error("base64decode failed\n");
			exit(1);
		}

		free(buf2);
		return code;
	}

	json_object *arg1 = json_object_new_int(fd);
	json_object *arg2 = json_object_new_int64(count);
	json_object *out = NULL;
	json_object *data = NULL;

	code = trampoline(&out, "read", 2, arg1, arg2);
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
	int code = 0;

	if (!strcmp("/dev/urandom", path)) {
		ret = writejson("{\"name\":\"stat\",\"args\":[\"/dev/urandom\"],\"raw\":true}");
		if (ret < 0) {
			debug_error("writejson failed with errno %d\n", errno);
			exit(errno);
		}

		ret = readraw("%d %d %u %u %u %u %u %u %u %u %u %u %u %u %u", &code, &errno,
				&buf->st_dev, &buf->st_ino, &buf->st_mode, &buf->st_nlink,
				&buf->st_uid, &buf->st_gid, &buf->st_rdev, &buf->st_size,
				&buf->st_atim.tv_sec, &buf->st_mtim.tv_sec, &buf->st_ctim.tv_sec,
				&buf->st_blksize, &buf->st_blocks);
		if (ret < 0) {
			debug_error("readraw failed with errno %d\n", errno);
			exit(errno);
		}

		return code;
	}

	json_object *arg1 = json_object_new_string(path);
	json_object *out = NULL;
	json_object *data = NULL;
	json_object *fld = NULL;

	ret = trampoline(&out, "stat", 1, arg1);
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

	ret = trampoline(&out, "fstat", 1, arg1);
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

	ret = trampoline(&out, "readlink", 1, arg1);
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

	return trampoline(NULL, "openat", numargs, arg1, arg2, arg3, arg4);
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

	ret = trampoline(&out, "getdents", 3, arg1, arg2, arg3);
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
