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

	public function seek( $offset, $whence ) {
		if ( $this->node === null ) {
			throw new SyscallException( EBADF );
		}

		switch ( $whence ) {
		case SEEK_SET:
			if ( $offset < 0 ) {
				throw new SyscallException( EINVAL );
			}

			$this->pos = $offset;
			break;
		case SEEK_CUR:
			if ( $this->pos + $offset < 0 ) {
				if ( $offset > 0 ) {
					throw new SyscallException( EOVERFLOW );
				} else {
					throw new SyscallException( EINVAL );
				}
			}

			$this->pos += $offset;
			break;
		case SEEK_END:
			$len = $this->node->getLen();
			if ( $len + $offset < 0 ) {
				if ( $offset > 0 ) {
					throw new SyscallException( EOVERFLOW );
				} else {
					throw new SyscallException( EINVAL );
				}
			}

			$this->pos = $len + $offset;
			break;
		}

		return $this->pos;
	}

	public function close() {
		if ( $this->node === null ) {
			throw new SyscallException( EBADF );
		}

		$this->node = null;
	}
}
