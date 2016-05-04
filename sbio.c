#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>
#include <json/json.h>

#include "sblibc.h"

int open(const char *pathname, int flags, ...)
{
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
