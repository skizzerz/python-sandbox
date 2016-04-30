<?php

namespace PythonSandbox;

class VirtualFS {
	protected $root = null;
	protected $fds = [];
	protected $cwd = '/tmp';

	public function __construct( $pyBin, $pyLib, $sbLib ) {
		$this->root = new VirtualDir( $this, '' );
		$this->root[] = new VirtualDir( $this, 'bin' );
		$this->root['bin'][] = new RealFile( $this, 'python', $pyBin );
		$this->root[] = new VirtualDir( $this, 'lib' );
		$this->root['lib'][] = new RealDir( $this, 'python', $pyLib,
			[ 'recurse' => true, 'fileWhitelist' => [ '*.py' ] ] );
		$this->root['lib'][] = new RealDir( $this, 'sandbox', $sbLib,
			[ 'recurse' => true, 'fileWhitelist' => [ '*.py' ] ] );
		$this->root[] = new VirtualDir($this, 'tmp');
		$this->root[] = new RealDir( $this, 'dev', '/dev',
			[ 'fileWhitelist' => [ 'urandom' ] ] );
	}

	protected function getNode( $path ) {
		if ( $path[0] === '/' ) {
			$path = $this->cwd . '/' . $path;
		}

		$pp = explode( '/', $path );
		$np = [];

		foreach ( $pp as $p ) {
			if ( $p === '' || $p === '.' ) {
				continue;
			} elseif ( $p === '..' ) {
				array_pop( $np );
			} else {
				$np[] = $p;
			}
		}

		$path = '/' . implode( '/', $np );

		// $path is now normalized into an absolute virtual path (all ./ and ../ resolved)
		$node = $this->root;
		foreach ($np as $p) {
			if ( !( $node instanceOf DirBase ) ) {
				throw new SyscallException( ENOTDIR );
			} elseif ( isset( $node[$p] ) ) {
				$node = $node[$p];
			} else {
				return null;
			}
		}

		return $node;
	}

	public function open( $path, $flags, $mode ) {
		$node = $this->getNode( $path );

		if ($node === null) {
			// file does not exist, if write requested throw EROFS instead of ENOENT
			if ( ( $flags & O_CREAT ) && ( $mode & 0222 ) ) {
				throw new SyscallException( EROFS );
			}

			throw new SyscallException( ENOENT );
		} elseif ( $flags & ( O_CREAT | O_EXCL ) ) {
			// file exists and we're supposed to be creating it only
			throw new SyscallException( EEXIST );
		} elseif ( $flags & ( O_WRONLY | O_RDWR ) ) {
			// write requested, which is not allowed
			throw new SyscallException( EROFS );
		}

		$maxfds = Configuration::singleton()->get( 'MaxFDs' );
		for ( $i = 5; $i < $maxfds; ++$i ) {
			if ( !isset( $this->fds[$i] ) ) {
				$this->fds[$i] = $node->open( $flags, $mode );
				return $i;
			}
		}

		throw new SyscallException( EMFILE );
	}

	public function close( $fd ) {
		if ( $fd >= 0 && $fd <= 4 ) {
			throw new SyscallException( EPERM );
		} elseif ( !isset( $this->fds[$fd] ) ) {
			throw new SyscallException( EBADF );
		}

		$this->fds[$fd]->close();
		unset( $this->fds[$fd] );
	}

	public function read( $fd, $length ) {
		$maxlen = Configuration::singleton()->get( 'MaxReadLength' );
		if ( $length > $maxlen ) {
			throw new SyscallException( EIO );
		} elseif ( $fd >= 0 && $fd <= 4 ) {
			throw new SyscallException( EPERM );
		} elseif ( !isset( $this->fds[$fd] ) ) {
			throw new SyscallException( EBADF );
		}

		return $this->fds[$fd]->read( $length );
	}
}
