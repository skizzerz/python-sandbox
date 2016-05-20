<?php

namespace PythonSandbox;

abstract class Application {
	abstract public function getPythonVersion();
	abstract public function getPythonBasePath();
	abstract public function getSandboxBasePath();
	
	public function getInitScriptNode( VirtualFS $fs, $fileName ) {
		return new VirtualFile( $fs, $fileName, '# init script contents' );
	}

	public function getUserScriptNode( VirtualFS $fs, $fileName ) {
		return new VirtualFile( $fs, $fileName, '# user script contents' );
	}

	public function getConfigurationInstance() {
		return Configuration::singleton();
	}

	public function getFilesystemInstance() {
		return new VirtualFS( $this );
	}

	public function initializeFilesystem( VirtualFS $fs ) {
		// no-op, a subclass can override this if it wishes to do something here
	}

	public function getLibraryPaths() {
		return [];
	}

	public function initializeSandbox( Sandbox $sb ) {
		// no-op, a subclass can override this if it wishes to do something here
	}
}
