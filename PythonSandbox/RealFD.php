<?php

namespace PythonSandbox;

class RealFD extends FDBase {
	protected $fh = null;

	public function __construct( Node $node, $fh, $mode ) {
		parent::__construct( $node, $mode );
		$this->fh = $fh;
	}

	public function __destruct() {
		if ( $this->fh !== null ) {
			$this->closeInternal();
		}
	}

	// for internal use only
	public function getfh() {
		return $this->fh;
	}

	public function read( $length ) {
		if ( $this->fh === null ) {
			throw new SyscallException( EBADF );
		}

		return fread( $this->fh, $length );
	}

	public function seek( $offset, $whence ) {
		if ( $this->fh === null ) {
			throw new SyscallException( EBADF );
		}

		$ret = fseek( $this->fh, $offset, $whence );
		if ( $ret === -1 ) {
			throw new SyscallException( EINVAL );
		}

		return $ret;
	}

	public function stat() {
		if ( $this->fh === null ) {
			throw new SyscallException( EBADF );
		}

		return fstat( $this->fh );
	}

	protected function closeInternal() {
		if ( $this->fh === null ) {
			throw new SyscallException( EBADF );
		}

		fclose( $this->fh );
		$this->fh = null;
	}
}
