<?php

namespace PythonSandbox;

class StatResult {
	protected $raw = [];
	protected $arr = [];

	public function __construct( $data ) {
		// override the user/group with our own (fake) version
		// this prevents minor information leaks about who owns what files
		// in the filesystem that the sandbox is able to directly access
		// we present a view that a file is either owned by root or the sandbox
		if ( $data['uid'] !== 0 ) {
			$data[4] = SB_UID;
			$data[5] = SB_GID;
			$data['uid'] = SB_UID;
			$data['gid'] = SB_GID;
		} else {
			$data[4] = 0;
			$data[5] = 0;
			$data['uid'] = 0;
			$data['gid'] = 0;
		}

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
