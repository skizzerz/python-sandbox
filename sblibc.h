// Utility function used to marshal data to the parent context

struct json_object;

// The varargs in trampoline should all be json_object *'s.
// If an error occurs, trampoline will set errno and return -1.
// If out is not NULL, the response json_object * will be set there,
// it is the caller's responsibility to free it with json_object_put()
extern int trampoline(struct json_object **out, const char *fname, int numargs, ...);
extern void fatal(const char *msg) __attribute__ ((noreturn));

// O_TMPFILE was added in kernel 3.11, some distros are still stuck on older versions
// (for example, CentOS 7 is on 3.10). As such, ignore the flag
#ifndef O_TMPFILE
#define O_TMPFILE 0
#endif

#ifdef SBLIBC_DEBUG
#define debug_print(...) printf(__VA_ARGS__)
#define debug_error(...) fprintf(stderr, __VA_ARGS__)
#define debug_backtrace() _debug_backtrace()
#else
#define debug_print(...)
#define debug_error(...)
#define debug_backtrace()
#endif
