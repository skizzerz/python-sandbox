<?php

namespace PythonSandbox;

class SyscallException extends RPCException {
	public function __construct( $code, $message = null ) {
		if ( $message === null ) {
			$message = SandboxUtil::strerror( $code );
		}

		parent::__construct( $message, -1, $code );
	}
}
