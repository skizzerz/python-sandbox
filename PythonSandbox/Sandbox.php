<?php

namespace PythonSandbox;

require_once 'Constants.php';

class Sandbox {
	protected $fs = null;
	protected $env = [];
	protected $sandboxPath = '';
	protected $proc = false;
	protected $pipes = [];

	public static function runNewSandbox( $sbBinPath, $pyBinPath, $sbLibPath, $pyLibPath ) {
		$sb = new Sandbox( $sbBinPath, $pyBinPath, $sbLibPath, $pyLibPath );
		return $sb->run();
	}

	public function __construct( $sbBinPath, $pyBinPath, $sbLibPath, $pyLibPath ) {
		$this->fs = new VirtualFS( $pyBinPath, $pyLibPath, $sbLibPath );
		$this->sandboxPath = "$sbBinPath/sandbox";
		$this->env = [
			'PYTHONHOME' => '/lib/python',
			'PYTHONPATH' => '/lib/sandbox',
			'PYTHONDONTWRITEBYTECODE' => '1',
			'PYTHONNOUSERSITE' => '1',
			'PYTHONFAULTHANDLER' => '',
			'PATH' => '/bin',
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
		// sandbox is established it can only read from 3 and 5 and write to 4.
		$this->proc = proc_open( "exec \"{$this->sandboxPath}\" /bin/python 0 0",
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
		$handler = new SyscallHandler( $this, $this->pipes[4], $this->pipes[3] );

		try {
			// this loops until an error is encountered or the child process finishes
			$handler->run();
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
}
