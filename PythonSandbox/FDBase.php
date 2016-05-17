<?php

namespace PythonSandbox;

abstract class FDBase {
	protected $node = null;
	protected $fdFlags = 0; // FD_*
	protected $fileFlags = 0; // O_*
	protected $refcount = 1;

	public function __construct( Node $node, $mode ) {
		$this->node = $node;
		$this->fileFlags = $mode;

		if ( $mode & O_CLOEXEC ) {
			$this->fdFlags = FD_CLOEXEC;
		}
	}

	abstract public function read( $length );
	abstract public function stat();
	abstract protected function closeInternal();
	abstract public function seek( $offset, $whence );

	public function &dup() {
		++$this->refcount;
		return $this;
	}

	public function close() {
		--$this->refcount;
		if ( $this->refcount == 0 ) {
			$this->closeInternal();
		}
	}

	public function getdents( $bufsize, $structBytes ) {
		throw new SyscallException( ENOTDIR );
	}

	public function getNode() {
		return $this->node;
	}

	public function getMode() {
		return $this->fileFlags;
	}

	public function setMode( $mode ) {
		// only allow modification of certain flags
		$mode &= O_APPEND | O_ASYNC | O_DIRECT | O_NOATIME | O_NONBLOCK;
		// zero out all changed bits from original flags,
		// then set the flags we need to set
		$mask = ~( $this->fileFlags ^ $mode );
		$this->fileFlags &= $mask;
		$this->fileFlags |= $mode;
	}

	public function getFlags() {
		return $this->fdFlags;
	}

	public function setFlags( $flags ) {
		$this->fdFlags = $flags;
	}
}
