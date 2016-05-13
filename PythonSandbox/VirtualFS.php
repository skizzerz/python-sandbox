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
			[ 'recurse' => true, 'followSymlinks' => true, 'fileWhitelist' => [ '*.py', '*.so' ] ] );
		$this->root['lib'][] = new RealDir( $this, 'sandbox', $sbLib,
			[ 'recurse' => true, 'fileWhitelist' => [ '*.py' ] ] );
		$this->root[] = new VirtualDir( $this, 'tmp' );
		$this->root[] = new RealDir( $this, 'dev', '/dev',
			[ 'fileWhitelist' => [ 'urandom' ] ] );
	}

	protected function getNode( $path, $base = AT_FDCWD ) {
		if ( $base === AT_FDCWD ) {
			$base = $this->cwd;
		} else {
			$this->validateFd( $base );
			$base = $this->fds[$base]->getNode()->getPath();
		}

		$path = SandboxUtil::normalizePath( $path, $base );
		$np = explode( '/', $path );
		array_shift( $np ); // path starts with / so np starts with ''
		$node = $this->root;

		foreach ( $np as $p ) {
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

	public function open( $path, $flags, $mode, $base ) {
		$node = $this->getNode( $path, $base );

		if ( $node === null ) {
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
		$this->validateFd( $fd );

		$this->fds[$fd]->close();
		unset( $this->fds[$fd] );
	}

	public function stat( $path ) {
		$node = $this->getNode( $path );

		if ( $node === null ) {
			throw new SyscallException( ENOENT );
		}

		return new StatResult( $node->stat() );
	}

	public function fstat( $fd ) {
		$this->validateFd( $fd );

		return new StatResult( $this->fds[$fd]->stat() );
	}

	public function access( $path, $mode ) {
		$node = $this->getNode( $path );

		if ( $node === null ) {
			throw new SyscallException( ENOENT );
		}

		return $node->access( $mode );
	}

	public function read( $fd, $length ) {
		$maxlen = Configuration::singleton()->get( 'MaxReadLength' );
		if ( $length > $maxlen ) {
			throw new SyscallException( EIO );
		}
		
		$this->validateFd( $fd );	

		return $this->fds[$fd]->read( $length );
	}

	public function validateFd( $fd ) {
		if ( $fd >= 0 && $fd <= 4 ) {
			throw new SyscallException( EPERM );
		} elseif ( !isset( $this->fds[$fd] ) ) {
			throw new SyscallException( EBADF );
		}
	}

	public function dup( $oldFd, $newFd = 0, $exactFd = false ) {
		$this->validateFd( $oldFd );

		$maxfds = Configuration::singleton()->get( 'MaxFDs' );
		if ( $newFd !== 0 && ( ( $newFd < 5 && $exactFd ) || $newFd >= $maxFds ) ) {
			throw new SyscallException( EINVAL );
		} elseif ( $newFd === 0 && $exactFd ) {
			throw new SyscallException( EINVAL );
		} elseif ( $newFd === 0 ) {
			$newFd = 5;
		}

		if ( $exactFd && isset( $this->fds[$newFd] ) ) {
			// this unsets $this->fds[$newFd] so that the loop below will work on the first
			// iteration, so we don't need to do special handling for $exactFd below.
			$this->close( $newFd );
		}

		for ( $i = $newFd; $i < $maxfds; ++$i ) {
			if ( !isset( $this->fds[$i] ) ) {
				$this->fds[$i] = &$this->fds[$oldFd];
				return $i;
			}
		}

		throw new SyscallException( EMFILE );
	}

	public function getdents( $fd, $bufsize, $structBytes ) {
		$this->validateFd( $fd );

		return $this->fds[$fd]->getdents( $bufsize, $structBytes );
	}

	public function getMode( $fd ) {
		$this->validateFd( $fd );

		return $this->fds[$fd]->getMode();
	}

	public function setMode( $fd, $mode ) {
		$this->validateFd( $fd );
		$this->fds[$fd]->setMode( $mode );
	}

	public function getFlags( $fd ) {
		$this->validateFd( $fd );

		return $this->fds[$fd]->getFlags();
	}

	public function setFlags( $fd, $flags ) {
		$this->validateFd( $fd );
		$this->fds[$fd]->setFlags( $flags );
	}
}
