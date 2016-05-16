<?php

namespace PythonSandbox;

class RealDir extends DirBase {
	protected $realpath;
	protected $diropts;

	public function __construct( VirtualFS $fs, $name, $realpath, array $diropts = [], DirBase $parent = null ) {
		parent::__construct( $fs, $name, $parent );
		$this->realpath = $realpath;
		// by default, we take a very narrow view: recursion is blocked and we cannot access dotfiles
		$diroptDefaults = [
			'recurse' => false,
			'followSymlinks' => false,
			'fileWhitelist' => [ '*' ],
			'subdirWhitelist' => [ '*' ],
		];

		$this->diropts = array_replace( $diroptDefaults, $diropts );
	}

	public function checkFile( $name ) {
		$found = ( count( $this->diropts['fileWhitelist'] ) === 0 );
		foreach ( $this->diropts['fileWhitelist'] as $pattern ) {
			if ( fnmatch( $pattern, $name, FNM_PERIOD ) ) {
				$found = true;
				break;
			}
		}

		return $found;
	}

	public function checkSubdir( $name ) {
		$found = ( count( $this->diropts['subdirWhitelist'] ) === 0 );
		foreach ( $this->diropts['subdirWhitelist'] as $pattern ) {
			if ( fnmatch( $pattern, $name, FNM_PERIOD ) ) {
				$found = true;
				break;
			}
		}

		return $found;
	}

	protected function childExists( $name ) {
		return $this->getChild( $name ) !== null;
	}

	protected function getChild( $name ) {
		// Allow for virtual files/dirs inside of real dirs
		// these should shadow any real files/dirs that exist
		// Note also that real children previously searched for are also here
		// which means a frequently-accessed child only incurs the penalty
		// for all of the stuff below once to search for it.
		if ( parent::childExists( $name ) ) {
			return parent::getChild( $name );
		}

		$di = scandir( $this->realpath );
		foreach ( $di as $item ) {
			if ( $item === '.' || $item === '..' || $item !== $name ) {
				continue;
			}

			$fullpath = $this->realpath . '/' . $item;

			if ( is_link( $fullpath ) ) {
				if ( !$this->diropts['followSymlinks'] ) {
					return null;
				}

				// resolve up to 10 symlinks before giving up
				for ( $i = 0; is_link( $fullpath ) && $i < 10; ++$i ) {
					$linkpath = readlink( $fullpath );
					$fullpath = SandboxUtil::normalizePath( $linkpath, $fullpath, true );

					if ( !file_exists( $fullpath ) ) {
						return null;
					}
				}

				if ( is_link( $fullpath ) ) {
					throw new SyscallException( ELOOP );
				}
			}

			$node = null;

			if ( is_dir( $fullpath ) ) {
				if ( !$this->diropts['recurse'] ) {
					return null;
				}

				if ( $this->checkSubdir( $name ) ) {
					$node = new RealDir( $this->fs, $name, $fullpath, $this->diropts, $this );
				}
			} else {
				if ( $this->checkFile( $name ) ) {
					$node = new RealFile( $this->fs, $name, $fullpath, $this );
				}
			}

			// cache this node
			$this->addChildInternal( $name, $node );

			return $node;
		}

		// no matches or empty dir
		return null;
	}

	public function open( $flags, $mode ) {
		if ( $flags & O_DIRECTORY !== O_DIRECTORY ) {
			throw new SyscallException( EISDIR );
		}

		return new DirFD( $this, $this->realpath, $flags );
	}

	public function stat() {
		return stat( $this->realpath );
	}

	public function access( $mode ) {
		if ( $mode & ~7 ) {
			throw new SyscallException( EINVAL );
		} elseif ( $mode & 2 ) {
			return false;
		}
		
		return posix_access( $this->realpath, $mode );
	}

	public function exists() {
		return is_dir( $this->realpath );
	}

	public function getPermissions() {
		return fileperms( $this->realpath ) & 0777;
	}
}
