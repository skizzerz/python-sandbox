// custom libc wrapper functions to marshal calls to our parent and read replies

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <json/json.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <execinfo.h>

#include "sbcontext.h"
#include "sblibc.h"

static FILE *pipeout = NULL;
static FILE *pipein = NULL;

int dispatch(int (*func)(va_list), ...)
{
	int ret;
	va_list args;

	va_start(args, func);
	ret = (*func)(args);
	va_end(args);

	return ret;
}

// helper function to write json string to pipeout, terminated with a newline
int writejson(const char *json)
{
	int ret;

	if (pipeout == NULL) {
		pipeout = fdopen(PIPEOUT, "w");
	}

	ret = fprintf(pipeout, "%s\n", json);
	fflush(pipeout);

	if (feof(pipeout)) {
		ret = -1;
		errno = EIO;
	}

	return ret;
}

// helper function to read json string from pipein (until a newline is encountered)
// caller is responsible for freeing the generated json_object
int readjson(struct json_object **out)
{
	// allocate a large enough buffer for our json data
	// (if the json is longer than this, we'll terminate the program)
	char *buf = (char *)malloc(65536);
	char *ret = NULL;
	int code = 0;

	if (out == NULL) {
		errno = EINVAL;
		code = -2;
		goto cleanup;
	}

	if (pipein == NULL) {
		pipein = fdopen(PIPEIN, "r");
	}

	ret = fgets(buf, 65536, pipein);
	if (ret == NULL) {
		*out = NULL;
		code = -1;
		goto cleanup;
	}

	if (feof(pipein)) {
		code = -1;
		errno = EIO;
		goto cleanup;
	}

	*out = json_tokener_parse(buf);

cleanup:
	free(buf);

	return code;
}

// read raw data from the input, useful during json library init
// this is still newline-separated (e.g. we fgets() and then run vsscanf() on that)
int readraw(const char *format, ...)
{
	// allocate a large enough buffer for our json data
	// (if the json is longer than this, we'll terminate the program)
	char *buf = (char *)malloc(65536);
	char *ret = NULL;
	int code = 0;
	va_list args;

	if (format == NULL) {
		errno = EINVAL;
		code = -2;
		goto cleanup;
	}

	if (pipein == NULL) {
		pipein = fdopen(PIPEIN, "r");
	}

	ret = fgets(buf, 65536, pipein);
	if (ret == NULL) {
		code = -1;
		goto cleanup;
	}

	va_start(args, format);
	vsscanf(buf, format, args);
	va_end(args);

	if (feof(pipein)) {
		code = -1;
		errno = EIO;
		goto cleanup;
	}

cleanup:
	free(buf);

	return code;
}

void _debug_backtrace() {
	void *buffer[32];
	int n = backtrace(buffer, 32);
	backtrace_symbols_fd(buffer, n, STDOUT_FILENO);
}

void fatal(const char *msg)
{
	debug_error("*** %s ***: sandbox terminated\n", msg);
	exit(1);
}

#ifdef JSON_C_TO_STRING_NOSLASHESCAPE
#define SB_JSON_FLAGS (JSON_C_TO_STRING PLAIN | JSON_C_TO_STRING_NOSLASHESCAPE)
#else
#define SB_JSON_FLAGS JSON_C_TO_STRING_PLAIN
#endif

// we send a JSON line containing the call in the following format:
// {"ns": int, "name": "str", "args": [any...]}
// we then get a JSON reply in the following format:
// {"code": int, "errno": int, "data": any, "base64": bool}
// (errno, data, and base64 are optional keys, code is required; base64 indicates
// the data is base64-encoded)
int trampoline(struct json_object **out, int ns, const char *fname, int numargs, ...)
{
	va_list vargs;
	int ret = 0, i;
	json_object *callinfo = json_object_new_object();
	json_object *response = NULL;
	json_object *json_code = NULL;
	json_object *json_errno = NULL;
	json_object *json_data = NULL;
	json_object *name = json_object_new_string(fname);
	json_object *args = json_object_new_array();
	json_object *namespace = json_object_new_int(ns);

	va_start(vargs, numargs);
	for (i = 0; i < numargs; ++i) {
		json_object_array_add(args, va_arg(vargs, json_object *));
	}
	va_end(vargs);

	json_object_object_add(callinfo, "ns", namespace);
	json_object_object_add(callinfo, "name", name);
	json_object_object_add(callinfo, "args", args);

	ret = writejson(json_object_to_json_string_ext(callinfo, SB_JSON_FLAGS));
	if (ret < 0) {
		debug_error("writejson failed with errno %d\n", errno);
		exit(-errno);
	}

	ret = readjson(&response);
	if (ret < 0) {
		debug_error("readjson failed with errno %d\n", errno);
		exit(-errno);
	}

	if (!json_object_is_type(response, json_type_object)) {
		debug_error("response is not json object.\n");
		exit(EPROTO);
	}

	if (!json_object_object_get_ex(response, "code", &json_code)) {
		debug_error("response does not have field code.\n");
		exit(EPROTO);
	}

	if (!json_object_is_type(json_code, json_type_int)) {
		debug_error("response code is not int.\n");
		exit(EPROTO);
	}

	ret = json_object_get_int(json_code);

	if (ret == -1 && json_object_object_get_ex(response, "errno", &json_errno)) {
		if (!json_object_is_type(json_errno, json_type_int)) {
			debug_error("response has errno but it is not int.\n");
			exit(EPROTO);
		}

		errno = json_object_get_int(json_errno);
	} else {
		errno = 0;
	}

	if (json_object_object_get_ex(response, "base64", &json_data) &&
		json_object_get_boolean(json_data) &&
		json_object_object_get_ex(response, "data", &json_data) &&
		json_object_is_type(json_data, json_type_string))
	{
		const char *b64 = json_object_get_string(json_data);
		size_t b64_len = (size_t)json_object_get_string_len(json_data) + 1;
		char *b64_buf = (char *)malloc(b64_len);
		
		if (base64decode(b64, strlen(b64), (unsigned char *)b64_buf, &b64_len)) {
			debug_error("invalid base64-encoded data.\n");
			exit(EPROTO);
		}

		json_data = json_object_new_string_len(b64_buf, b64_len);
		json_object_object_del(response, "data");
		json_object_object_add(response, "data", json_data);
		free(b64_buf);
	}

	if (out == NULL) {
		json_object_put(response);
	} else {
		*out = response;
	}

	return ret;
}

// base64 decode routine from wikibooks
// code was released into the public domain there

#define WHITESPACE 64
#define EQUALS     65
#define INVALID    66

static const unsigned char d[] = {
    66,66,66,66,66,66,66,66,66,66,64,66,66,66,66,66,66,66,66,66,66,66,66,66,66,
    66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,62,66,66,66,63,52,53,
    54,55,56,57,58,59,60,61,66,66,66,65,66,66,66, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
    10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,66,66,66,66,66,66,26,27,28,
    29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,66,66,
    66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,
    66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,
    66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,
    66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,
    66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,
    66,66,66,66,66,66
};

int base64decode(const char *in, size_t inLen, unsigned char *out, size_t *outLen)
{ 
    const char *end = in + inLen;
    char iter = 0;
    size_t buf = 0, len = 0;
    
    while (in < end) {
        unsigned char c = d[(unsigned char)*in++];
        
        switch (c) {
        case WHITESPACE: continue;   /* skip whitespace */
        case INVALID:    return 1;   /* invalid input, return error */
        case EQUALS:                 /* pad character, end of data */
            in = end;
            continue;
        default:
            buf = buf << 6 | c;
            iter++; // increment the number of iteration
            /* If the buffer is full, split it into bytes */
            if (iter == 4) {
                if ((len += 3) > *outLen) return 1; /* buffer overflow */
                *(out++) = (buf >> 16) & 255;
                *(out++) = (buf >> 8) & 255;
                *(out++) = buf & 255;
                buf = 0; iter = 0;

            }   
        }
    }
   
    if (iter == 3) {
        if ((len += 2) > *outLen) return 1; /* buffer overflow */
        *(out++) = (buf >> 10) & 255;
        *(out++) = (buf >> 2) & 255;
    }
    else if (iter == 2) {
        if (++len > *outLen) return 1; /* buffer overflow */
        *(out++) = (buf >> 4) & 255;
    }

    *outLen = len; /* modify to reflect the actual output size */
    return 0;
}

const struct sys_arg_map arg_map[] = {
	ASYS(open, 3),
	ASYS(fcntl, 3),
	ASYS(close, 1),
	ASYS(read, 3),
	ASYS(stat, 2),
	ASYS(fstat, 2),
	ASYS(lstat, 2),
	ASYS(readlink, 3),
	ASYS(openat, 4),
	ASYS(getdents, 3),
	ASYS(lseek, 3),
	ASYS(dup, 1),
	{ NULL, 0, NULL }
};

// map syscall ints to strings (table generated via ausyscall --dump)

#if defined(__x86_64__)

const int nsyscalls = 322;
const char *syscalls[] = {
/* 0 */ "read",
/* 1 */ "write",
/* 2 */ "open",
/* 3 */ "close",
/* 4 */ "stat",
/* 5 */ "fstat",
/* 6 */ "lstat",
/* 7 */ "poll",
/* 8 */ "lseek",
/* 9 */ "mmap",
/* 10 */ "mprotect",
/* 11 */ "munmap",
/* 12 */ "brk",
/* 13 */ "rt_sigaction",
/* 14 */ "rt_sigprocmask",
/* 15 */ "rt_sigreturn",
/* 16 */ "ioctl",
/* 17 */ "pread",
/* 18 */ "pwrite",
/* 19 */ "readv",
/* 20 */ "writev",
/* 21 */ "access",
/* 22 */ "pipe",
/* 23 */ "select",
/* 24 */ "sched_yield",
/* 25 */ "mremap",
/* 26 */ "msync",
/* 27 */ "mincore",
/* 28 */ "madvise",
/* 29 */ "shmget",
/* 30 */ "shmat",
/* 31 */ "shmctl",
/* 32 */ "dup",
/* 33 */ "dup2",
/* 34 */ "pause",
/* 35 */ "nanosleep",
/* 36 */ "getitimer",
/* 37 */ "alarm",
/* 38 */ "setitimer",
/* 39 */ "getpid",
/* 40 */ "sendfile",
/* 41 */ "socket",
/* 42 */ "connect",
/* 43 */ "accept",
/* 44 */ "sendto",
/* 45 */ "recvfrom",
/* 46 */ "sendmsg",
/* 47 */ "recvmsg",
/* 48 */ "shutdown",
/* 49 */ "bind",
/* 50 */ "listen",
/* 51 */ "getsockname",
/* 52 */ "getpeername",
/* 53 */ "socketpair",
/* 54 */ "setsockopt",
/* 55 */ "getsockopt",
/* 56 */ "clone",
/* 57 */ "fork",
/* 58 */ "vfork",
/* 59 */ "execve",
/* 60 */ "exit",
/* 61 */ "wait4",
/* 62 */ "kill",
/* 63 */ "uname",
/* 64 */ "semget",
/* 65 */ "semop",
/* 66 */ "semctl",
/* 67 */ "shmdt",
/* 68 */ "msgget",
/* 69 */ "msgsnd",
/* 70 */ "msgrcv",
/* 71 */ "msgctl",
/* 72 */ "fcntl",
/* 73 */ "flock",
/* 74 */ "fsync",
/* 75 */ "fdatasync",
/* 76 */ "truncate",
/* 77 */ "ftruncate",
/* 78 */ "getdents",
/* 79 */ "getcwd",
/* 80 */ "chdir",
/* 81 */ "fchdir",
/* 82 */ "rename",
/* 83 */ "mkdir",
/* 84 */ "rmdir",
/* 85 */ "creat",
/* 86 */ "link",
/* 87 */ "unlink",
/* 88 */ "symlink",
/* 89 */ "readlink",
/* 90 */ "chmod",
/* 91 */ "fchmod",
/* 92 */ "chown",
/* 93 */ "fchown",
/* 94 */ "lchown",
/* 95 */ "umask",
/* 96 */ "gettimeofday",
/* 97 */ "getrlimit",
/* 98 */ "getrusage",
/* 99 */ "sysinfo",
/* 100 */ "times",
/* 101 */ "ptrace",
/* 102 */ "getuid",
/* 103 */ "syslog",
/* 104 */ "getgid",
/* 105 */ "setuid",
/* 106 */ "setgid",
/* 107 */ "geteuid",
/* 108 */ "getegid",
/* 109 */ "setpgid",
/* 110 */ "getppid",
/* 111 */ "getpgrp",
/* 112 */ "setsid",
/* 113 */ "setreuid",
/* 114 */ "setregid",
/* 115 */ "getgroups",
/* 116 */ "setgroups",
/* 117 */ "setresuid",
/* 118 */ "getresuid",
/* 119 */ "setresgid",
/* 120 */ "getresgid",
/* 121 */ "getpgid",
/* 122 */ "setfsuid",
/* 123 */ "setfsgid",
/* 124 */ "getsid",
/* 125 */ "capget",
/* 126 */ "capset",
/* 127 */ "rt_sigpending",
/* 128 */ "rt_sigtimedwait",
/* 129 */ "rt_sigqueueinfo",
/* 130 */ "rt_sigsuspend",
/* 131 */ "sigaltstack",
/* 132 */ "utime",
/* 133 */ "mknod",
/* 134 */ "uselib",
/* 135 */ "personality",
/* 136 */ "ustat",
/* 137 */ "statfs",
/* 138 */ "fstatfs",
/* 139 */ "sysfs",
/* 140 */ "getpriority",
/* 141 */ "setpriority",
/* 142 */ "sched_setparam",
/* 143 */ "sched_getparam",
/* 144 */ "sched_setscheduler",
/* 145 */ "sched_getscheduler",
/* 146 */ "sched_get_priority_max",
/* 147 */ "sched_get_priority_min",
/* 148 */ "sched_rr_get_interval",
/* 149 */ "mlock",
/* 150 */ "munlock",
/* 151 */ "mlockall",
/* 152 */ "munlockall",
/* 153 */ "vhangup",
/* 154 */ "modify_ldt",
/* 155 */ "pivot_root",
/* 156 */ "_sysctl",
/* 157 */ "prctl",
/* 158 */ "arch_prctl",
/* 159 */ "adjtimex",
/* 160 */ "setrlimit",
/* 161 */ "chroot",
/* 162 */ "sync",
/* 163 */ "acct",
/* 164 */ "settimeofday",
/* 165 */ "mount",
/* 166 */ "umount2",
/* 167 */ "swapon",
/* 168 */ "swapoff",
/* 169 */ "reboot",
/* 170 */ "sethostname",
/* 171 */ "setdomainname",
/* 172 */ "iopl",
/* 173 */ "ioperm",
/* 174 */ "create_module",
/* 175 */ "init_module",
/* 176 */ "delete_module",
/* 177 */ "get_kernel_syms",
/* 178 */ "query_module",
/* 179 */ "quotactl",
/* 180 */ "nfsservctl",
/* 181 */ "getpmsg",
/* 182 */ "putpmsg",
/* 183 */ "afs_syscall",
/* 184 */ "tuxcall",
/* 185 */ "security",
/* 186 */ "gettid",
/* 187 */ "readahead",
/* 188 */ "setxattr",
/* 189 */ "lsetxattr",
/* 190 */ "fsetxattr",
/* 191 */ "getxattr",
/* 192 */ "lgetxattr",
/* 193 */ "fgetxattr",
/* 194 */ "listxattr",
/* 195 */ "llistxattr",
/* 196 */ "flistxattr",
/* 197 */ "removexattr",
/* 198 */ "lremovexattr",
/* 199 */ "fremovexattr",
/* 200 */ "tkill",
/* 201 */ "time",
/* 202 */ "futex",
/* 203 */ "sched_setaffinity",
/* 204 */ "sched_getaffinity",
/* 205 */ "set_thread_area",
/* 206 */ "io_setup",
/* 207 */ "io_destroy",
/* 208 */ "io_getevents",
/* 209 */ "io_submit",
/* 210 */ "io_cancel",
/* 211 */ "get_thread_area",
/* 212 */ "lookup_dcookie",
/* 213 */ "epoll_create",
/* 214 */ "epoll_ctl_old",
/* 215 */ "epoll_wait_old",
/* 216 */ "remap_file_pages",
/* 217 */ "getdents64",
/* 218 */ "set_tid_address",
/* 219 */ "restart_syscall",
/* 220 */ "semtimedop",
/* 221 */ "fadvise64",
/* 222 */ "timer_create",
/* 223 */ "timer_settime",
/* 224 */ "timer_gettime",
/* 225 */ "timer_getoverrun",
/* 226 */ "timer_delete",
/* 227 */ "clock_settime",
/* 228 */ "clock_gettime",
/* 229 */ "clock_getres",
/* 230 */ "clock_nanosleep",
/* 231 */ "exit_group",
/* 232 */ "epoll_wait",
/* 233 */ "epoll_ctl",
/* 234 */ "tgkill",
/* 235 */ "utimes",
/* 236 */ "vserver",
/* 237 */ "mbind",
/* 238 */ "set_mempolicy",
/* 239 */ "get_mempolicy",
/* 240 */ "mq_open",
/* 241 */ "mq_unlink",
/* 242 */ "mq_timedsend",
/* 243 */ "mq_timedreceive",
/* 244 */ "mq_notify",
/* 245 */ "mq_getsetattr",
/* 246 */ "kexec_load",
/* 247 */ "waitid",
/* 248 */ "add_key",
/* 249 */ "request_key",
/* 250 */ "keyctl",
/* 251 */ "ioprio_set",
/* 252 */ "ioprio_get",
/* 253 */ "inotify_init",
/* 254 */ "inotify_add_watch",
/* 255 */ "inotify_rm_watch",
/* 256 */ "migrate_pages",
/* 257 */ "openat",
/* 258 */ "mkdirat",
/* 259 */ "mknodat",
/* 260 */ "fchownat",
/* 261 */ "futimesat",
/* 262 */ "newfstatat",
/* 263 */ "unlinkat",
/* 264 */ "renameat",
/* 265 */ "linkat",
/* 266 */ "symlinkat",
/* 267 */ "readlinkat",
/* 268 */ "fchmodat",
/* 269 */ "faccessat",
/* 270 */ "pselect6",
/* 271 */ "ppoll",
/* 272 */ "unshare",
/* 273 */ "set_robust_list",
/* 274 */ "get_robust_list",
/* 275 */ "splice",
/* 276 */ "tee",
/* 277 */ "sync_file_range",
/* 278 */ "vmsplice",
/* 279 */ "move_pages",
/* 280 */ "utimensat",
/* 281 */ "epoll_pwait",
/* 282 */ "signalfd",
/* 283 */ "timerfd",
/* 284 */ "eventfd",
/* 285 */ "fallocate",
/* 286 */ "timerfd_settime",
/* 287 */ "timerfd_gettime",
/* 288 */ "accept4",
/* 289 */ "signalfd4",
/* 290 */ "eventfd2",
/* 291 */ "epoll_create1",
/* 292 */ "dup3",
/* 293 */ "pipe2",
/* 294 */ "inotify_init1",
/* 295 */ "preadv",
/* 296 */ "pwritev",
/* 297 */ "rt_tgsigqueueinfo",
/* 298 */ "perf_event_open",
/* 299 */ "recvmmsg",
/* 300 */ "fanotify_init",
/* 301 */ "fanotify_mark",
/* 302 */ "prlimit64",
/* 303 */ "name_to_handle_at",
/* 304 */ "open_by_handle_at",
/* 305 */ "clock_adjtime",
/* 306 */ "syncfs",
/* 307 */ "sendmmsg",
/* 308 */ "setns",
/* 309 */ "getcpu",
/* 310 */ "process_vm_readv",
/* 311 */ "process_vm_writev",
/* 312 */ "kcmp",
/* 313 */ "finit_module",
/* 314 */ "sched_setattr",
/* 315 */ "sched_getattr",
/* 316 */ "renameat2",
/* 317 */ "seccomp",
/* 318 */ "getrandom",
/* 319 */ "memfd_create",
/* 320 */ "kexec_file_load",
/* 321 */ "bpf",
NULL
};

#elif defined(__i386__) /* arch */

const int nsyscalls = 358;
const char *syscalls[] = {
/* 0 */ "restart_syscall",
/* 1 */ "exit",
/* 2 */ "fork",
/* 3 */ "read",
/* 4 */ "write",
/* 5 */ "open",
/* 6 */ "close",
/* 7 */ "waitpid",
/* 8 */ "creat",
/* 9 */ "link",
/* 10 */ "unlink",
/* 11 */ "execve",
/* 12 */ "chdir",
/* 13 */ "time",
/* 14 */ "mknod",
/* 15 */ "chmod",
/* 16 */ "lchown",
/* 17 */ "break",
/* 18 */ "oldstat",
/* 19 */ "lseek",
/* 20 */ "getpid",
/* 21 */ "mount",
/* 22 */ "umount",
/* 23 */ "setuid",
/* 24 */ "getuid",
/* 25 */ "stime",
/* 26 */ "ptrace",
/* 27 */ "alarm",
/* 28 */ "oldfstat",
/* 29 */ "pause",
/* 30 */ "utime",
/* 31 */ "stty",
/* 32 */ "gtty",
/* 33 */ "access",
/* 34 */ "nice",
/* 35 */ "ftime",
/* 36 */ "sync",
/* 37 */ "kill",
/* 38 */ "rename",
/* 39 */ "mkdir",
/* 40 */ "rmdir",
/* 41 */ "dup",
/* 42 */ "pipe",
/* 43 */ "times",
/* 44 */ "prof",
/* 45 */ "brk",
/* 46 */ "setgid",
/* 47 */ "getgid",
/* 48 */ "signal",
/* 49 */ "geteuid",
/* 50 */ "getegid",
/* 51 */ "acct",
/* 52 */ "umount2",
/* 53 */ "lock",
/* 54 */ "ioctl",
/* 55 */ "fcntl",
/* 56 */ "mpx",
/* 57 */ "setpgid",
/* 58 */ "ulimit",
/* 59 */ "oldolduname",
/* 60 */ "umask",
/* 61 */ "chroot",
/* 62 */ "ustat",
/* 63 */ "dup2",
/* 64 */ "getppid",
/* 65 */ "getpgrp",
/* 66 */ "setsid",
/* 67 */ "sigaction",
/* 68 */ "sgetmask",
/* 69 */ "ssetmask",
/* 70 */ "setreuid",
/* 71 */ "setregid",
/* 72 */ "sigsuspend",
/* 73 */ "sigpending",
/* 74 */ "sethostname",
/* 75 */ "setrlimit",
/* 76 */ "getrlimit",
/* 77 */ "getrusage",
/* 78 */ "gettimeofday",
/* 79 */ "settimeofday",
/* 80 */ "getgroups",
/* 81 */ "setgroups",
/* 82 */ "select",
/* 83 */ "symlink",
/* 84 */ "oldlstat",
/* 85 */ "readlink",
/* 86 */ "uselib",
/* 87 */ "swapon",
/* 88 */ "reboot",
/* 89 */ "readdir",
/* 90 */ "mmap",
/* 91 */ "munmap",
/* 92 */ "truncate",
/* 93 */ "ftruncate",
/* 94 */ "fchmod",
/* 95 */ "fchown",
/* 96 */ "getpriority",
/* 97 */ "setpriority",
/* 98 */ "profil",
/* 99 */ "statfs",
/* 100 */ "fstatfs",
/* 101 */ "ioperm",
/* 102 */ "socketcall",
/* 103 */ "syslog",
/* 104 */ "setitimer",
/* 105 */ "getitimer",
/* 106 */ "stat",
/* 107 */ "lstat",
/* 108 */ "fstat",
/* 109 */ "olduname",
/* 110 */ "iopl",
/* 111 */ "vhangup",
/* 112 */ "idle",
/* 113 */ "vm86old",
/* 114 */ "wait4",
/* 115 */ "swapoff",
/* 116 */ "sysinfo",
/* 117 */ "ipc",
/* 118 */ "fsync",
/* 119 */ "sigreturn",
/* 120 */ "clone",
/* 121 */ "setdomainname",
/* 122 */ "uname",
/* 123 */ "modify_ldt",
/* 124 */ "adjtimex",
/* 125 */ "mprotect",
/* 126 */ "sigprocmask",
/* 127 */ "create_module",
/* 128 */ "init_module",
/* 129 */ "delete_module",
/* 130 */ "get_kernel_syms",
/* 131 */ "quotactl",
/* 132 */ "getpgid",
/* 133 */ "fchdir",
/* 134 */ "bdflush",
/* 135 */ "sysfs",
/* 136 */ "personality",
/* 137 */ "afs_syscall",
/* 138 */ "setfsuid",
/* 139 */ "setfsgid",
/* 140 */ "_llseek",
/* 141 */ "getdents",
/* 142 */ "_newselect",
/* 143 */ "flock",
/* 144 */ "msync",
/* 145 */ "readv",
/* 146 */ "writev",
/* 147 */ "getsid",
/* 148 */ "fdatasync",
/* 149 */ "_sysctl",
/* 150 */ "mlock",
/* 151 */ "munlock",
/* 152 */ "mlockall",
/* 153 */ "munlockall",
/* 154 */ "sched_setparam",
/* 155 */ "sched_getparam",
/* 156 */ "sched_setscheduler",
/* 157 */ "sched_getscheduler",
/* 158 */ "sched_yield",
/* 159 */ "sched_get_priority_max",
/* 160 */ "sched_get_priority_min",
/* 161 */ "sched_rr_get_interval",
/* 162 */ "nanosleep",
/* 163 */ "mremap",
/* 164 */ "setresuid",
/* 165 */ "getresuid",
/* 166 */ "vm86",
/* 167 */ "query_module",
/* 168 */ "poll",
/* 169 */ "nfsservctl",
/* 170 */ "setresgid",
/* 171 */ "getresgid",
/* 172 */ "prctl",
/* 173 */ "rt_sigreturn",
/* 174 */ "rt_sigaction",
/* 175 */ "rt_sigprocmask",
/* 176 */ "rt_sigpending",
/* 177 */ "rt_sigtimedwait",
/* 178 */ "rt_sigqueueinfo",
/* 179 */ "rt_sigsuspend",
/* 180 */ "pread64",
/* 181 */ "pwrite64",
/* 182 */ "chown",
/* 183 */ "getcwd",
/* 184 */ "capget",
/* 185 */ "capset",
/* 186 */ "sigaltstack",
/* 187 */ "sendfile",
/* 188 */ "getpmsg",
/* 189 */ "putpmsg",
/* 190 */ "vfork",
/* 191 */ "ugetrlimit",
/* 192 */ "mmap2",
/* 193 */ "truncate64",
/* 194 */ "ftruncate64",
/* 195 */ "stat64",
/* 196 */ "lstat64",
/* 197 */ "fstat64",
/* 198 */ "lchown32",
/* 199 */ "getuid32",
/* 200 */ "getgid32",
/* 201 */ "geteuid32",
/* 202 */ "getegid32",
/* 203 */ "setreuid32",
/* 204 */ "setregid32",
/* 205 */ "getgroups32",
/* 206 */ "setgroups32",
/* 207 */ "fchown32",
/* 208 */ "setresuid32",
/* 209 */ "getresuid32",
/* 210 */ "setresgid32",
/* 211 */ "getresgid32",
/* 212 */ "chown32",
/* 213 */ "setuid32",
/* 214 */ "setgid32",
/* 215 */ "setfsuid32",
/* 216 */ "setfsgid32",
/* 217 */ "pivot_root",
/* 218 */ "mincore",
/* 219 */ "madvise",
/* 220 */ "getdents64",
/* 221 */ "fcntl64",
/* 224 */ "gettid",
/* 225 */ "readahead",
/* 226 */ "setxattr",
/* 227 */ "lsetxattr",
/* 228 */ "fsetxattr",
/* 229 */ "getxattr",
/* 230 */ "lgetxattr",
/* 231 */ "fgetxattr",
/* 232 */ "listxattr",
/* 233 */ "llistxattr",
/* 234 */ "flistxattr",
/* 235 */ "removexattr",
/* 236 */ "lremovexattr",
/* 237 */ "fremovexattr",
/* 238 */ "tkill",
/* 239 */ "sendfile64",
/* 240 */ "futex",
/* 241 */ "sched_setaffinity",
/* 242 */ "sched_getaffinity",
/* 243 */ "set_thread_area",
/* 244 */ "get_thread_area",
/* 245 */ "io_setup",
/* 246 */ "io_destroy",
/* 247 */ "io_getevents",
/* 248 */ "io_submit",
/* 249 */ "io_cancel",
/* 250 */ "fadvise64",
/* 252 */ "exit_group",
/* 253 */ "lookup_dcookie",
/* 254 */ "epoll_create",
/* 255 */ "epoll_ctl",
/* 256 */ "epoll_wait",
/* 257 */ "remap_file_pages",
/* 258 */ "set_tid_address",
/* 259 */ "timer_create",
/* 260 */ "timer_settime",
/* 261 */ "timer_gettime",
/* 262 */ "timer_getoverrun",
/* 263 */ "timer_delete",
/* 264 */ "clock_settime",
/* 265 */ "clock_gettime",
/* 266 */ "clock_getres",
/* 267 */ "clock_nanosleep",
/* 268 */ "statfs64",
/* 269 */ "fstatfs64",
/* 270 */ "tgkill",
/* 271 */ "utimes",
/* 272 */ "fadvise64_64",
/* 273 */ "vserver",
/* 274 */ "mbind",
/* 275 */ "get_mempolicy",
/* 276 */ "set_mempolicy",
/* 277 */ "mq_open",
/* 278 */ "mq_unlink",
/* 279 */ "mq_timedsend",
/* 280 */ "mq_timedreceive",
/* 281 */ "mq_notify",
/* 282 */ "mq_getsetattr",
/* 283 */ "sys_kexec_load",
/* 284 */ "waitid",
/* 286 */ "add_key",
/* 287 */ "request_key",
/* 288 */ "keyctl",
/* 289 */ "ioprio_set",
/* 290 */ "ioprio_get",
/* 291 */ "inotify_init",
/* 292 */ "inotify_add_watch",
/* 293 */ "inotify_rm_watch",
/* 294 */ "migrate_pages",
/* 295 */ "openat",
/* 296 */ "mkdirat",
/* 297 */ "mknodat",
/* 298 */ "fchownat",
/* 299 */ "futimesat",
/* 300 */ "fstatat64",
/* 301 */ "unlinkat",
/* 302 */ "renameat",
/* 303 */ "linkat",
/* 304 */ "symlinkat",
/* 305 */ "readlinkat",
/* 306 */ "fchmodat",
/* 307 */ "faccessat",
/* 308 */ "pselect6",
/* 309 */ "ppoll",
/* 310 */ "unshare",
/* 311 */ "set_robust_list",
/* 312 */ "get_robust_list",
/* 313 */ "splice",
/* 314 */ "sync_file_range",
/* 315 */ "tee",
/* 316 */ "vmsplice",
/* 317 */ "move_pages",
/* 318 */ "getcpu",
/* 319 */ "epoll_pwait",
/* 320 */ "utimensat",
/* 321 */ "signalfd",
/* 322 */ "timerfd",
/* 323 */ "eventfd",
/* 324 */ "fallocate",
/* 325 */ "timerfd_settime",
/* 326 */ "timerfd_gettime",
/* 327 */ "signalfd4",
/* 328 */ "eventfd2",
/* 329 */ "epoll_create1",
/* 330 */ "dup3",
/* 331 */ "pipe2",
/* 332 */ "inotify_init1",
/* 333 */ "preadv",
/* 334 */ "pwritev",
/* 335 */ "rt_tgsigqueueinfo",
/* 336 */ "perf_event_open",
/* 337 */ "recvmmsg",
/* 338 */ "fanotify_init",
/* 339 */ "fanotify_mark",
/* 340 */ "prlimit64",
/* 341 */ "name_to_handle_at",
/* 342 */ "open_by_handle_at",
/* 343 */ "clock_adjtime",
/* 344 */ "syncfs",
/* 345 */ "sendmmsg",
/* 346 */ "setns",
/* 347 */ "process_vm_readv",
/* 348 */ "process_vm_writev",
/* 349 */ "kcmp",
/* 350 */ "finit_module",
/* 351 */ "sched_setattr",
/* 352 */ "sched_getattr",
/* 353 */ "renameat2",
/* 354 */ "seccomp",
/* 355 */ "getrandom",
/* 356 */ "memfd_create",
/* 357 */ "bpf",
NULL
};

#endif /* arch */
