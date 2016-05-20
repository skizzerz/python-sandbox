<?php

namespace PythonSandbox;

class RPCServer {
	protected $sb;
	protected $rpipe;
	protected $wpipe;

	public function __construct( Sandbox $sb, $rpipe, $wpipe ) {
		$this->sb = $sb;
		$this->rpipe = $rpipe;
		$this->wpipe = $wpipe;
	}

	public function run() {
		$config = Configuration::singleton();
		$mappings = $config->get( 'RPCHandlers' );
		// TODO: run a hook to allow extensions to manipulate the mappings
		foreach ( $mappings as &$handler ) {
			$handler = new $handler( $this->sb );
		}

		// this function loops until the child proc finishes or we get an exception
		// (other than RPCException which indicate we should pass error down to child).
		while ( true ) {
			// marshal format is line based, each line has one json object
			// input: {"name": "fname", "args": [...]}
			// response: {"code": 0, "errno": 0, "data": ...}

			$r = [ $this->rpipe ];
			$w = [];
			$x = [];

			if ( !stream_select( $r, $w, $x, 5 ) ) {
				echo "Read timeout.\n";
				break;
			}

			$line = fgets( $this->rpipe );
			if ( $line === false ) {
				break;
			}

			if ( $config->get( 'Verbose' ) ) {
				echo "<<< $line";
			}

			$call = json_decode( $line, false, 32, JSON_BIGINT_AS_STRING );
			if ( $call === null || !isset( $call->ns ) || !isset( $call->name )
					|| !isset( $call->args ) || !is_array( $call->args ) ) {
				echo "Invalid JSON.\n";
				break;
			} elseif ( !preg_match( '/^[a-z0-9_]{1,32}$/i', $call->name ) ) {
				// paranoia check. Just in case there's some weird exploit with Reflection that could allow
				// a carefully crafted method name to call arbitrary code, we ensure that whatever name
				// we receive will form a valid PHP method name.
				echo "Invalid name.\n";
				break;
			} elseif ( !array_key_exists( $call->ns, $mappings ) ) {
				echo "Invalid namespace.\n";
				break;
			}

			$raw = isset( $call->raw ) && $call->raw;
			$nargs = count( $call->args );
			$obj = $mappings[$call->ns];
			$handler = new \ReflectionObject( $obj );
			if ( $handler->hasMethod( $call->name ) ) {
				try {
					$m = $handler->getMethod( $call->name );
					$ret = $m->invokeArgs( $obj, $call->args );
					$errno = 0;
					$data = null;

					if ( $call->ns === NS_SYS ) {
						if ( is_array( $ret ) ) {
							list( $ret, $data ) = $ret;
						}
					} else {
						$data = $ret;
						$ret = 0;
					}
				} catch ( RPCException $e ) {
					if ($call->ns === NS_SYS) {
						$ret = -1;
						$errno = $e->getErrno();
						$data = $e->getMessage();
					} else {
						$ret = $e->getCode();
						$data = $e->getMessage();
						$errno = $e->getErrno();
					}
				}

				if ( $raw ) {
					if ( $data instanceOf StatResult ) {
						$ret = "$ret $errno {$data->getRaw()}";
					} else {
						$data = base64_encode( $data );
						$ret = "$ret $errno $data";
					}
				} else {
					if ( $data instanceOf StatResult ) {
						$data = $data->getArray();
					}

					$json = json_encode( [
						'code' => $ret,
						'errno' => $errno,
						'data' => $data
					] );

					if ( $json === false ) {
						// $data is a binary string
						$json = json_encode( [
							'code' => $ret,
							'errno' => $errno,
							'data' => base64_encode( $data ),
							'base64' => true
						] );
					}

					$ret = $json;
				}

				if ( $config->get( 'Verbose' ) ) {
					if ( strlen( $ret ) > 250 ) {
						echo ">>> " . substr( $ret, 0, 250 ) . "...\n";
					} else {
						echo ">>> $ret\n";
					}
				}

				$r = [];
				$w = [ $this->wpipe ];
				$x = [];

				if ( !stream_select( $r, $w, $x, 5 ) ) {
					echo "Write timeout.\n";
					break;
				}

				fprintf( $this->wpipe, "%s\n", $ret );
			} else {
				echo "No such method {$handler->getName()}::{$call->name}().\n";
				return;
			}
		}
	}
}
