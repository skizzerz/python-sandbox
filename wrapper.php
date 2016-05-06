<?php

spl_autoload_register(function ($class) {
	$file = __DIR__ . '/' . str_replace('\\', '/', $class) . '.php';
	if (file_exists($file)) {
		require $file;
	}
});

$IP = __DIR__;
$pyBin = "$IP/python35/bin/python3";
$pyLib = "$IP/python35/lib/python3.5";
$sbLib = "$IP/lib";
$sbBin = $IP;

PythonSandbox\Sandbox::runNewSandbox($sbBin, $pyBin, $sbLib, $pyLib);
