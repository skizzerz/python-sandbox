<?php

namespace PythonSandbox;

class Configuration {
	private static $instance = null;

	protected $config = [
		'MaxFDs' => 64,
		'MaxReadLength' => 8192,
		'MemoryLimit' => 0,
		'CPULimit' => 0,
		'RPCHandlers' => [
			NS_SYS => 'PythonSandbox\SyscallHandler',
			NS_SB => 'PythonSandbox\SandboxHandler',
			// the application is expected to provide this class, or alternatively
			// override this configuration with some other value. As such, it is not
			// namespaced. Its constructor signature should be:
			// public function __construct( PythonSandbox\Sandbox $sb )
			NS_APP => 'ApplicationHandler'
		],
		'AllowedPythonLibs' => [
			'*.py',
			'*.so'
		],
		'AllowedSystemLibs' => [
			'*.so',
			'*.so.[0-9]',
			'*.so.[0-9][0-9]'
		]
	];

	protected function __construct() { }

	// this method should never be called directly, use Application::getConfigurationInstance()
	// instead so that user code can return a subclass if desired
	public static function singleton() {
		if ( self::$instance === null ) {
			self::$instance = new Configuration();
		}

		return self::$instance;
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
