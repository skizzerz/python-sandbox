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

#include "sbcontext.h"
#include "sblibc.h"

static int urandom_fd = -1;

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

int open(const char *pathname, int flags, ...) __attribute__ ((weak, alias ("__open")));
int __open_alias(const char *pathname, int flags, ...) __attribute__ ((weak, alias ("__open")));
int __open_2(const char *pathname, int flags, ...) __attribute__ ((weak, alias ("__open")));

int __open64(const char *pathname, int flags, ...)
{
	int mode;
	va_list vargs;

	va_start(vargs, flags);
	mode = va_arg(vargs, int);
	va_end(vargs);

	return __open(pathname, flags | O_LARGEFILE, mode);
}

int open64(const char *pathname, int flags, ...) __attribute__ ((weak, alias ("__open64")));
int __open64_alias(const char *pathname, int flags, ...) __attribute__ ((weak, alias ("__open64")));
int __open64_2(const char *pathname, int flags, ...) __attribute__ ((weak, alias ("__open64")));

int __fcntl(int fd, int cmd, ...)
{
	va_list vargs;
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

	if (fd >= 0 && fd <= 4) {
		int (*original_fcntl)(int, int, ...);
		original_fcntl = dlsym(RTLD_NEXT, "fcntl");

		if (numargs == 3) {
			void *arg = NULL;
			va_start(vargs, cmd);
			arg = va_arg(vargs, void *);
			va_end(vargs);
			return (*original_fcntl)(fd, cmd, arg);
		}

		return (*original_fcntl)(fd, cmd);
	}

	json_object *arg1 = json_object_new_int(fd);
	json_object *arg2 = json_object_new_int(cmd);
	json_object *arg3 = NULL;

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

int fcntl(int fd, int cmd, ...) __attribute__ ((weak, alias ("__fcntl")));

int close(int fd)
{
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

ssize_t read(int fd, void *buf, size_t count)
{
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

int __xstat(int ver, const char *path, struct stat *buf)
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
	if (ver != _STAT_VER) {
		errno = EINVAL;
		return -1;
	}

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

int __xstat64(int ver, const char *path, struct stat *buf) __attribute__ ((weak, alias ("__xstat")));

int __fxstat(int ver, int fd, struct stat *buf)
{
	if (ver != _STAT_VER) {
		errno = EINVAL;
		return -1;
	}

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

int __fxstat64(int ver, int fd, struct stat *buf) __attribute__ ((weak, alias ("__fxstat")));

ssize_t readlink(const char *path, char *buf, size_t bufsiz)
{
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
