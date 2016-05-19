<?php

namespace PythonSandbox;

require_once 'Constants.php';

class Sandbox {
	protected $initialized = false;
	protected $fs = null;
	protected $env = [];
	protected $sandboxPath = '';
	protected $proc = false;
	protected $pipes = [];

	public static function runNewSandbox( $pyVer, $pyBase, $sbBase ) {
		$sb = new Sandbox( $pyVer, $pyBase, $sbBase );
		return $sb->run();
	}

	public function __construct( $pyVer, $pyBase, $sbBase ) {
		$this->fs = new VirtualFS( $pyVer, $pyBase, $sbBase );
		$this->sandboxPath = "$sbBase/sandbox";
		$this->env = [
			'PYTHONPATH' => '/usr/lib/sandbox',
			'PYTHONDONTWRITEBYTECODE' => '1',
			'PYTHONNOUSERSITE' => '1',
			'PATH' => '/bin',
			'LD_PRELOAD' => "$sbBase/libsbpreload.so"
		];
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
		$this->proc = proc_open( "exec \"{$this->sandboxPath}\" /usr/bin/python 0 0 \"$dynlibDir\"",
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
