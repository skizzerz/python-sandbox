<?php

namespace PythonSandbox;

abstract class DirBase extends Node implements \ArrayAccess, \Iterator {
	protected $nodes = [];

	public function getChildren() {
		return $this->nodes;
	}

	public function addChild( Node $child ) {
		$this[] = $child;
	}

	public function removeChild( Node $child ) {
		if ( $this->hasChild( $child ) && $child->getParent() == $this ) {
			unset( $this[$child] );
		}
	}

	public function hasChild( Node $child ) {
		return isset( $this[$child] ) && $this[$child] === $child;
	}

	public function getPermissions() {
		return parent::getPermissions() | 0111;
	}

	public function open( $flags, $mode ) {
		throw new SyscallException( EISDIR );
	}

	protected function childExists( $name ) {
		return isset( $this->nodes[$name] );
	}

	protected function getChild( $name ) {
		return $this->nodes[$name];
	}

	protected function addChildInternal( $name, $child ) {
		$this->nodes[$name] = $child;
	}

	protected function removeChildInternal( $name ) {
		unset( $this->nodes[$name] );
	}

	// ArrayAccess

	public function offsetExists( $offset ) {
		if ( $offset instanceOf Node ) {
			$offset = $offset->getName();
		}

		return $this->childExists( $offset );
	}

	public function offsetGet( $offset ) {
		if ( $offset instanceOf Node ) {
			$offset = $offset->getName();
		}

		return $this->offsetExists( $offset ) ? $this->getChild( $offset ) : null;
	}

	public function offsetSet( $offset, $value ) {
		if ( !( $value instanceOf Node ) ) {
			throw new \InvalidArgumentException( 'Children must be Nodes.' );
		}

		if ( $offset instanceOf Node ) {
			$offset = $offset->getName();
		}

		if ( $offset === null ) {
			$this->addChildInternal( $value->getName(), $value );
		} else {
			$this->addChildInternal( $offset, $value );
		}

		$value->setParent( $this );
	}

	public function offsetUnset( $offset ) {
		if ( $offset instanceOf Node ) {
			$offset = $offset->getName();
		}

		if ( $this->offsetExists( $offset ) ) {
			$node = $this->getChild( $offset );
			$this->removeChildInternal( $offset );
			$node->setParent( null );
		}
	}

	// Iterator

	public function current() {
		return current( $this->nodes );
	}

	public function key() {
		return key( $this->nodes );
	}

	public function next() {
		next( $this->nodes );
	}

	public function rewind() {
		reset( $this->nodes );
	}

	public function valid() {
		return $this->key() !== null;
	}
}
