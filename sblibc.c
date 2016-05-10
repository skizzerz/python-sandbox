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
	fprintf(stderr, "*** %s ***: sandbox terminated\n", msg);
	exit(1);
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
	json_object *json_data = NULL;
	json_object *name = json_object_new_string(fname);
	json_object *args = json_object_new_array();

	va_start(vargs, numargs);
	for (i = 0; i < numargs; ++i) {
		json_object_array_add(args, va_arg(vargs, json_object *));
	}
	va_end(vargs);

	json_object_object_add(callinfo, "name", name);
	json_object_object_add(callinfo, "args", args);

	ret = writejson(json_object_to_json_string_ext(callinfo, JSON_C_TO_STRING_PLAIN));
	if (ret < 0) {
		debug_error("writejson failed with errno %d\n", errno);
		exit(errno);
	}

	ret = readjson(&response);
	if (ret < 0) {
		debug_error("readjson failed with errno %d\n", errno);
		exit(errno);
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
