<?php

namespace PythonSandbox;

class RealFD extends FDBase {
	protected $fh = null;

	public function __construct( Node $node, $fh ) {
		parent::__construct( $node );
		$this->fh = $fh;
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

		return fread( $this->fh, $length );
	}

	public function stat() {
		if ( $this->fh === null ) {
			throw new SyscallException( EBADF );
		}

		return fstat( $this->fh );
	}

	public function close() {
		if ( $this->fh === null ) {
			throw new SyscallException( EBADF );
		}

		fclose( $this->fh );
		$this->fh = null;
	}
}
