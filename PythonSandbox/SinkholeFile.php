<?php

namespace PythonSandbox;

class SinkholeFile extends VirtualFile {
	public function __construct( VirtualFS $fs, $name, DirBase $parent = null ) {
		parent::__construct( $fs, $name, '', $parent );
	}

	public function readInternal( $pos, $length ) {
		return '';
	}

	public function writeInternal( $pos, $contents ) {
		return strlen( $contents );
	}

	public function stat() {
		$time = time();
		$mode = S_IFCHR | 0666; // crw-rw-rw-

		return [
			1, $this->inode, $mode, 0, 0, 0, 0, 0, $time, $time, $time, 512, 0,
			'dev' => 1,
			'ino' => $this->inode,
			'mode' => $mode,
			'nlink' => 0,
			'uid' => 0,
			'gid' => 0,
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
		} elseif ( $mode & 1 ) {
			return false;
		}

		return true;
	}
}
