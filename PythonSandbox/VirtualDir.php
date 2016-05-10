<?php

namespace PythonSandbox;

class VirtualDir extends DirBase {
	protected $inode;

	public function __construct( VirtualFS $fs, $name, DirBase $parent = null ) {
		parent::__construct( $fs, $name, $parent );
		$this->inode = rand();
	}

	public function stat() {
		$time = time();
		$mode = S_IFDIR | $this->getPermissions();
		return [
			1, $this->inode, $mode, 0, SB_UID, SB_GID, 0, 0, $time, $time, $time, 512, 0,
			'dev' => 1,
			'ino' => $this->inode,
			'mode' => $mode,
			'nlink' => 0,
			'uid' => SB_UID,
			'gid' => SB_GID,
			'rdev' => 0,
			'size' => 0,
			'atime' => $time,
			'mtime' => $time,
			'ctime' => $time,
			'blksize' => 512,
			'blocks' => 0
		];
	}

	public function access( $mode ) {
		if ( $mode & ~7 ) {
			throw new SyscallException( EINVAL );
		} elseif ( $mode & 2 ) {
			return false;
		}

		return true;
	}
}
