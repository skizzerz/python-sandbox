<?php

namespace PythonSandbox;

class SyscallHandler {
	protected $sb;

	public function __construct( Sandbox $sb ) {
		$this->sb = $sb;
	}

	public function open( $path, $flags, $mode = 0 ) {
		return $this->sb->getfs()->open( $path, $flags, $mode, AT_FDCWD );
	}

	public function openat( $fd, $path, $flags, $mode = 0 ) {
		return $this->sb->getfs()->open( $path, $flags, $mode, $fd );
	}

	public function fcntl( $fd, $cmd, $arg = null ) {
		switch ( $cmd ) {
		case F_GETFD:
			return $this->sb->getfs()->getFlags( $fd );
		case F_GETFL:
			return $this->sb->getfs()->getMode( $fd );
		case F_DUPFD:
		case F_DUPFD_CLOEXEC:
			return $this->sb->getfs()->dup( $fd, $arg, /* exactFd */ false );
		case F_SETFD:
			$this->sb->getfs()->setFlags( $fd, $arg );
			return 0;
		case F_SETFL:
			$this->sb->getfs()->setMode( $fd, $arg );
			return 0;
		case F_GETOWN:
		case F_GETSIG:
		case F_GETLEASE:
		case F_GETPIPE_SZ:
		case F_GET_SEALS:
		case F_SETLK:
		case F_SETLKW:
		case F_GETLK:
		case F_OFD_SETLK:
		case F_OFD_SETLKW:
		case F_OFD_GETLK:
		case F_SETOWN:
		case F_GETOWN_EX:
		case F_SETOWN_EX:
		case F_SETSIG:
		case F_SETLEASE:
		case F_NOTIFY:
		case F_SETPIPE_SZ:
		case F_ADD_SEALS:
		default:
			// invalid or unsupported $cmd
			throw new SyscallException( EINVAL );
		}
	}

	public function dup( $oldfd ) {
		return $this->sb->getfs()->dup( $oldfd );
	}

	public function close( $fd ) {
		$this->sb->getfs()->close( $fd );
		return 0;
	}

	public function read( $fd, $length ) {
		$str = $this->sb->getfs()->read( $fd, $length );
		return [ strlen( $str ), $str ];
	}

	public function stat( $path ) {
		$res = $this->sb->getfs()->stat( $path );
		return [ 0, $res ];
	}

	public function lstat( $path ) {
		// our virtualized fs does not support symlinks
		$res = $this->sb->getfs()->stat( $path );
		return [ 0, $res ];
	}

	public function fstat( $fd ) {
		$res = $this->sb->getfs()->fstat( $fd );

		return [ 0, $res ];
	}

	public function readlink( $path ) {
		if ( !$this->sb->getfs()->access( $path, POSIX_F_OK ) ) {
			throw new SyscallException( ENOENT );
		}

		// our virtualized fs does not support symlinks
		throw new SyscallException( EINVAL, 'The named file is not a symbolic link.' );
	}

	public function getdents( $fd, $bufsize, $structBytes ) {
		$arr = $this->sb->getfs()->getdents( $fd, $bufsize, $structBytes );

		// the real getdents() syscall returns bytes whereas we report array size
		// the child sandbox will correctly compute the actual return value, and only
		// uses ours to check for error (below 0), end of directory (0), or data (above zero).
		return [ count( $arr ), $arr ];
	}

	public function lseek( $fd, $offset, $whence ) {
		return $this->sb->getfs()->seek( $fd, $offset, $whence );
	}

	public function getuid() {
		return SB_UID;
	}

	public function geteuid() {
		return SB_UID;
	}

	public function getgid() {
		return SB_GID;
	}

	public function getegid() {
		return SB_GID;
	}
}
