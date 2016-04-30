<?php

namespace PythonSandbox;

abstract class FDBase {
	protected $node = null;

	public function __construct( Node $node ) {
		$this->node = $node;
	}

	abstract public function read( $length );
	abstract public function stat();
	abstract public function close();
}
