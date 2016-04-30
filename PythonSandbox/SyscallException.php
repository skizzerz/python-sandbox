<?php

namespace PythonSandbox;

class SyscallException extends \RuntimeException {
	public function __construct( $code, \Exception $previous = null ) {
		parent::__construct( SandboxUtil::strerror( $code ), $code, $previous );
	}
}
