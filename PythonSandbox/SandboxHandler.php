<?php

namespace PythonSandbox;

class SandboxHandler {
	protected $sb;

	public function __construct( Sandbox $sb ) {
		$this->sb = $sb;
	}

	public function complete_init() {
		$this->sb->setInitialized();
	}
}
