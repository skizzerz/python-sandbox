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

	public function access( $path, $mode ) {
		if ( !$this->sb->getfs()->access( $path, $mode ) ) {
			throw new SyscallException( EACCES );
		} elseif ( $mode & W_OK ) {
			throw new SyscallException( EROFS );
		}

		return 0;
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

	public function statfs( $path ) {
		// SELinux calls this, not sure what else. File an issue on github if
		// a real implementation of this is important since PHP cannot actually
		// call the real statfs(2).
		throw new SyscallException( ENOSYS );
	}

	public function poll( $fds, $timeout ) {
		$r = [];
		$w = [];
		$x = [];

		$ret = [];

		foreach ( $fds as $i => $fd ) {
			$f = $this->sb->getfs()->getfd( $fd->fd );
			$ret[$i] = 0;

			if ( $f === null && $fd['fd'] >= 0 ) {
				$ret[$i] = POLLNVAL;
			} elseif ( $f instanceOf VirtualFD && ( $fd->events & POLLIN ) && $f->canRead() ) {
				$ret[$i] = POLLIN;
			} elseif ( $f instanceOf RealFD ) {
				$fh = $f->getfh();

				if ( $fd->events & POLLIN ) {
					$r[$i] = $fh;
				}

				if ( $fd->events & POLLOUT ) {
					$w[$i] = $fh;
				}

				if ( $fd->events & POLLPRI ) {
					$x[$i] = $fh;
				}
			}
		}

		if ( count( $r ) + count( $w ) + count( $x ) > 0 ) {
			if ( $timeout > 5000 || $timeout < 0 ) {
				$ts = 5;
				$tu = 0;
			} else {
				$ts = (int)( $timeout / 1000 );
				$tu = ( $timeout % 1000 ) * 1000;
			}

			if ( stream_select( $r, $w, $x, $ts, $tu ) === false ) {
				throw new SyscallException( EINTR );
			}

			foreach ( $r as $i => $fh ) {
				$ret[$i] |= POLLIN;
			}

			foreach ( $w as $i => $fh ) {
				$ret[$i] |= POLLOUT;
			}

			foreach ( $x as $i => $fh ) {
				$ret[$i] |= POLLPRI;
			}
		}

		$code = array_reduce( $ret, function ( $y, $x ) { return $y + ( $x > 0 ); }, 0 );

		return [ $code, $ret ];
	}
}
