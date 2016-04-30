<?php

namespace PythonSandbox;

abstract class FileBase extends Node {
	public function getChildren() {
		throw new \Exception( 'Cannot call getChildren on a file.' );
	}

	public function addChild( Node $child ) {
		throw new \Exception( 'Cannot call addChild on a file.' );
	}

	public function hasChild( Node $child ) {
		throw new \Exception( 'Cannot call hasChild on a file.' );
	}

	public function removeChild( Node $child ) {
		throw new \Exception( 'Cannot call removeChild on a file.' );
	}
}
