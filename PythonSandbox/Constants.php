<?php

namespace PythonSandbox;

// uid/gid of the sandbox (fake)
// these are also in sandbox-preload.c
const SB_UID = 1000;
const SB_GID = 1000;

// built-in sandbox namespaces for RPCs. Additional namespaces
// can be registered in extensions via the configuration.
const NS_SYS = 0; // syscall
const NS_SB  = 1; // sandbox
const NS_MW  = 2; // mediawiki

// errno constants
const EPERM     = 1;  // Operation not permitted
const ENOENT    = 2;  // No such file or directory
const EIO       = 5;  // I/O error
const EBADF     = 9;  // Bad file number
const EACCES    = 13; // Permission denied
const EEXIST    = 17; // File exists
const ENOTDIR   = 20; // Not a directory
const EISDIR    = 21; // Is a directory
const EINVAL    = 22; // Invalid argument
const EMFILE    = 24; // Too many open files
const ENOSPC    = 28; // No space left on device
const EROFS     = 30; // Read-only file system
const ENOSYS    = 38; // Function not implemented
const ELOOP     = 40; // Too many symbolic links encountered
const EOVERFLOW = 75; // Value too large for defined data type

// file modes
// these flags aren't necessarily the same across every OS
// the below are valid for most if not all linuxes
const O_RDONLY    = 000000000;
const O_WRONLY    = 000000001;
const O_RDWR      = 000000002;
const O_CREAT     = 000000100;
const O_EXCL      = 000000200;
const O_NOCTTY    = 000000400;
const O_TRUNC     = 000001000;
const O_APPEND    = 000002000;
const O_NONBLOCK  = 000004000;
const O_DSYNC     = 000010000;
const O_DIRECT    = 000040000;
const O_LARGEFILE = 000100000;
const O_DIRECTORY = 000200000;
const O_NOFOLLOW  = 000400000;
const O_NOATIME   = 001000000;
const O_CLOEXEC   = 002000000;
const O_SYNC      = 004010000; // __O_SYNC | O_DSYNC
const O_PATH      = 010000000;
const O_TMPFILE   = 020200000; // __O_TMPFILE | O_DIRECTORY

// file descriptor flags
const FD_CLOEXEC = 1;

// openat stuff
const AT_FDCWD = -100;

// node permissions (files/directories)
const S_IFMT   = 0170000; // bit mask for the file type bit field
const S_IFSOCK = 0140000; // socket
const S_IFLNK  = 0120000; // symbolic link
const S_IFREG  = 0100000; // regular file
const S_IFBLK  = 0060000; // block device
const S_IFDIR  = 0040000; // directory
const S_IFCHR  = 0020000; // character device
const S_IFIFO  = 0010000; // FIFO
const S_ISUID  = 0004000; // setuid
const S_ISGID  = 0002000; // setgid
const S_ISVTX  = 0001000; // sticky bit
const S_IRWXU  = 0000700; // orwx
const S_IRUSR  = 0000400; // ur--
const S_IWUSR  = 0000200; // u-w-
const S_IXUSR  = 0000100; // u--x
const S_IRWXG  = 0000070; // grwx
const S_IRGRP  = 0000040; // gr--
const S_IWGRP  = 0000020; // g-w-
const S_IXGRP  = 0000010; // g--x
const S_IRWXO  = 0000007; // orwx
const S_IROTH  = 0000004; // or--
const S_IWOTH  = 0000002; // o-w-
const S_IXOTH  = 0000001; // o--x

// fcntl commands
const F_DUPFD         = 0;
const F_GETFD         = 1;
const F_SETFD         = 2;
const F_GETFL         = 3;
const F_SETFL         = 4;
const F_GETLK         = 5;
const F_SETLK         = 6;
const F_SETLKW        = 7;
const F_SETOWN        = 8;
const F_GETOWN        = 9;
const F_SETSIG        = 10;
const F_GETSIG        = 11;
const F_GETLK64       = 12;
const F_SETLK64       = 13;
const F_SETLKW64      = 14;
const F_SETOWN_EX     = 15;
const F_GETOWN_EX     = 16;
const F_OFD_GETLK     = 36;
const F_OFD_SETLK     = 37;
const F_OFD_SETLKW    = 38;
const F_SETLEASE      = 1024;
const F_GETLEASE      = 1025;
const F_NOTIFY        = 1026;
const F_DUPFD_CLOEXEC = 1030;
const F_SETPIPE_SZ    = 1031;
const F_GETPIPE_SZ    = 1032;
