<?php

namespace PythonSandbox;

class SyscallHandler {
	protected $sb;
	protected $rpipe;
	protected $wpipe;

	public function __construct( Sandbox $sb, $rpipe, $wpipe ) {
		$this->sb = $sb;
		$this->rpipe = $rpipe;
		$this->wpipe = $wpipe;
	}

	public function run() {
		$self = new \ReflectionObject( $this );

		// this function loops until the child proc finishes or we get an exception
		// (other than SyscallException which indicate we should pass error down to child).
		while ( true ) {
			// marshal format is line based, each line has one json object
			// input: {"name": "fname", "args": [...]}
			// response: {"code": 0, "errno": 0, "data": ...}

			$r = [ $this->rpipe ];
			$w = [];
			$x = [];

			if ( !stream_select( $r, $w, $x, 5 ) ) {
				echo "Read timeout.\n";
				break;
			}

			$line = fgets( $this->rpipe );
			if ( $line === false ) {
				break;
			}

			echo "<<< $line";

			$call = json_decode( $line, false, 32, JSON_BIGINT_AS_STRING );
			if ( $call === null || !isset( $call->name ) || !isset( $call->args ) || !is_array( $call->args ) ) {
				echo "Invalid JSON.\n";
				break;
			} elseif( !preg_match( '/^[a-z0-9_]{1,32}$/', $call->name ) ) {
				// paranoia check. Just in case there's some weird exploit with Reflection that could allow
				// a carefully crafted method name to call arbitrary code, we ensure that whatever name
				// we receive will form a valid PHP method name.
				echo "Invalid syscall name.\n";
				break;
			}

			$raw = isset( $call->raw ) && $call->raw;
			$nargs = count( $call->args );
			$method = "sys__{$call->name}__{$nargs}";
			if ( $self->hasMethod( $method ) ) {
				try {
					$m = $self->getMethod( $method );
					$ret = $m->invokeArgs( $this, $call->args );
					$errno = 0;
					$data = false;

					if ( is_array( $ret ) ) {
						list( $ret, $data ) = $ret;
					}
				} catch ( SyscallException $e ) {
					$ret = -1;
					$errno = $e->getCode();
					$data = $e->getMessage();
				}

				if ( $raw ) {
					if ( $data instanceOf StatResult ) {
						$ret = "$ret $errno {$data->getRaw()}";
					} else {
						$data = base64_encode( $data );
						$ret = "$ret $errno $data";
					}
				} else {
					if ( $data instanceOf StatResult ) {
						$data = $data->getArray();
					}

					$json = json_encode( [
						'code' => $ret,
						'errno' => $errno,
						'data' => $data
					] );

					if ( $json === false ) {
						// $data is a binary string
						$json = json_encode( [
							'code' => $ret,
							'errno' => $errno,
							'data' => base64_encode( $data ),
							'base64' => true
						] );
					}

					$ret = $json;
				}

				echo ">>> $ret\n";

				$r = [];
				$w = [ $this->wpipe ];
				$x = [];

				if ( !stream_select( $r, $w, $x, 5 ) ) {
					echo "Write timeout.\n";
					break;
				}

				fprintf( $this->wpipe, "%s\n", $ret );
			} else {
				echo "No such method $method.\n";
				// Eventually we'll remove this return, but quitting early makes development faster
				return;

				$ret = json_encode( [
					'code' => -1,
					'errno' => ENOSYS
				] );

				echo ">>> $ret\n";

				$r = [];
				$w = [ $this->wpipe ];
				$x = [];

				if ( !stream_select( $r, $w, $x, 5 ) ) {
					echo "Write timeout.\n";
					break;
				}

				fprintf( $this->wpipe, "%s\n", $ret );
			}
		}
	}

	public function sys__open__2( $path, $flags ) {
		return $this->sb->getfs()->open( $path, $flags, 0, AT_FDCWD );
	}

	public function sys__open__3( $path, $flags, $mode ) {
		return $this->sb->getfs()->open( $path, $flags, $mode, AT_FDCWD );
	}

	public function sys__openat__3( $fd, $path, $flags ) {
		return $this->sb->getfs()->open( $path, $flags, 0, $fd );
	}

	public function sys__openat__4( $fd, $path, $flags, $mode ) {
		return $this->sb->getfs()->open( $path, $flags, $mode, $fd );
	}

	public function sys__fcntl__2( $fd, $cmd ) {
		switch ( $cmd ) {
		case F_GETFD:
			return $this->sb->getfs()->getFlags( $fd );
		case F_GETFL:
			return $this->sb->getfs()->getMode( $fd );
		case F_GETOWN:
		case F_GETSIG:
		case F_GETLEASE:
		case F_GETPIPE_SZ:
		case F_GET_SEALS:
		default:
			// invalid or unsupported $cmd
			throw new SyscallException( EINVAL );
		}
	}

	public function sys__fcntl__3( $fd, $cmd, $arg ) {
		switch ( $cmd ) {
		case F_DUPFD:
		case F_DUPFD_CLOEXEC:
			return $this->sb->getfs()->dup( $fd, $arg, /* exactFd */ false );
		case F_SETFD:
			$this->sb->getfs()->setFlags( $fd, $arg );
			return 0;
		case F_SETFL:
			$this->sb->getfs()->setMode( $fd, $arg );
			return 0;
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

	public function sys__close__1( $fd ) {
		$this->sb->getfs()->close( $fd );
		return 0;
	}

	public function sys__read__2( $fd, $length ) {
		$str = $this->sb->getfs()->read( $fd, $length );
		return [ strlen( $str ), $str ];
	}

	public function sys__stat__1( $path ) {
		$res = $this->sb->getfs()->stat( $path );
		return [ 0, $res ];
	}

	public function sys__fstat__1( $fd ) {
		switch ( $fd ) {
		case 0:
			$res = new StatResult( fstat( STDIN ) );
			break;
		case 1:
			$res = new StatResult( fstat( STDOUT ) );
			break;
		case 2:
			$res = new StatResult( fstat( STDERR ) );
			break;
		default:
			$res = $this->sb->getfs()->fstat( $fd );
			break;
		}

		return [ 0, $res ];
	}

	public function sys__readlink__1( $path ) {
		if ( !$this->sb->getfs()->access( $path, POSIX_F_OK ) ) {
			throw new SyscallException( ENOENT );
		}

		// our virtualized fs does not support symlinks
		throw new SyscallException( EINVAL, 'The named file is not a symbolic link.' );
	}

	public function sys__getdents__3( $fd, $bufsize, $structBytes ) {
		$arr = $this->sb->getfs()->getdents( $fd, $bufsize, $structBytes );

		// the real getdents() syscall returns bytes whereas we report array size
		// the child sandbox will correctly compute the actual return value, and only
		// uses ours to check for error (below 0), end of directory (0), or data (above zero).
		return [ count( $arr ), $arr ];
	}
}
