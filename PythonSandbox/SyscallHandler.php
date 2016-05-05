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
				echo "Broken pipe.\n";
				break;
			}

			echo "<<< $line";

			$call = json_decode( $line, false, 32, JSON_BIGINT_AS_STRING );
			if ( $call === null || !isset( $call->name ) || !isset( $call->args ) || !is_array( $call->args ) ) {
				echo "Invalid JSON.\n";
				break;
			}

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

				$ret = json_encode( [
					'code' => $ret,
					'errno' => $errno,
					'data' => $data
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
			} else {
				echo "No such method $method.\n";
				return;
			}
		}
	}

	public function sys__open__2( $path, $flags ) {
		return $this->sb->getfs()->open( $path, $flags, 0 );
	}

	public function sys__open__3( $path, $flags, $mode ) {
		return $this->sb->getfs()->open( $path, $flags, $mode );
	}

	public function sys__fcntl__2( $fd, $cmd ) {
		switch ( $cmd ) {
		case F_GETFD:
			$this->sb->getfs()->validateFd( $fd, true );
			return 0;
		case F_GETFL:
			return $this->sb->getfs()->getFlags( $fd );
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
			$this->sb->getfs()->validateFd( $fd, true );
			return 0;
		case F_SETFL:
			$this->sb->getfs()->setFlags( $fd, $arg );
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
}
