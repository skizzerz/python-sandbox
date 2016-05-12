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
			$this->close();
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

	public function close() {
		if ( $this->fh === null ) {
			throw new SyscallException( EBADF );
		}

		closedir( $this->fh );
		$this->fh = null;
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

			$len = strlen( $next );
			if ( $used + $structBytes + $len > $bufsize ) {
				break;
			}

			$stat = stat( "{$this->realpath}/{$next}" );

			$arr[] = [
				'd_ino' => $stat['ino'],
				'd_type' => ($stat['mode'] & 0170000) >> 12,
				'd_name' => $next,
			];
			$used += $structBytes + $len;
			++$this->off;
		}

		return $arr;
	}
}
