CC=/usr/bin/gcc
# TODO: have a configure script to alter PYCONFIG in order to alter what python is used
PYCONFIG=/usr/bin/python3-config
PYCFLAGS=$(shell $(PYCONFIG) --cflags)
PYLDFLAGS=$(shell $(PYCONFIG) --ldflags)
CFLAGS=
LDFLAGS=-lseccomp

all: sandbox

sandbox: sandbox.o
	$(CC) -o sandbox sandbox.o $(PYLDFLAGS) $(LDFLAGS)

sandbox.o: sandbox.c sbcontext.h
	$(CC) -c sandbox.c $(PYCFLAGS) $(CFLAGS)

clean:
	rm sandbox *.o
