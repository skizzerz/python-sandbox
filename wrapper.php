<?php

spl_autoload_register(function ($class) {
	$file = __DIR__ . '/' . str_replace('\\', '/', $class) . '.php';
	if (file_exists($file)) {
		require $file;
	}
});

$IP = __DIR__;
$pyBin = "$IP/python35/bin/python3";
$pyLib = "$IP/python35";
$sbLib = "$IP/lib";
$sbBin = "$IP/sandbox";
$sbPreload = "$IP/libsbpreload.so";

$opts = getopt('v');
if (isset($opts['v'])) {
	PythonSandbox\Configuration::singleton()->set('Verbose', true);
}

PythonSandbox\Sandbox::runNewSandbox($sbBin, $pyBin, $sbLib, $pyLib, $sbPreload);
