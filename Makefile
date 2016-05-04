CC=/usr/bin/gcc
# TODO: have a configure script to alter PYCONFIG in order to alter what python is used
PYCONFIG=/usr/bin/python3-config
PKG_CONFIG=/usr/bin/pkg-config
CFLAGS=$(shell $(PKG_CONFIG) --cflags json-c) $(shell $(PYCONFIG) --cflags) -DSB_DEBUG
LDFLAGS=$(shell $(PKG_CONFIG) --libs json-c) $(shell $(PYCONFIG) --ldflags) -lseccomp

all: sandbox sblibc.so

sandbox: sandbox.o
	$(CC) -o sandbox sandbox.o $(LDFLAGS)

sandbox.o: sandbox.c sbcontext.h sbdebug.h
	$(CC) -c sandbox.c $(CFLAGS)

sblibc.so: sblibc.o sbio.o
	$(CC) -o sblibc.so -shared $(LDFLAGS)

sblibc.o: sblibc.c sbcontext.h sblibc.h
	$(CC) -c sblibc.c $(CFLAGS)

sbio.o: sbio.c sblibc.h
	$(CC) -c sbio.c $(CFLAGS)

clean:
	rm sandbox sblibc.so *.o
