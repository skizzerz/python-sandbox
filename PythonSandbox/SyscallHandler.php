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
			// binary format is 4 byte syscall name len, string syscall name, 1 byte number of arguments
			// for each argument: 4 byte argument len, then that many bytes for argument
			// response: 4 byte response code, if sending error then follow by 4 byte errno
			// some syscalls may stream additional response data on success

			$len = $this->readInt();
			$syscall = $this->readData( $len );
			$nargs = $this->readByte();
			$args = [];
			$dbgargs = [];

			for ( $i = 0; $i < $nargs; ++$i ) {
				$type = $this->readByte();
				$arglen = $this->readInt();
				$arg = $this->readData( $arglen, $type );
				$args[] = $arg;

				if ( $type === TYPE_STR ) {
					$dbgargs[] = "\"$arg\"";
				} else {
					$dbgargs[] = $arg;
				}
			}

			echo "<<< $syscall(" . implode( ', ', $dbgargs ) . ")\n";
			return;
		}
	}

	protected function readInt() {
		return unpack( 'iv', $this->readData( 4 ) )['v'];
	}

	protected function readByte() {
		return ord( $this->readData( 1 ) );
	}

	protected function readData( $length, $type = TYPE_STR ) {
		$read = 0;
		$tries = 0;
		$data = '';

		if ( $length <= 0 || $length > Configuration::singleton()->get( 'MaxReadLength' ) ) {
			throw new SandboxException( 'Invalid data length.' );
		}

		while ( true ) {
			// we wait a maximum of 5 seconds for data to be available on the pipe to read,
			// if that times out we terminate the sandbox due to a timeout
			$r = [ $this->rpipe ];
			$w = [];
			$x = [];

			$ret = stream_select( $r, $w, $x, 5 );
			if ( !$ret ) {
				throw new SandboxException( 'Error with select: timeout expired or other error.' );
			}

			$buf = fread( $this->rpipe, $length - $read );

			if ( $buf === false ) {
				throw new SandboxException( 'Unable to read from child: fread returned false.' );
			} elseif ( $buf === '' ) {
				if ( feof( $this->rpipe ) ) {
					throw new SandboxException( 'Unexpected EOF.' );
				}

				++$tries;
				if ( $tries > 3 ) {
					throw new SandboxException( 'Unable to read from child: Broken pipe.' );
				}
			}

			$data .= $buf;
			$read += strlen( $buf );

			if ( $read == $length ) {
				break;
			}
		}

		switch ( $type ) {
		case TYPE_STR:
			// no-op, already a string
			break;
		case TYPE_INT:
			switch ( $length ) {
			case 2:
				$data = unpack( 'sv', $data )['v'];
				break;
			case 4:
				$data = unpack( 'iv', $data )['v'];
				break;
			case 8:
				$i1 = unpack( 'iv', substr( $data, 0, 4 ) )['v'];
				$i2 = unpack( 'iv', substr( $data, 4 ) )['v'];
				$data = SandboxUtil::isLittleEndian() ? $i1 : $i2;
				break;
			default:
				throw new SandboxException( 'Invalid integer size.' );
			}
			break;
		case TYPE_UINT:
			switch ( $length ) {
			case 2:
				$data = unpack( 'Sv', $data )['v'];
				break;
			case 4:
				$data = unpack( 'Iv', $data )['v'];
				break;
			case 8:
				$i1 = unpack( 'Iv', substr( $data, 0, 4 ) )['v'];
				$i2 = unpack( 'Iv', substr( $data, 4 ) )['v'];
				$data = SandboxUtil::isLittleEndian() ? $i1 : $i2;
				break;
			default:
				throw new SandboxException( 'Invalid integer size.' );
			}
			break;
		case TYPE_FLT:
			switch ( $length ) {
			case 4:
				$data = unpack( 'fv', $data )['v'];
				break;
			case 8:
				$data = unpack( 'dv', $data )['v'];
				break;
			default:
				throw new SandboxException( 'Invalid float size.' );
			}
			break;
		default:
			throw new SandboxException( 'Invalid type.' );
		}

		return $data;
	}
}
