#ifndef SBCONTEXT_H
#define SBCONTEXT_H

/* RPC namespaces, the parent can define additional namespaces which
 * the python libraries can call into, but the C side only ever calls these.
 */
#define NS_SYS 0
#define NS_SB 1
#define NS_APP 2

/* O_TMPFILE was added in kernel 3.11, some distros are still stuck on older versions
 * (for example, CentOS 7 is on 3.10). As such, ignore the flag
 */
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

/* fds for our input and output to the overall parent */
#define PIPEIN 3
#define PIPEOUT 4

/* sandboxed child talks to its parent on this fd; it's a socket unlike the above */
#define RPCSOCK 3

/* default resource usage limits by sandbox, 200 MiB of memory and 5 seconds of cpu time
 * these can be modified (increased or decreased) by configuration passed to parent
 */
#define DEF_MEMORY 209715200
#define DEF_CPU 5

int run_child();
int run_parent(pid_t child_pid, int child_socket);
intptr_t dispatch(intptr_t (*func)(va_list), ...);
void fatal(const char *msg) __attribute__ ((noreturn));
void _debug_backtrace();

extern _Bool is_child;

#define SBFS_FOLLOW    0x0001 /* filter: follow symlinks */
#define SBFS_RECURSE   0x0002 /* filter: allow recursion into real subdirectories */
#define SBFS_BLACKLIST 0x0004 /* filter: treat filter as a blacklist instead of a whitelist */
#define SBFS_PROXY     0x0100 /* forward any requests for this node or subnodes to the parent */
#define SBFS_WRITABLE  0x0200 /* makes this node/fd writable; still need appropriate fs permissions */
#define SBFS_DIRECTORY 0x0400 /* marks this node as a directory; if unset indicates node is a file */
#define SBFS_CLOEXEC   0x0800 /* close-on-exec flag for virtual nodes */
#define SBFS_NOCLOSE   0x1000 /* node cannot be closed (used for virtual stdin/stdout/stderr) */

#define MAX_FDS 64

// FNM_EXTMATCH is a GNU extension to fnmatch(), don't use if it doesn't exist
#ifndef FNM_EXTMATCH
#define FNM_EXTMATCH 0
#endif

struct sbfs_node {
	char *name;
	char *realpath; // usually NULL if virtual node (might not be if it is also a proxy node)
	struct sbfs_node *parent; // parent node (root's parent is root, not NULL)
	struct sbfs_node *child; // first child or NULL
	struct sbfs_node *next; // next sibling or NULL
	char **filter; // filters that apply to real children or NULL if no filters
	unsigned int flags; // bitfield of SBFS_* constants
};

struct sbfs_fd {
	int realfd; // the real fd for this, -1 if virtual or 0 for invalid fd
	struct sbfs_node *node; // name and realpath are deep copied, those and node itself must be free()d.
};

extern struct sbfs_node root;
extern struct sbfs_node proxy;
extern struct sbfs_fd fds[MAX_FDS];

int open_node(const char *pathname, int flags, int mode);
int read_node(int fd, void *buf, size_t count);
int write_node(int fd, const void *buf, size_t count);
int stat_node(const char *path, struct stat *buf);
int fstat_node(int fd, struct stat *buf);
int lstat_node(const char *path, struct stat *buf);
int close_node(int fd);

/* External API (Parent <-> Overall parent) */
struct json_object;

/* The varargs in trampoline should all be json_object *'s.
 * If an error occurs, trampoline will set errno and return -1.
 * If out is not NULL, the response json_object * will be set there,
 * it is the caller's responsibility to free it with json_object_put().
 * NOTE: these functions only work on the parent process, calling from child will
 * result in sandbox termination.
 */
int trampoline(struct json_object **out, int ns, const char *fname, int numargs, ...);
int writejson(const char *json);
int readjson(struct json_object **out);
int base64decode(const char *in, size_t inLen, unsigned char *out, size_t *outLen);

/* architecture-dependent macros to manipulate registers given a ucontext_t
 * register mapping lifted from man syscall(2) and browsing ucontext.h source
 */

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

#endif /* SBCONTEXT_H */
