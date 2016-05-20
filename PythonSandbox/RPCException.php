<?php

namespace PythonSandbox;

class RPCException extends \RuntimeException {
	protected $errno;

	public function __construct( $message, $code, $errno = null ) {
		if ( $errno === null ) {
			$errno = $code;
		}

		$this->errno = $errno;
		parent::__construct( $message, $code );
	}

	public function getErrno() {
		return $this->errno;
	}
}
