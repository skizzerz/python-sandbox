<?php

namespace PythonSandbox;

class VirtualFile extends FileBase {
	protected $contents = '';
	protected $len = 0;
	protected $inode = 0;

	public function __construct( VirtualFS $fs, $name, $contents, DirBase $parent = null ) {
		parent::__construct( $fs, $name, $parent );

		$this->contents = $contents;
		$this->len = strlen( $contents );
		$this->inode = rand();
	}

	public function open( $flags, $mode ) {
		// TODO: deny access if trying to open for writing
		return new VirtualFD( $this, $flags );
	}

	// Called by VirtualFD::read() so needs to be public
	// this is not meant to be called by external code
	public function readInternal( $pos, $length ) {
		if ( $pos >= $this->len ) {
			return '';
		}

		return substr( $this->contents, $pos, $length );
	}

	public function stat() {
		$time = time();
		$mode = S_IFREG | $this->getPermissions();
		$nblocks = (int)ceil( $this->len / 512 );

		return [
			1, $this->inode, $mode, 0, SB_UID, SB_GID, 0, $this->len, $time, $time, $time, 512, $nblocks,
			'dev' => 1,
			'ino' => $this->inode,
			'mode' => $mode,
			'nlinks' => 0,
			'uid' => SB_UID,
			'gid' => SB_GID,
			'rdev' => 0,
			'size' => $this->len,
			'atime' => $time,
			'mtime' => $time,
			'ctime' => $time,
			'blksize' => 512,
			'blocks' => $nblocks
		];
	}

	public function access( $mode ) {
		if ( $mode & ~7 ) {
			throw new SyscallException( EINVAL );
		} elseif ( $mode & 3 ) {
			return false
		}

		return true;
	}
}
