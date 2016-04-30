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

		return new RealFD( $this, $fh );
	}

	public function stat() {
		return stat( $this->realpath );
	}

	public function exists() {
		return is_file( $this->realpath );
	}

	public function getPermissions() {
		return fileperms( $this->realpath ) & 0777;
	}
}
