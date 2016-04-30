<?php

namespace PythonSandbox;

class Configuration {
	private static $instance = null;

	protected $config = [
		'MaxFDs' => 64,
		'MaxReadLength' => 8192
	];

	protected function __construct() { }

	public static function singleton() {
		if ( self::$instance === null ) {
			self::$instance = new Configuration();
		}

		return self::$instance;
	}

	public static function setInstance( Configuration $instance ) {
		self::$instance = $instance;
	}

	public function exists( $key ) {
		return isset( $this->config[$key] );
	}

	public function get( $key, $default = null ) {
		if ( isset( $this->config[$key] ) ) {
			return $this->config[$key];
		}

		return $default;
	}

	public function set( $key, $value ) {
		$prev = $this->get( $key );
		$this->config[$key] = $value;
		return $prev;
	}
}
