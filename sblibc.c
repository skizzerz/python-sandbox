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

#include "sbcontext.h"
#include "sblibc.h"

static FILE *pipeout = NULL;
static FILE *pipein = NULL;

// helper function to write json string to pipeout, terminated with a newline
static int writejson(const char *json)
{
	if (pipeout == NULL) {
		pipeout = fdopen(PIPEOUT, "w");
	}

	return fprintf(pipeout, "%s\n", json);
}

// helper function to read json string from pipein (until a newline is encountered)
// caller is responsible for freeing the generated json_object
static int readjson(json_object **out)
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

	*out = json_tokener_parse(buf);

cleanup:
	free(buf);

	return code;
}

// we send a JSON line containing the call in the following format:
// {"name": "str", "args": [any...]}
// we then get a JSON reply in the following format:
// {"code": int, "errno": int, "data": any}
// (errno and data are optional keys, code is required)
int trampoline(struct json_object **out, const char *fname, int numargs, ...)
{
	va_list vargs;
	int ret = 0, i;
	json_object *callinfo = json_object_new_object();
	json_object *response = NULL;
	json_object *json_code = NULL;
	json_object *json_errno = NULL;
	json_object *name = json_object_new_string(fname);
	json_object *args = json_object_new_array();

	va_start(vargs, numargs);
	for (i = 0; i < numargs; ++i) {
		json_object_array_add(args, va_arg(vargs, json_object *));
	}
	va_end(vargs);

	json_object_object_add(callinfo, "name", name);
	json_object_object_add(callinfo, "args", args);

	ret = writejson(json_object_get_string(callinfo));
	if (ret < 0) {
		exit(errno);
	} else if (feof(pipeout)) {
		exit(-SIGPIPE);
	}

	ret = readjson(&response);
	if (ret < 0) {
		exit(errno);
	} else if (feof(pipein)) {
		exit(-SIGPIPE);
	}

	if (!json_object_is_type(response, json_type_object)) {
		exit(EPROTO);
	}

	if (!json_object_object_get_ex(response, "code", &json_code)) {
		exit(EPROTO);
	}

	if (!json_object_is_type(json_code, json_type_int)) {
		exit(EPROTO);
	}

	ret = json_object_get_int(json_code);

	if (ret == -1 && json_object_object_get_ex(*out, "errno", &json_errno)) {
		if (!json_object_is_type(json_errno, json_type_int)) {
			exit(EPROTO);
		}

		errno = json_object_get_int(json_errno);
	} else {
		errno = 0;
	}

	if (out == NULL) {
		json_object_put(response);
	} else {
		*out = response;
	}

	return ret;
}
