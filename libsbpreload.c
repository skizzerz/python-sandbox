// Sometimes, it is easier to override/virtualize things at a higher level
// than the sandbox syscall interface. Hence, this file is introduced.

// _FORTIFY_SOURCE does funky stuff with open() and other functions, which is
// not desirable when we're trying to simply override them with custom stuff.
#undef _FORTIFY_SOURCE
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <pwd.h>
#include <string.h>
#include <dlfcn.h>

#define INIT_NEXT(name, type, ...) static type (*next)(__VA_ARGS__) = NULL;\
	if (!next) next = dlsym(RTLD_NEXT, name)

#define PUNT_NEXT(...) if (!sb_enabled) return next(__VA_ARGS__)

static volatile int sb_enabled = 0;
void enable_sandbox() {
	sb_enabled = 1;
}

// isatty calls tcgetattr which issues an ioctl syscall
// and I *really* don't want to emulate ioctl, it's a gnarly mess
// ttyname/ttyname_r included for posterity
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

int ttyname_r(int fd, char *buf, size_t buflen)
{
	return ENOTTY;
}

// While getuid and friends are straightforward to emulate via syscalls,
// getpwnam is not because it does funky things like opening sockets
// As such, all of the related functions are emulated here.
#define SB_UID 1000
#define SB_GID 1000
static struct passwd sb_pwd = {"sandbox", "*", SB_UID, SB_GID, "", "/tmp", "/bin/false"};
static struct passwd rt_pwd = {"root", "*", 0, 0, "", "/root", "/bin/sh"};
#define SB_PWD_BUF "sandbox\x00*\x00\x00/tmp\x00/bin/false\x00"
#define SB_PWD_BUFLEN 27
#define SB_PWD_OFF 8
#define SB_GECOS_OFF 10
#define SB_DIR_OFF 11
#define SB_SHELL_OFF 16
#define RT_PWD_BUF "root\x00*\x00\x00/root\x00/bin/sh\x00"
#define RT_PWD_BUFLEN 22
#define RT_PWD_OFF 5
#define RT_GECOS_OFF 7
#define RT_DIR_OFF 8
#define RT_SHELL_OFF 14

uid_t getuid()
{
	INIT_NEXT("getuid", uid_t, void);
	PUNT_NEXT();

	return SB_UID;
}

uid_t geteuid()
{
	INIT_NEXT("geteuid", uid_t, void);
	PUNT_NEXT();

	return SB_UID;
}

gid_t getgid()
{
	INIT_NEXT("getgid", gid_t, void);
	PUNT_NEXT();

	return SB_GID;
}

gid_t getegid()
{
	INIT_NEXT("getegid", gid_t, void);
	PUNT_NEXT();

	return SB_GID;
}

struct passwd *getpwnam(const char *name)
{
	INIT_NEXT("getpwnam", struct passwd *, const char *);
	PUNT_NEXT(name);

	if (!strcmp(name, "sandbox")) {
		return &sb_pwd;
	} else if (!strcmp(name, "root")) {
		return &rt_pwd;
	}

	return NULL;
}

struct passwd *getpwuid(uid_t uid)
{
	INIT_NEXT("getpwuid", struct passwd *, uid_t);
	PUNT_NEXT(uid);

	if (uid == SB_UID) {
		return &sb_pwd;
	} else if (uid == 0) {
		return &rt_pwd;
	}

	return NULL;
}

int getpwnam_r(const char *name, struct passwd *pwd, char *buf, size_t buflen, struct passwd **result)
{
	INIT_NEXT("getpwnam_r", int, const char *, struct passwd *, char *, size_t, struct passwd **);
	PUNT_NEXT(name, pwd, buf, buflen, result);

	if (!strcmp(name, "sandbox")) {
		if (buflen < SB_PWD_BUFLEN) {
			*result = NULL;
			return ERANGE;
		}

		memcpy(buf, SB_PWD_BUF, SB_PWD_BUFLEN);
		pwd->pw_name = buf;
		pwd->pw_passwd = buf + SB_PWD_OFF;
		pwd->pw_uid = SB_UID;
		pwd->pw_gid = SB_GID;
		pwd->pw_gecos = buf + SB_GECOS_OFF;
		pwd->pw_dir = buf + SB_DIR_OFF;
		pwd->pw_shell = buf + SB_SHELL_OFF;
		*result = pwd;
		return 0;
	} else if (!strcmp(name, "root")) {
		if (buflen < RT_PWD_BUFLEN) {
			*result = NULL;
			return ERANGE;
		}

		memcpy(buf, RT_PWD_BUF, RT_PWD_BUFLEN);
		pwd->pw_name = buf;
		pwd->pw_passwd = buf + RT_PWD_OFF;
		pwd->pw_uid = 0;
		pwd->pw_gid = 0;
		pwd->pw_gecos = buf + RT_GECOS_OFF;
		pwd->pw_dir = buf + RT_DIR_OFF;
		pwd->pw_shell = buf + RT_SHELL_OFF;
		*result = pwd;
		return 0;
	}

	*result = NULL;
	return 0;
}

int getpwuid_r(uid_t uid, struct passwd *pwd, char *buf, size_t buflen, struct passwd **result)
{
	INIT_NEXT("getpwuid_r", int, uid_t, struct passwd *, char *, size_t, struct passwd **);
	PUNT_NEXT(uid, pwd, buf, buflen, result);

	if (uid == SB_UID) {
		if (buflen < SB_PWD_BUFLEN) {
			*result = NULL;
			return ERANGE;
		}

		memcpy(buf, SB_PWD_BUF, SB_PWD_BUFLEN);
		pwd->pw_name = buf;
		pwd->pw_passwd = buf + SB_PWD_OFF;
		pwd->pw_uid = SB_UID;
		pwd->pw_gid = SB_GID;
		pwd->pw_gecos = buf + SB_GECOS_OFF;
		pwd->pw_dir = buf + SB_DIR_OFF;
		pwd->pw_shell = buf + SB_SHELL_OFF;
		*result = pwd;
		return 0;
	} else if (uid == 0) {
		if (buflen < RT_PWD_BUFLEN) {
			*result = NULL;
			return ERANGE;
		}

		memcpy(buf, RT_PWD_BUF, RT_PWD_BUFLEN);
		pwd->pw_name = buf;
		pwd->pw_passwd = buf + RT_PWD_OFF;
		pwd->pw_uid = 0;
		pwd->pw_gid = 0;
		pwd->pw_gecos = buf + RT_GECOS_OFF;
		pwd->pw_dir = buf + RT_DIR_OFF;
		pwd->pw_shell = buf + RT_SHELL_OFF;
		*result = pwd;
		return 0;
	}

	*result = NULL;
	return 0;
}
