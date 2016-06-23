#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fnmatch.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <json/json.h>

#include "sbcontext.h"
#include "sblibc.h"

struct sbfs_node root;
struct sbfs_node proxy;
struct sbfs_fd fds[MAX_FDS];

static struct sbfs_node sb_stdin = { "stdin", NULL, NULL, NULL, NULL, NULL, SBFS_NOCLOSE };
static struct sbfs_node sb_stdout = { "stdout", NULL, NULL, NULL, NULL, NULL, SBFS_WRITABLE | SBFS_NOCLOSE };
static struct sbfs_node sb_stderr = { "stderr", NULL, NULL, NULL, NULL, NULL, SBFS_WRITABLE | SBFS_NOCLOSE };

static struct sbfs_node *get_node(const char *path);
static void build_tree(json_object *json, struct sbfs_node *parent);

int run_parent(pid_t child_pid, int child_socket)
{
	json_object *out = NULL;
	json_object *temp = NULL;
	unsigned int limits[3];
	int ret;

	// The first thing our child expects is for us to stream the memory and cpu limits
	// it expects this without first sending a request for them our way.
	// We request these limits from our parent and then forward them onwards.
	ret = trampoline(&out, NS_SB, "getlimits", 0);
	if (ret != 0) {
		json_object_put(out);
		return ret;
	}

	json_object_object_get_ex(out, "mem", &temp);
	limits[0] = (unsigned int)json_object_get_int64(temp);
	json_object_object_get_ex(out, "cpu", &temp);
	limits[1] = (unsigned int)json_object_get_int64(temp);
	json_object_put(out);

	ret = write(child_socket, limits, sizeof(limits));
	if (ret != sizeof(limits)) {
		debug_error("Unable to send limit data to child.\n");
		return -1;
	}

	// Now, set up our virtualized filesystem; our parent tells us a mapping of real and
	// virtual nodes to use (real nodes point at actual things in the filesystem,
	// virtual nodes are stored on the parent and we send a request upstream whenever
	// something needs to happen with them.
	// out is intentionally not deallocated here, rather we leave it allocated until our
	// process completes, as it contains the various string values used in our node tree
	// (similarly, memory allocated for the node tree is not released by us)
	ret = trampoline(&out, NS_SB, "getfs", 0);
	if (ret != 0) {
		json_object_put(out);
		return ret;
	}

	root.parent = &root;

	int len = json_object_array_length(out);
	for (int i = 0; i < len; ++i) {
		build_tree(json_object_array_get_idx(out, i), &root);
	}

	// Our child expects a string containing the virtual python path, so give that too
	// it's prefixed by an int containing the string length.
	ret = trampoline(&out, NS_SB, "getpythonpath", 0);
	if (ret != 0) {
		json_object_put(out);
		return ret;
	}

	struct iovec request[3];
	len = json_object_get_string_len(out);
	request[0].iov_base = &len;
	request[0].iov_len = sizeof(len);
	request[1].iov_base = (void *)json_object_get_string(out);
	request[1].iov_len = (size_t)len;

	ret = writev(child_socket, request, 2);
	if ((size_t)ret != len + sizeof(len)) {
		debug_error("Unable to send python path data to child.\n");
		return -1;
	}

	// set up known fds (stdin/stdout/stderr)
	// these are always forwarded to the parent to handle
	// fd 3 is used by child to communicate with us, so nothing should ever happen on that fd
	fds[0].realfd = -1;
	fds[0].node = &sb_stdin;
	fds[1].realfd = -2;
	fds[1].node = &sb_stdout;
	fds[2].realfd = -3;
	fds[2].flags = &sb_stderr;

	/* run in loop until child terminates, handling requests from child and proxying to
	 * our parent if necessary (note: child termination is handled via SIGCHLD handler).
	 * child requests are sent matching the following structure:
	 * struct child_request {
	 *     int16_t namespace; -- NS_* constant (not NS_SYS, see below)
	 *     uint16_t fnamelen;
	 *     uint16_t arglen;
	 *     char fname[]; -- Must be NULL terminated
	 *     char args[]; -- JSON array of arguments
	 * };
	 * for NS_SYS:
	 * struct child_request {
	 *     int16_t namespace; -- NS_SYS
	 *     uint16_t syscall;
	 *     uint16_t arglen;
	 *     char args[]; -- of length arglen, each arg is tightly packed; strings null terminated
	 * };
	 */
	int16_t namespace;
	uint16_t fnamelen, length;
	void *params[6];
	char buf[65537];
	for (;;) {
		request[0].iov_base = &namespace;
		request[0].iov_len = 2;
		request[1].iov_base = &fnamelen;
		request[1].iov_len = 2;
		request[2].iov_base = &length;
		request[2].iov_len = 2;
		ret = readv(child_socket, request, 3);
		if (ret != 4) {
			debug_error("Unable to read request from child.\n");
			goto fail;
		}

		memset(buf, 0, 65537);
		if (namespace == NS_SYS) {
			// this is something we are meant to handle ourselves, most likely
			// fnamelen contains the syscall number (should be less than nsyscalls)
			// and we can look up the handler in sys_arg_map.func
			if (fnamelen >= nsyscalls) {
				// invalid syscall number, likely malicious input
				debug_error("Invalid syscall number.\n");
				goto fail;
			}

			// read argument data into buf
			if (length > 0) {
				ret = read(child_socket, buf, length);
				if (ret != length) {
					debug_error("Unable to read request from child.\n");
					goto fail;
				}
			}

			// find the handler function for this syscall and then call it
			const char *syscall = syscalls[fnamelen];
			const struct sys_arg_map *map;
			for (map = arg_map; map->sys == NULL || !strcmp(map->sys, syscall); ++map)
				/* nothing */;
			if (map->sys == NULL) {
				debug_error("Syscall %s not implemented.\n", syscall);
				goto fail;
			}

			size_t arg_off = 0;
			for (int i = 0; i < map->nargs; ++i) {
				if (arg_off >= 65536) {
					ret = -1;
					debug_error("Ran out of space for arguments.\n");
					goto fail;
				}

				params[i] = (void *)(buf + arg_off);
				if (map->arglen[i] == 0) {
					arg_off += strlen((char *)params[i]) + 1;
				} else if (map->arglen[i] > 0) {
					arg_off += map->arglen[i];
				}
			}

			// dispatch rewrites buf (now leads with an int length followed by length bytes of output params)
			// note that params[0] also points to the beginning of buf; used here since attempting to recast
			// a char[] breaks strict-aliasing whereas casting void * does not.
			ret = dispatch(map->func, params[0], params[1], params[2], params[3], params[4], params[5]);
			struct iovec response[3];
			response[0].iov_base = &ret;
			response[0].iov_len = sizeof(int);
			response[1].iov_base = &errno;
			response[1].iov_len = sizeof(int);
			response[2].iov_base = buf + sizeof(int);
			response[2].iov_len = *((int *)params[0]);

			ret = writev(child_socket, response, 3);
			if ((size_t)ret != sizeof(int) + sizeof(int) + response[2].iov_len) {
				debug_error("Unable to write response to child.\n");
				goto fail;
			}
		} else {
			// punt this up to our parent and then return the response (as a json blob)
			if (fnamelen + length > 65536) {
				debug_error("Ran out of space for arguments.\n");
				goto fail;
			}
			ret = read(child_socket, buf, fnamelen + length);
			if (ret != fnamelen + length) {
				ret = -1;
				debug_error("Unable to read data from child.\n");
				goto fail;
			}

			json_object *json_args = json_tokener_parse(buf + fnamelen);
			if (!json_object_is_type(json_args, json_type_array)) {
				debug_error("Argument data is not an array.\n");
				goto fail;
			}

			ret = trampoline(&out, namespace, buf, -1, json_args);
		}
	}

fail:
	// getting here means that we aborted the loop before the child died, so kill the child now
	kill(child_pid, SIGTERM);
	return ret;
}

/* Gets a node from the given path, which may be either relative or absolute.
 * If relative, it is retrived based on the current working directory.
 * (The current working directory is specified by our parent).
 * The node must not be free()d, and its values are not guaranteed to remain
 * the same after another call to get_node (e.g. it may be a static buffer) */
static struct sbfs_node *get_node(const char *path)
{
	struct sbfs_node *cur = &root;
	json_object *out = NULL;
	int ret;

	if (path[0] != '/') {
		// get cwd from parent
		out = NULL;
		ret = trampoline(&out, NS_SYS, "getcwd", 0);
		if (ret != 0) {
			json_object_put(out);
			return NULL;
		}

		cur = get_node(json_object_get_string(out));
		json_object_put(out);
		if (cur == NULL) {
			return NULL;
		}
	}

	// make a copy of path so we can do stuff with it
	char *ourpath = strdup(path);
	for (char *name = strtok(ourpath, "/"); name != NULL; name = strtok(NULL, "/")) {
		if (name[0] == '\0' || !strcmp(name, "."))
			continue;

		if (!strcmp(name, "..")) {
			cur = cur->parent;
			continue;
		}

		if (cur->flags & SBFS_PROXY) {
			json_object *cur_name = json_object_new_string(cur->name);
			json_object *cur_realpath = json_object_new_string(cur->realpath);
			json_object *json_name = json_object_new_string(name);
			json_object *json_path = json_object_new_string(path);
			out = NULL;
			ret = trampoline(&out, NS_SB, "getnode", 4, cur_name, cur_realpath, json_name, json_path);
			if (ret != 0) {
				json_object_put(out);
				cur = NULL;
				goto cleanup;
			}

			if (proxy.child != NULL) {
				free(proxy.child);
				proxy.child = NULL;
			}

			// using build_tree is a convenience so we don't replicate code, out should NOT contain
			// any children, as that would cause memory leaks if it did.
			build_tree(out, &proxy);
			cur = proxy.child;
			continue;
		}

		// check for children with the given name, even if this is a real directory
		// this allows for virtual nodes or other real nodes to shadow what the real fs has
		for (struct sbfs_node *child = cur->child; child != NULL; child = child->next) {
			if (!strcmp(name, child->name)) {
				cur = child;
				goto found;
			}
		}

		// no children; check for a real file or directory with our name
		if (cur->realpath != NULL && (cur->flags & SBFS_RECURSE)) {
			// Before we get around to actually checking the filesystem, first check our blacklist/whitelist
			// for our name. If we aren't allowed to read it, don't bother seeing if it exists.
			if (cur->filter != NULL) {
				int i;

				for (i = 0; cur->filter[i] != NULL; ++i) {
					// our filter may have nested subdirectories, ensure we only match the topmost one
					char *filter = strdup(cur->filter[i]);
					char *slash = strchr(filter, '/');
					if (slash != NULL)
						*slash = '\0';

					if (!fnmatch(filter, name, FNM_PERIOD | FNM_EXTMATCH)) {
						if (cur->flags & SBFS_BLACKLIST) {
							errno = ENOENT;
							cur = NULL;
							free(filter);
							goto cleanup;
						} else {
							// matched whitelist entry
							free(filter);
							break;
						}
					}

					free(filter);
				}

				if ((cur->flags & SBFS_BLACKLIST) == 0 && cur->filter[i] == NULL) {
					// no entries matched and we are using a whitelist
					errno = ENOENT;
					cur = NULL;
					goto cleanup;
				}

				// getting here means we either matched a whitelist entry or did not match a blacklist entry
				// either way execution should proceed
			}

			DIR *dir = opendir(cur->realpath);
			if (dir == NULL) {
				cur = NULL;
				goto cleanup;
			}

			struct sbfs_node *newcur = NULL;
			for (struct dirent *dent = readdir(dir); dent != NULL; dent = readdir(dir)) {
				if (strcoll(dent->d_name, name))
					continue;

				size_t buflen = strlen(cur->realpath) + strlen(name) + 2;
				char *buf = (char *)malloc(buflen);
				strcpy(buf, cur->realpath);
				strcat(buf, "/");
				strcat(buf, name);

				free(proxy.name);
				free(proxy.realpath);
				free(proxy.filter);
				proxy.name = strdup(name);
				proxy.realpath = buf;
				proxy.flags = cur->flags & ~SBFS_DIRECTORY;
				proxy.filter = NULL;

				struct stat statres;
				ret = lstat(proxy.realpath, &statres);
				if (ret < 0) {
					debug_error("Cannot stat %s: %s", proxy.realpath, strerror(errno));
					cur = NULL;
					goto cleanup;
				}

				if (S_ISLNK(statres.st_mode)) {
					if (!(cur->flags & SBFS_FOLLOW)) {
						newcur = NULL;
						break;
					}

					ret = stat(proxy.realpath, &statres);
					if (ret < 0) {
						debug_error("Cannot stat %s: %s", proxy.realpath, strerror(errno));
						cur = NULL;
						goto cleanup;
					}
				}

				if (S_ISDIR(statres.st_mode)) {
					proxy.flags |= SBFS_DIRECTORY;
				}

				// copy over matching filters, advanced by one directory
				if (cur->filter != NULL) {
					int fbuflen, i = 0;
					char **fbuf;
					for (fbuflen = 0; cur->filter[fbuflen] != NULL; ++fbuflen)
						/* nothing */;

					fbuf = (char **)malloc(fbuflen * sizeof(char *));
					for (int j = 0; j < fbuflen; ++j) {
						char *filter = strdup(cur->filter[j]);
						char *slash = strchr(filter, '/');
						if (slash != NULL)
							*slash = '\0';

						if (!fnmatch(filter, name, FNM_PERIOD | FNM_EXTMATCH)) {
							slash = strchr(cur->filter[j], '/');
							if (slash != NULL) {
								fbuf[i] = slash + 1;
								++i;
							}
						}

						free(filter);
					}

					fbuf[i] = NULL;
					proxy.filter = fbuf;
				}

				newcur = &proxy;
				break;
			}

			cur = newcur;
			closedir(dir);
		}

found:
		/* nothing */;
	}

cleanup:
	free(ourpath);
	return cur;
}

static void build_tree(json_object *json, struct sbfs_node *parent)
{
	/* getfs data format: all fields optional except name; if unspecified a default
	 * of either null, false, or an empty array will be assumed. node is a single element
	 * of the array (which may have a child array filled with similar nodes).
	 * [{
	 *   "name": string virtual name,
	 *   "realpath": string real path or null for virtual file
	 *   "children": [
	 *     array of objects in the same format, empty array for no children
	 *   ]
	 *   "follow": bool whether or not to follow symlinks
	 *   "recurse": bool whether or not to recurse into real subdirectories
	 *   "dir": bool whether or not this is a directory (ignored if realpath is not null)
	 *   "filter": [
	 *     array of shell wildcard patterns to use as a whitelist or blacklist
	 *     for any real files/dirs under us. these are passed to fnmatch() with
	 *     the FNM_PERIOD and FNM_EXTMATCH flags
	 *   ]
	 *   "blacklist": bool if filter is a blacklist (true) or whitelist (false)
	 *   "proxy": bool if any requests are supposed to be proxied to parent,
	 *     useful for virtual directories whose contents are not known during init
	 *   "writable": bool if writing is allowed to this node or real files/subdirs;
	 *     for real files/subdirs writing must also be allowed by filesystem permissions
	 * },
	 * ...
	 * ]
	 */
	
	json_object *temp;
	struct sbfs_node *node = (struct sbfs_node *)malloc(sizeof(struct sbfs_node));
	int len = 0;

	// set up links; we prepend this node in front of previous first child so that insertion
	// is O(1) rather than O(N). This does mean nodes are enumerated in reverse order, however
	// that should not matter for any practical purpose.
	node->parent = parent;
	node->next = parent->child;
	parent->child = node;

	json_object_object_get_ex(json, "name", &temp);
	node->name = (char *)json_object_get_string(temp);

	if (json_object_object_get_ex(json, "realpath", &temp) && json_object_is_type(temp, json_type_string)) {
		node->realpath = (char *)json_object_get_string(temp);
		struct stat statres;
		int ret = stat(node->realpath, &statres);
		if (ret == -1) {
			debug_error("Unable to stat %s: %s", node->realpath, strerror(errno));
			exit(1);
		}

		if (S_ISDIR(statres.st_mode)) {
			node->flags |= SBFS_DIRECTORY;
		}
	} else if (json_object_object_get_ex(json, "dir", &temp) && json_object_get_boolean(temp)) {
		node->flags |= SBFS_DIRECTORY;
	}

	if (json_object_object_get_ex(json, "follow", &temp) && json_object_get_boolean(temp)) {
		node->flags |= SBFS_FOLLOW;
	}

	if (json_object_object_get_ex(json, "recurse", &temp) && json_object_get_boolean(temp)) {
		node->flags |= SBFS_RECURSE;
	}

	if (json_object_object_get_ex(json, "blacklist", &temp) && json_object_get_boolean(temp)) {
		node->flags |= SBFS_BLACKLIST;
	}

	if (json_object_object_get_ex(json, "proxy", &temp) && json_object_get_boolean(temp)) {
		node->flags |= SBFS_PROXY;
	}

	if (json_object_object_get_ex(json, "writable", &temp) && json_object_get_boolean(temp)) {
		node->flags |= SBFS_WRITABLE;
	}

	if (json_object_object_get_ex(json, "filter", &temp)) {
		len = json_object_array_length(temp);
		if (len > 0) {
			node->filter = (char **)malloc(len * sizeof(char *));
			for (int i = 0; i < len; ++i) {
				json_object *filter = json_object_array_get_idx(temp, i);
				node->filter[i] = (char *)json_object_get_string(filter);
			}
		}
	}

	if ((node->flags & SBFS_DIRECTORY) && json_object_object_get_ex(json, "children", &temp)) {
		len = json_object_array_length(temp);
		for (int i = 0; i < len; ++i) {
			json_object *child = json_object_array_get_idx(temp, i);
			build_tree(child, node);
		}
	}
}

int open_node(const char *pathname, int flags, int mode)
{
	struct sbfs_node *node = get_node(pathname);
	if (node == NULL) {
		// does not exist
		if (flags & O_CREAT) {
			errno = EROFS;
		} else {
			errno = ENOENT;
		}

		return -1;
	}

	if ((node->flags & SBFS_DIRECTORY) && (mode & (O_WRONLY | O_RDWR))) {
		errno = EISDIR;
		return -1;
	}

	if (mode & (O_CREAT | O_EXCL)) {
		errno = EEXIST;
		return -1;
	}

	if (!(node->flags & SBFS_WRITABLE) && (mode & (O_WRONLY | O_RDWR))) {
		errno = EROFS;
		return -1;
	}

	if ((node->flags & SBFS_DIRECTORY) && !(mode & O_DIRECTORY)) {
		errno = ENOTDIR;
		return -1;
	}

	int realfd = -1;
	if (node->realpath != NULL) {
		realfd = open(node->realpath, flags, mode);
		if (realfd == -1) {
			return -1;
		}
	} else {
		// trampoline open request to parent to get a virtual (negative) fd
		// note that the parent returns a postive fd and we make it negative
		// e.g. if it reports fd 3 then we negate and subtract one for -4
		// when we pass the fd back to parent we add one and negate to get 3 back
		// TODO: this
	}

	int i;
	for (i = 4; i < MAX_FDS; ++i) {
		if (fds[i].realfd == 0) {
			struct sbfs_node *newnode = calloc(1, sizeof(struct sbfs_node));
			newnode->name = strdup(node->name);
			newnode->flags = node->flags;
			if (node->realpath != NULL) {
				newnode->realpath= strdup(node->realpath);
			}

			if (realfd < 0 && (flags & O_CLOEXEC)) {
				newnodw->flags |= SBFS_CLOEXEC;
			}

			fds[i].realfd = realfd;
			fds[i].node = newnode;
			break;
		}
	}

	if (i == MAX_FDS) {
		errno = EMFILE;
		return -1;
	}

	return i;
}
