<?php

namespace PythonSandbox;

final class SandboxUtil {
	private function __construct() { }

	public static function strerror( $code ) {
		if ( function_exists( 'posix_strerror' ) ) {
			return posix_strerror( $code );
		} elseif ( function_exists( 'pcntl_strerror' ) ) {
			return pcntl_strerror( $code );
		} elseif ( function_exists( 'socket_strerror' ) ) {
			return socket_strerror( $code );
		}

		// not entirely helpful, but an internet search can turn up more details
		return "Error $code.";
	}
}
