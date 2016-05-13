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

	public static function normalizePath( $path, $base = '/', $stripFileFromBase = false ) {
		if ( $path[0] !== '/' ) {
			if ( $stripFileFromBase ) {
				$pp = explode( '/', $base );
				array_pop( $pp );
				$base = '/' . implode( '/', $pp );
			}

			$path = "{$base}/{$path}";
		}

		$pp = explode( '/', $path );
		$np = [];

		foreach ( $pp as $p ) {
			if ( $p === '' || $p === '.' ) {
				continue;
			} elseif ( $p === '..' ) {
				array_pop( $np );
			} else {
				$np[] = $p;
			}
		}

		$path = '/' . implode( '/', $np );

		return $path;
	}
}
