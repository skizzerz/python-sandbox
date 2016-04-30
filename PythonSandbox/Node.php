<?php

namespace PythonSandbox;

// represents a file or directory in our virtual filesystem
abstract class Node {
	protected $fs = null;
	protected $name = '';
	protected $parent = null;
	protected $path = null;

	public function __construct( VirtualFS $fs, $name, DirBase $parent = null ) {
		$this->fs = $fs;
		$this->name = $name;

		if ( $parent !== null ) {
			$parent->addChild( $this );
			$this->setParent( $parent );
		}
	}

	abstract public function getChildren();
	abstract public function addChild( Node $child );
	abstract public function removeChild( Node $child );
	abstract public function hasChild( Node $child );

	abstract public function open( $flags, $mode );
	abstract public function stat();

	public function exists() {
		return true;
	}

	public function getPermissions() {
		return 0444;
	}

	public function getName() {
		return $this->name;
	}

	public function getPath() {
		return $this->path;
	}

	public function getParent() {
		return $this->parent;
	}

	public function setParent( Node $parent = null ) {
		if ( $parent === null && !$this->parent->hasChild( $this ) ) {
			$this->parent = null;
			$this->path = null;
		} elseif ( $parent->hasChild( $this ) ) {
			if ( $this->parent !== null ) {
				$this->parent->removeChild( $this );
			}

			$this->parent = $parent;
			$this->path = "{$parent->getPath()}/{$this->getName()}";
		}
	}
}
