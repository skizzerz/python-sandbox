<?php

spl_autoload_register(function ($class) {
	$file = __DIR__ . '/' . str_replace('\\', '/', $class) . '.php';
	if (file_exists($file)) {
		require $file;
	}
});

$IP = __DIR__;
$pyVer = "python3.5";
$pyBase = "$IP/python35";
$sbBase = $IP;

$opts = getopt('v');
if (isset($opts['v'])) {
	PythonSandbox\Configuration::singleton()->set('Verbose', true);
}

PythonSandbox\Sandbox::runNewSandbox($pyVer, $pyBase, $sbBase);
