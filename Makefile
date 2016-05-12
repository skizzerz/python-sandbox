CC=/usr/bin/gcc
# TODO: have a configure script to alter PYCONFIG in order to alter what python is used
PYCONFIG=/usr/bin/python3.5-config
PKG_CONFIG=/usr/bin/pkg-config
CFLAGS=$(shell $(PKG_CONFIG) --cflags json-c) $(shell $(PYCONFIG) --cflags)
LDFLAGS=$(shell $(PKG_CONFIG) --libs json-c) $(shell $(PYCONFIG) --ldflags) -lseccomp

all: sandbox

sandbox: sandbox.o sblibc.o sbio.o
	$(CC) -o sandbox sandbox.o sblibc.o sbio.o $(LDFLAGS)

sandbox.o: sandbox.c sbcontext.h
	$(CC) -c sandbox.c $(CFLAGS) -DSB_DEBUG

sblibc.o: sblibc.c sbcontext.h sblibc.h
	$(CC) -c sblibc.c $(CFLAGS) -DSB_DEBUG

sbio.o: sbio.c sbcontext.h sblibc.h
	$(CC) -c sbio.c $(CFLAGS) -DSB_DEBUG

clean:
	rm sandbox *.o
