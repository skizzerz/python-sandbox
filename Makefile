CC=/usr/bin/gcc
# TODO: have a configure script to alter PYCONFIG in order to alter what python is used
PYCONFIG=/usr/bin/python3.5-config
PKG_CONFIG=/usr/bin/pkg-config
CFLAGS=$(shell $(PKG_CONFIG) --cflags json-c) $(shell $(PYCONFIG) --cflags) -DSB_DEBUG
LDFLAGS=$(shell $(PKG_CONFIG) --libs json-c) $(shell $(PYCONFIG) --ldflags) -lseccomp

all: libsbpreload.so sandbox

sandbox: sandbox.o sblibc.o sbio.o
	$(CC) -o sandbox sandbox.o sblibc.o sbio.o $(LDFLAGS)

sandbox.o: sandbox.c sbcontext.h
	$(CC) -c sandbox.c $(CFLAGS)

sblibc.o: sblibc.c sbcontext.h sblibc.h
	$(CC) -c sblibc.c $(CFLAGS)

sbio.o: sbio.c sbcontext.h sblibc.h
	$(CC) -c sbio.c $(CFLAGS)

libsbpreload.so: libsbpreload.o
	$(CC) -o libsbpreload.so libsbpreload.o -shared $(LDFLAGS)

libsbpreload.o: libsbpreload.c
	$(CC) -c libsbpreload.c $(CFLAGS)

clean:
	rm sandbox libsbpreload.so *.o
