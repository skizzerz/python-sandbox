<?php

namespace PythonSandbox;

class VirtualFD extends FDBase {
	protected $pos = 0;

	public function read( $length ) {
		if ( $this->node === null ) {
			throw new SyscallException( EBADF );
		}

		$ret = $this->node->readInternal( $this->pos, $length );
		$this->pos += $length;

		return $ret;
	}

	public function stat() {
		if ( $this->node === null ) {
			throw new SyscallException( EBADF );
		}

		return $this->node->stat();
	}

	public function close() {
		if ( $this->node === null ) {
			throw new SyscallException( EBADF );
		}

		$this->node = null;
	}
}
