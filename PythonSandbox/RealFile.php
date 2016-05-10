<?php

namespace PythonSandbox;

class RealFile extends FileBase {
	protected $realpath;

	public function __construct( VirtualFS $fs, $name, $realpath, DirBase $parent = null ) {
		parent::__construct( $fs, $name, $parent );
		$this->realpath = $realpath;
	}

	public function open( $flags, $mode ) {
		// TODO: use flags/mode somehow
		$fh = fopen( $this->realpath, 'rt' );

		return new RealFD( $this, $fh, $flags );
	}

	public function stat() {
		return stat( $this->realpath );
	}

	public function access( $mode ) {
		if ( $mode & ~7 ) {
			throw new SyscallException( EINVAL );
		} elseif ( $mode & 2 ) {
			// VirtualFile does not set the execute bit however we want to test
			// for the real +x in RealFile because some things may care if a file
			// is executable or not even if it has no ability to actually execute it.
			return false;
		}
		
		return posix_access( $this->realpath, $mode );
	}

	public function exists() {
		return is_file( $this->realpath );
	}

	public function getPermissions() {
		return fileperms( $this->realpath ) & 0777;
	}
}
