<?php

namespace PythonSandbox;

class VirtualFS {
	protected $app = null;
	protected $root = null;
	protected $fds = [];
	protected $cwd = '/tmp';

	public function __construct( Application $app ) {
		$this->app = $app;
		$pyVer = $app->getPythonVersion();
		$pyBase = $app->getPythonBasePath();
		$sbBase = $app->getSandboxBasePath();
		$config = $app->getConfigurationInstance();

		$pyVerDir = false;
		$libDir = 'lib';
		if ( file_exists( "$pyBase/lib64/$pyVer" ) ) {
			$pyVerDir = "$pyBase/lib64/$pyVer";
			$libDir = 'lib64';
		} elseif ( file_exists( "$pyBase/lib/$pyVer" ) ) {
			$pyVerDir = "$pyBase/lib/$pyVer";
		}

		if ( $pyVerDir === false ) {
			throw new SandboxException( 'Unable to find python directory in pyLib' );
		}

		$allowedPyLibs = $config->get( 'AllowedPythonLibs' );
		$allowedSysLibs = $config->get( 'AllowedSystemLibs' );

		// initialize directories required by the sandbox itself, the application will
		// have the ability to add to this afterwards
		$this->root = new VirtualDir( $this, '' );
		$this->root[] = new RealDir( $this, 'lib', '/lib',
			[ 'recurse' => true, 'followSymlinks' => true, 'fileWhitelist' => $allowedSysLibs ] );
		$this->root[] = new VirtualDir( $this, 'usr' );
		$this->root['usr'][] = new RealDir( $this, 'lib', '/usr/lib',
			[ 'recurse' => true, 'followSymlinks' => true, 'fileWhitelist' => $allowedSysLibs ] );
		if ( $libDir === 'lib64' ) {
			$this->root[] = new RealDir( $this, 'lib64', '/lib64',
				[ 'recurse' => true, 'followSymlinks' => true, 'fileWhitelist' => $allowedSysLibs ] );
			$this->root['usr'][] = new RealDir( $this, 'lib64', '/usr/lib64',
				[ 'recurse' => true, 'followSymlinks' => true, 'fileWhitelist' => $allowedSysLibs ] );
		}
		$this->root['usr'][] = new VirtualDir( $this, 'bin' );
		$this->root['usr']['bin'][] = new RealFile( $this, 'python', "$pyBase/bin/python3" );
		$this->root['usr'][$libDir][] = new RealDir( $this, $pyVer, $pyVerDir,
			[ 'recurse' => true, 'followSymlinks' => true, 'fileWhitelist' => $allowedPyLibs ] );
		$this->root['usr']['lib'][] = new RealDir( $this, 'sandbox', "$sbBase/lib",
			[ 'recurse' => true, 'fileWhitelist' => [ '*.py' ] ] );
		$this->root[] = new VirtualDir( $this, 'tmp' );
		$this->root['tmp'][] = $app->getInitScriptNode( $this, 'init.py' );
		$this->root['tmp'][] = $app->getUserScriptNode( $this, 'main.py' );
		$this->root[] = new RealDir( $this, 'dev', '/dev',
			[ 'fileWhitelist' => [ 'urandom' ] ] );

		// detect if we are in a virtualenv; if we are we need an orig-prefix.txt file
		// and another dir in lib that points to the real python installation, as virtualenv
		// does not include every base python library (such as json).
		if ( file_exists( "$pyVerDir/orig-prefix.txt" ) ) {
			$prefix = file_get_contents( "$pyVerDir/orig-prefix.txt" );
			$this->root[] = new VirtualDir( $this, 'venv' );
			$this->root['venv'][] = new VirtualDir( $this, $libDir );
			$this->root['venv'][$libDir][] = new RealDir( $this, $pyVer, "$prefix/$libDir/$pyVer",
				[ 'recurse' => true, 'followSymlinks' => true, 'fileWhitelist' => $allowedPyLibs ] );
			$this->root['usr'][$libDir][$pyVer][] =
				new VirtualFile( $this, 'orig-prefix.txt', '/venv' );
		}

		// Let the application set up its own directories, for example it may wish to add a new
		// lib directory for its own libraries.
		$app->initializeFilesystem( $this );

		// init stdin/out/err so that fstat can be called on them
		$this->fds[0] = new StatOnlyFD( $this->root, STDIN );
		$this->fds[1] = new StatOnlyFD( $this->root, STDOUT );
		$this->fds[2] = new StatOnlyFD( $this->root, STDERR );
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

	public function getRoot() {
		return $this->root;
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

		$maxfds = $this->app->getConfigurationInstance()->get( 'MaxFDs' );
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
		$this->validateFd( $fd, true );

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
		$maxlen = $this->app->getConfigurationInstance()->get( 'MaxReadLength' );
		if ( $length > $maxlen ) {
			$length = $maxlen;
		}
		
		$this->validateFd( $fd );	

		return $this->fds[$fd]->read( $length );
	}

	public function seek( $fd, $offset, $whence ) {
		$this->validateFd( $fd );

		if ( !in_array( $whence, [ SEEK_SET, SEEK_CUR, SEEK_END ] ) ) {
			throw new SyscallException( EINVAL );
		}

		return $this->fds[$fd]->seek( $offset, $whence );
	}

	public function validateFd( $fd, $allowSpecial = false ) {
		if ( !$allowSpecial && $fd >= 0 && $fd <= 2 ) {
			throw new SyscallException( EPERM );
		} elseif ( $fd >= 3 && $fd <= 4 ) {
			throw new SyscallException( EPERM );
		} elseif ( !isset( $this->fds[$fd] ) ) {
			throw new SyscallException( EBADF );
		}
	}

	public function dup( $oldFd, $newFd = 0, $exactFd = false ) {
		$this->validateFd( $oldFd, true );

		$maxfds = $this->app->getConfigurationInstance()->get( 'MaxFDs' );
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
				$this->fds[$i] = &$this->fds[$oldFd]->dup();
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
