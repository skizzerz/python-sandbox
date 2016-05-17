<?php

namespace PythonSandbox;

class DirFD extends FDBase {
	protected $realpath;
	protected $fh = null;
	protected $off = 0;

	public function __construct( Node $node, $realpath, $mode ) {
		parent::__construct( $node, $mode );
		$this->realpath = $realpath;
		$this->fh = opendir( $realpath );
	}

	public function __destruct() {
		if ( $this->fh !== null ) {
			$this->closeInternal();
		}
	}

	public function read( $length ) {
		if ( $this->fh === null ) {
			throw new SyscallException( EBADF );
		}

		throw new SyscallException( EISDIR );
	}

	public function stat() {
		return stat( $this->realpath );
	}

	protected function closeInternal() {
		if ( $this->fh === null ) {
			throw new SyscallException( EBADF );
		}

		closedir( $this->fh );
		$this->fh = null;
	}

	public function seek( $offset, $whence ) {
		if ( $this->fh === null ) {
			throw new SyscallException( EBADF );
		}

		if ( $offset === 0 && $whence === SEEK_SET ) {
			rewinddir( $this->fh );
			$this->off = 0;
			return 0;
		}

		throw new SyscallException( EINVAL, 'Invalid directory seek' );
	}

	public function getdents( $bufsize, $structBytes ) {
		if ( $this->fh === null ) {
			throw new SyscallException( EBADF );
		}

		$arr = [];
		$used = 0;

		// init: the previous call to this advances the directory
		// one beyond where we start, so rewind it.
		rewinddir( $this->fh );
		for ( $i = 0; $i < $this->off; ++$i ) {
			readdir( $this->fh );
		}

		while ( true ) {
			$next = readdir( $this->fh );
			if ( $next === false ) {
				// nothing left to get
				break;
			}

			$stat = stat( "{$this->realpath}/{$next}" );
			// check if we're allowed to view this file/directory before
			// returning that it exists
			if ( ( $stat['mode'] & S_IFMT ) === S_IFDIR ) {
				if ( !$this->node->checkSubdir( $next ) ) {
					++$this->off;
					continue;
				}
			} else {
				if ( !$this->node->checkFile( $next ) ) {
					++$this->off;
					continue;
				}
			}

			$len = strlen( $next );
			if ( $used + $structBytes + $len > $bufsize ) {
				break;
			}

			$arr[] = [
				'd_ino' => $stat['ino'],
				'd_type' => ($stat['mode'] & S_IFMT) >> 12,
				'd_name' => $next,
			];
			$used += $structBytes + $len;
			++$this->off;
		}

		return $arr;
	}
}
