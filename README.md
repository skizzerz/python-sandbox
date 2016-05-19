# python-sandbox
Ability to run python in a sandbox with a parent-child process relationship.
This repository features the sandboxed child as well as a reference parent implementation in PHP.
Implementing the parent in other languages is possible, please consule the sandbox API documentation for details.
By itself, the sandbox does nothing; a parent application is needed in order to have this do anything useful.

## System Requirements
* **Linux 3.5+**: The sandbox only works on linux, and even then only on kernel versions 3.5 and later (those with seccomp-bpf support).
Notably, the sandbox does not work on things that are not linux, such as Windows, Mac OS X, or FreeBSD.
* **Python 3.4+**: The sandbox was written for Python 3, and officially only supports Python versions 3.4 and later.
While it may work on earlier versions and Python 2, these are not supported scenarios and may require code modification.
Python development headers/libraries must also be installed in order to compile the sandbox.
* **glibc**: The sandbox leverages numerous GNU extensions to libc. If you're running linux, chances are you're also running glibc
so this is not an issue.
* **json-c**: The json-c development libraries must be installed. It is assumed these are installed via a package manager.

If you are using the reference PHP implementation for the parent, PHP 5.5+ is additionally required.

## Compilation
To compile the sandbox, light editing of the Makefile will be required. Patches are welcome to convert this over to an
autoconf or cmake-based build system, but that is very low priority for me at the moment. Notably, you may need to edit the paths
pointed to by PYCONFIG and PKG_CONFIG. If json-c was not installed via a package manager, then CFLAGS and LDFLAGS must be modified to
include the correct flags to include the json-c headers and link to the library. After the Makefile has been edited to your liking,
simply run `make` to compile the sandbox. In case you wish to move the outputs to a different directory, the files you care about are
`sandbox` and `libsbpreload.so`, as well as the entire `lib` directory.

## API Documentation
For API Documentation, including both the sandbox client API and the reference PHP API, please see the Wiki.
