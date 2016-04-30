<?php

spl_autoload_register(function ($class) {
	$file = __DIR__ . '/' . str_replace('\\', '/', $class) . '.php';
	if (file_exists($file)) {
		require $file;
	}
});

$IP = __DIR__;
$pyBin = "$IP/python34/bin/python3";
$pyLib = "$IP/python34/lib/python3.4";
$sbLib = "$IP/lib";
$sbBin = "$IP/sandbox";

PythonSandbox\Sandbox::runNewSandbox($sbBin, $pyBin, $sbLib, $pyLib);
