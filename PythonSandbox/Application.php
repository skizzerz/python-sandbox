<?php

namespace PythonSandbox;

abstract class Application {
	abstract public function getPythonVersion();
	abstract public function getPythonBasePath();
	abstract public function getSandboxBasePath();
	abstract public function getInitScriptNode( VirtualFS $fs, $fileName );
	abstract public function getUserScriptNode( VirtualFS $fs, $fileName );

	public function getConfigurationInstance() {
		return Configuration::singleton();
	}

	public function initializeFilesystem( Node $root ) {
		// no-op, a subclass can override this if it wishes to do something here
	}
}
