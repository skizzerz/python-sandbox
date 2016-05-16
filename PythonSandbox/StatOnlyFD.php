<?php

namespace PythonSandbox;

class StatOnlyFD extends RealFD {
	public function __construct( Node $node, $fh ) {
		parent::__construct( $node, $fh, 0 );
	}

	public function read( $length ) {
		throw new SyscallException( EPERM );
	}

	public function seek( $offset, $whence ) {
		throw new SyscallException( EPERM );
	}

	public function close () {
		// no-op
	}
}
