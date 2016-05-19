<?php

namespace PythonSandbox;

require_once 'Constants.php';

class Sandbox {
	protected $app = null;
	protected $initialized = false;
	protected $fs = null;
	protected $env = [];
	protected $sandboxPath = '';
	protected $proc = false;
	protected $pipes = [];

	public static function runNewSandbox( Application $app ) {
		$sb = new Sandbox( $app );
		return $sb->run();
	}

	public function __construct( Application $app ) {
		$this->app = $app;
		$this->fs = $app->getFilesystemInstance();

		$sbBase = $app->getSandboxBasePath();
		$this->sandboxPath = "$sbBase/sandbox";

		$paths = $app->getLibraryPaths();
		if ( !is_array( $paths ) ) {
			$paths = [ $paths ];
		}

		$paths = implode( ':', $paths );
		if ( $paths !== '' ) {
			$paths = ':' . $paths;
		}

		$this->env = [
			'PYTHONPATH' => '/usr/lib/sandbox' . $paths,
			'PYTHONDONTWRITEBYTECODE' => '1',
			'PYTHONNOUSERSITE' => '1',
			'PATH' => '/bin',
			'LD_PRELOAD' => "$sbBase/libsbpreload.so"
		];

		// give the application the ability to perform further sandbox init;
		// for example, by calling $sb->setenv() to modify environment vars
		// this advanced init is generally not required, as getLibraryPaths()
		// covers the most common use case (injecting directories into the python search path)
		$app->initializeSandbox( $this );
	}

	public function __destruct() {
		if ( $this->proc !== false && proc_get_status( $this->proc )['running'] ) {
			// send SIGKILL to child proc; this should be safe as the sandbox does not ever
			// write to the filesystem. If needed, we can SIGTERM first to allow for a more graceful
			// shutdown and then wait a bit before sending SIGKILL. However since PHP is singlethreaded
			// this would negatively impact performance. Hence, just SIGKILL.
			proc_terminate( $this->proc, 9 /* SIGKILL */ );
		}
	}

	public function run() {
		// proc_open spawns the subproc in a shell, which is not desirable here, so we use exec
		// the child proc only has direct access to stdin/stdout/stderr during init, once the
		// sandbox is established it can only read from 3 and write to 4.
		$dynlibDir = $this->fs->getDynlibDir();
		$config = $this->app->getConfigurationInstance();
		$memLimit = $config->get( 'MemoryLimit' );
		$cpuLimit = $config->get( 'CPULimit' );
		$this->proc = proc_open( "exec \"{$this->sandboxPath}\" /usr/bin/python $memLimit $cpuLimit \"$dynlibDir\"",
			[
				0 => STDIN,
				1 => STDOUT,
				2 => STDERR,
				3 => [ 'pipe', 'r' ],
				4 => [ 'pipe', 'w' ]
			],
			$this->pipes, '/tmp', $this->env );

		if ( $this->proc === false ) {
			return false;
		}

		stream_set_blocking( $this->pipes[3], false );
		stream_set_blocking( $this->pipes[4], false );
		$server = new RPCServer( $this, $this->pipes[4], $this->pipes[3] );

		try {
			// this loops until an error is encountered or the child process finishes
			$server->run();
		} catch ( SandboxException $e ) {
			// no-op; we throw SandboxException whenever we encounter a condition wherein we wish
			// to immediately close the sandbox without also raising an exception in our parent process.
		} finally {
			fclose( $this->pipes[3] );
			fclose( $this->pipes[4] );
			$status = proc_close( $this->proc );
			echo "Child exited with $status.\n";
			$this->proc = false;
		}

		return $status;
	}

	public function getenv( $var, $default = null ) {
		if ( isset( $this->env[$var] ) ) {
			return $this->env[$var];
		}

		return $default;
	}

	public function setenv( $var, $value ) {
		$old = $this->getenv( $var );
		$this->env[$var] = $value;

		return $old;
	}

	public function getfs() {
		return $this->fs;
	}

	public function isInitialized() {
		return $this->initialized;
	}

	public function setInitialized() {
		$this->initialized = true;
	}
}
