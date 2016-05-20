<?php

namespace PythonSandbox;

class RPCException extends \RuntimeException {
	protected $errno;

	public function __construct( $code, $message, $errno = null ) {
		if ( $errno === null ) {
			$errno = $code;
		}

		$this->errno = $errno;
	}

	public function getErrno() {
		return $this->errno;
	}
}
