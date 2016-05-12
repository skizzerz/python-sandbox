<?php

namespace PythonSandbox;

class StatResult {
	protected $raw = [];
	protected $arr = [];

	public function __construct( $data ) {
		foreach ( $data as $key => $val ) {
			if ( is_int( $key ) ) {
				$this->raw[$key] = $val;
			} else {
				$this->arr["st_$key"] = $val;
			}
		}
	}

	public function getRaw() {
		return implode( ' ', $this->raw );
	}

	public function getArray() {
		return $this->arr;
	}
}
