#include <Python.h>
#include <frameobject.h>
#include <seccomp.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <ucontext.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <execinfo.h>
#include <dlfcn.h>
#include <dirent.h>

#include "sbcontext.h"
#include "sblibc.h"

void sigsys_handler(int signal, siginfo_t *info, void *context);

int run_child()
{
	int ret = -1;
	unsigned long limits[2] = {0};
	int vpathsz = 0;
	char *vpath = NULL;
	struct rlimit rl;
	wchar_t *program = NULL;
	size_t programlen = 0;
	FILE *mainpy = NULL;
	scmp_filter_ctx ctx = NULL;

#ifdef SB_DEBUG
	// not used, it's here to force loading of the relevant .sos before the sandbox inits
	// so that we can safely print backtraces in the sigtrap handler
	void *bt_buffer[32];
	int bt_n = backtrace(bt_buffer, 32);
	char **bt_sym = backtrace_symbols(bt_buffer, bt_n);
	free(bt_sym);
#endif

	// grab config from parent about memory and cpu limits
	ret = read(RPCSOCK, limits, sizeof(limits));
	if (ret != sizeof(limits))
		goto cleanup;

	debug_print("Got %lu memory and %lu cpu\n", limits[0], limits[1]);

	if (limits[0] == 0)
		limits[0] = DEF_MEMORY;

	if (limits[1] == 0)
		limits[1] = DEF_CPU;

	// set resource limits, we limit our address space and cpu, and disable core dumps
	rl.rlim_cur = limits[0];
	rl.rlim_max = limits[0];
	ret = setrlimit(RLIMIT_AS, &rl);
	if (ret < 0)
		goto cleanup;

	rl.rlim_cur = 0;
	rl.rlim_max = 0;
	ret = setrlimit(RLIMIT_CORE, &rl);
	if (ret < 0)
		goto cleanup;

	rl.rlim_cur = limits[1];
	rl.rlim_max = limits[1];
	ret = setrlimit(RLIMIT_CPU, &rl);
	if (ret < 0)
		goto cleanup;

	// set up our SIGSYS handler; any disallowed syscalls are trapped by this handler
	// and sent up to the parent to process.
	struct sigaction sa;
	sigset_t mask;
	// Allow other signals to interrupt our signal handler, including signals from ourself in case we implement
	// a syscall by making other syscalls (our mmap implementation operates this way).
	// This means that if the handler tries to make an invalid syscall, it will recurse until it
	// fills up the stack space and crashes.
	sigemptyset(&mask);
	sa.sa_sigaction = sigsys_handler;
	sa.sa_mask = mask;
	sa.sa_flags = SA_SIGINFO | SA_NODEFER;
	sigaction(SIGSYS, &sa, NULL);

	/* set up seccomp, we allow the following syscalls:
	 * IO:
	 * - read(), readv() - fd 3 only
	 * - write(), writev() - fd 3 only (also stdout and stderr if in debug mode)
	 * - fstat(), fcntl(F_GETFD), fcntl(F_GETFL) - fd 3 only
	 * Memory:
	 * - mmap(MAP_ANONYMOUS | MAP_PRIVATE) - new mappings that don't read from fds,
	 *   this still allows the application to choose a memory address, but this is unfortunately required
	 *   in order for dlopen() to work (as that makes use of MAP_FIXED)
	 * - brk() - memory allocation (we have setrlimit to keep this in check)
	 * - munmap() - deallocation
	 * - mprotect() - changing protection (used by our implementation of mmap for fds)
	 * Signal Handlers:
	 * - sigreturn(), rt_sigreturn(), rt_sigprocmask(), sigaltstack()
	 * - rt_sigaction() - can retrieve all signals (2nd param NULL), cannot set handler for SIGSYS
	 * Misc:
	 * - getrusage(RUSAGE_SELF) - used for profiling purposes
	 * - tgkill() - Called by Python internals
	 * - futex() - dlsym() needs this, python threading probably does too
	 * - uname() - should be no harm in revealing kernel version info as it should be kept patched anyway,
	 *   only info leak would be from the nodename/domainname fields. If hiding that is important,
	 *   open an issue on github and I can supply a compiler flag to pass uname calls to the parent instead.
	 * - exit(), exit_group() - so program can terminate
	 */
	ctx = seccomp_init(SCMP_ACT_TRAP);

	if (ctx == NULL)
		goto cleanup;

#define SB_RULE(sys, ...) ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(sys), __VA_ARGS__); if (ret < 0) goto cleanup

	SB_RULE(read, 1, SCMP_A0(SCMP_CMP_EQ, RPCSOCK));
	SB_RULE(write, 1, SCMP_A0(SCMP_CMP_EQ, RPCSOCK));
	SB_RULE(readv, 1, SCMP_A0(SCMP_CMP_EQ, RPCSOCK));
	SB_RULE(writev, 1, SCMP_A0(SCMP_CMP_EQ, RPCSOCK));
	SB_RULE(fstat, 1, SCMP_A0(SCMP_CMP_EQ, RPCSOCK));
	SB_RULE(fcntl, 2, SCMP_A0(SCMP_CMP_EQ, RPCSOCK), SCMP_A1(SCMP_CMP_EQ, F_GETFD));
	SB_RULE(fcntl, 2, SCMP_A0(SCMP_CMP_EQ, RPCSOCK), SCMP_A1(SCMP_CMP_EQ, F_GETFL));

#ifdef SB_DEBUG
	// if debugging, allow sandbox to write to stdout and stderr
	SB_RULE(write, 1, SCMP_A0(SCMP_CMP_EQ, 1));
	SB_RULE(write, 1, SCMP_A0(SCMP_CMP_EQ, 2));
	SB_RULE(writev, 1, SCMP_A0(SCMP_CMP_EQ, 1));
	SB_RULE(writev, 1, SCMP_A0(SCMP_CMP_EQ, 2));
#endif

	SB_RULE(mmap, 1, SCMP_A3(SCMP_CMP_MASKED_EQ, MAP_ANONYMOUS | MAP_PRIVATE, MAP_ANONYMOUS | MAP_PRIVATE));
	SB_RULE(brk, 0);
	SB_RULE(munmap, 0);
	SB_RULE(mprotect, 0);
	SB_RULE(sigreturn, 0);
	SB_RULE(rt_sigreturn, 0);
	SB_RULE(rt_sigprocmask, 0);
	SB_RULE(sigaltstack, 0);
	SB_RULE(rt_sigaction, 1, SCMP_A0(SCMP_CMP_NE, SIGSYS));
	SB_RULE(rt_sigaction, 1, SCMP_A1(SCMP_CMP_EQ, NULL));
	SB_RULE(futex, 0);
	SB_RULE(uname, 0);
	SB_RULE(getrusage, 1, SCMP_A0(SCMP_CMP_EQ, RUSAGE_SELF));
	SB_RULE(tgkill, 1, SCMP_A0(SCMP_CMP_EQ, getpid()));
	SB_RULE(exit_group, 0);
	SB_RULE(exit, 0);

	ret = seccomp_load(ctx);
	if (ret < 0)
		goto cleanup;

#undef SB_RULE

	// inform the preloader that we are now inside the sandbox
	// this causes it to override a couple more libc functions that it simply passes through above
	void (*enable_sandbox)(void) = dlsym(RTLD_DEFAULT, "enable_sandbox");
	if (!enable_sandbox) {
		ret = -1;
		fprintf(stderr, "Unable to find enable_sandbox function. Ensure preloader is installed.\n");
		goto cleanup;
	}

	enable_sandbox();

	// grab (virtual) path to python from parent
	ret = read(RPCSOCK, &vpathsz, sizeof(vpathsz));
	if (ret != sizeof(vpathsz) || vpathsz <= 0)
		goto cleanup;
	vpath = (char *)malloc(vpathsz + 1);
	ret = read(RPCSOCK, vpath, vpathsz);
	if (ret != vpathsz)
		goto cleanup;
	vpath[vpathsz - 1] = '\0';

	// initialize python interpreter -- this is initialized AFTER sandbox is set up
	// so that the python path can be faked (in essence, this allows for the parent proc
	// to implement a pseudo-chroot by specifying a virtual path to python).
	programlen = mbstowcs(NULL, vpath, 0) + 1;
	program = (wchar_t *)malloc(programlen * sizeof(wchar_t));
	if (mbstowcs(program, vpath, programlen) == (size_t)-1) {
		ret = -1;
		fprintf(stderr, "Cannot decode python path.\n");
		goto cleanup;
	}

	Py_SetProgramName(program);
	Py_Initialize();

	// optional user init code
	mainpy = fopen("init.py", "r");
	if (mainpy != NULL) {
		// this closes mainpy after completion so we don't need to fclose it in cleanup
		ret = PyRun_SimpleFile(mainpy, "init.py");
		if (ret != 0) {
			fprintf(stderr, "init.py returned error %d.\n", ret);
			goto cleanup;
		}
	}

	// notify the parent sandbox that we have completed initialization
	PyObject *sandbox, *complete_init, *complete_init_ret;
	sandbox = PyImport_ImportModule("sandbox");
	if (sandbox == NULL) {
		PyErr_Print();
		fprintf(stderr, "unable to import sandbox module.\n");
		ret = -1;
		goto cleanup;
	}

	complete_init = PyObject_GetAttrString(sandbox, "complete_init");
	if (complete_init == NULL || !PyCallable_Check(complete_init)) {
		if (PyErr_Occurred()) {
			PyErr_Print();
		}

		fprintf(stderr, "cannot run sandbox.complete_init().\n");
		ret = -1;
		Py_XDECREF(complete_init);
		goto cleanup;
	}

	complete_init_ret = PyObject_CallObject(complete_init, NULL);
	if (complete_init_ret == NULL) {
		PyErr_Print();
		fprintf(stderr, "error running sandbox.complete_init().\n");
		ret = -1;
		goto cleanup;
	}

	Py_DECREF(complete_init_ret);
	Py_DECREF(complete_init);
	Py_DECREF(sandbox);

	// at this point, init is complete and we can begin to run user code.
	// The parent is expected to provide a /tmp/main.py file for this.
	mainpy = fopen("main.py", "r");
	if (mainpy == NULL) {
		ret = errno;
		fprintf(stderr, "Cannot open main.py.\n");
		goto cleanup;
	}

	ret = PyRun_SimpleFile(mainpy, "main.py");

cleanup:
	Py_Finalize();
	free(program);
	free(vpath);
	seccomp_release(ctx);

	return -ret;
}

void sigsys_handler(int signal, siginfo_t *siginfo, void *void_ctx)
{
	const char *name;
	int found = 0, i;
	// SB_P*(ctx) can be used to get first 6 params passed to syscall (P1-P6)
	ucontext_t *ctx = (ucontext_t *)void_ctx;
#ifdef SB_DEBUG
	void *buffer[32];
	int n;
#endif

	if (signal != SIGSYS || siginfo->si_code != SYS_SECCOMP) {
		// signal was not generated by seccomp, ignore it
		return;
	}

	if (siginfo->si_syscall >= nsyscalls) {
		// invalid or unknown syscall number
#ifdef SB_DEBUG
		fprintf(stderr, "Unknown syscall number %d\n", siginfo->si_syscall);
		fputs("Backtrace:\n", stderr);
		n = backtrace(buffer, 32);
		backtrace_symbols_fd(buffer, n, STDERR_FILENO);
#endif

		exit(SIGSYS);
	}

	name = syscalls[siginfo->si_syscall];
	for (i = 0; arg_map[i].sys != NULL; ++i) {
		if (!strcmp(name, arg_map[i].sys)) {
			found = 1;
			break;
		}
	}

	if (found) {
		SB_RET(ctx) = dispatch(arg_map[i].func, SB_P1(ctx), SB_P2(ctx), SB_P3(ctx), SB_P4(ctx), SB_P5(ctx), SB_P6(ctx));
		return;
	}

#ifdef SB_DEBUG	
	fprintf(stderr, "Syscall not implemented %s(%lld, %lld, %lld, %lld, %lld, %lld)\n",
		name, SB_P1(ctx), SB_P2(ctx), SB_P3(ctx), SB_P4(ctx), SB_P5(ctx), SB_P6(ctx));
	fputs("Backtrace:\n", stderr);
	n = backtrace(buffer, 32);
	backtrace_symbols_fd(buffer, n, STDERR_FILENO);
#endif

	exit(SIGSYS);
}
