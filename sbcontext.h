struct json_object;

// trampoline namespaces, at the C level we only care about the syscall and sandbox namespaces
// the parent can define additional namespaces which the python libraries can call into
#define NS_SYS 0
#define NS_SB 1

// The varargs in trampoline should all be json_object *'s.
// If an error occurs, trampoline will set errno and return -1.
// If out is not NULL, the response json_object * will be set there,
// it is the caller's responsibility to free it with json_object_put()
extern intptr_t dispatch(intptr_t (*func)(va_list), ...);
extern int trampoline(struct json_object **out, int ns, const char *fname, int numargs, ...);
extern void fatal(const char *msg) __attribute__ ((noreturn));
extern int writejson(const char *json);
extern int readjson(struct json_object **out);
extern int readraw(const char *format, ...);
extern int base64decode(const char *in, size_t inLen, unsigned char *out, size_t *outLen);
extern void _debug_backtrace();

// O_TMPFILE was added in kernel 3.11, some distros are still stuck on older versions
// (for example, CentOS 7 is on 3.10). As such, ignore the flag
#ifndef O_TMPFILE
#define O_TMPFILE 0
#endif

#ifdef SB_DEBUG
#define debug_print(...) printf(__VA_ARGS__)
#define debug_error(...) fprintf(stderr, __VA_ARGS__)
#define debug_backtrace() _debug_backtrace()
#else
#define debug_print(...)
#define debug_error(...)
#define debug_backtrace()
#endif

#ifndef SYS_SECCOMP
#define SYS_SECCOMP 1
#endif

// fds for our input and output, opened by the parent proc
#define PIPEIN 3
#define PIPEOUT 4

// default resource usage limits by sandbox, 200 MiB of memory and 5 seconds of cpu time
// these can be modified (increased or decreased) by argv params
#define DEF_MEMORY 209715200
#define DEF_CPU 5

// architecture-dependent macros to manipulate registers given a ucontext_t
// register mapping lifted from man syscall(2) and browsing ucontext.h source

#if defined(__x86_64__)

#define SB_REG(ctx, reg) ((ctx)->uc_mcontext.gregs[(reg)])
#define SB_P1(ctx) SB_REG(ctx, REG_RDI)
#define SB_P2(ctx) SB_REG(ctx, REG_RSI)
#define SB_P3(ctx) SB_REG(ctx, REG_RDX)
#define SB_P4(ctx) SB_REG(ctx, REG_R10)
#define SB_P5(ctx) SB_REG(ctx, REG_R8)
#define SB_P6(ctx) SB_REG(ctx, REG_R9)
#define SB_RET(ctx) SB_REG(ctx, REG_RAX)

#elif defined(__i386__) /* arch */

#define SB_REG(ctx, reg) ((ctx)->uc_mcontext.gregs[(reg)])
#define SB_P1(ctx) SB_REG(ctx, REG_EBX)
#define SB_P2(ctx) SB_REG(ctx, REG_ECX)
#define SB_P3(ctx) SB_REG(ctx, REG_EDX)
#define SB_P4(ctx) SB_REG(ctx, REG_ESI)
#define SB_P5(ctx) SB_REG(ctx, REG_EDI)
#define SB_P6(ctx) SB_REG(ctx, REG_EDP)
#define SB_RET(ctx) SB_REG(ctx, REG_EAX)

#endif /* arch */

