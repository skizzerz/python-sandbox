<?php

namespace PythonSandbox;

class SyscallException extends \RuntimeException {
	public function __construct( $code, $message = null, \Exception $previous = null ) {
		if ( $previous === null && $message instanceOf \Exception ) {
			$previous = $message;
			$message = null;
		}

		if ( $message === null ) {
			$message = SandboxUtil::strerror( $code );
		}

		parent::__construct( $message, $code, $previous );
	}
}
