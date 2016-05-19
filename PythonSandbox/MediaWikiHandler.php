<?php

namespace PythonSandbox;

class MediaWikiHandler {
	protected $sb;

	public function __construct( Sandbox $sb ) {
		$this->sb = $sb;
	}
}
