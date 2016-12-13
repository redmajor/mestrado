
# generated by ./configure.sh 

CC=gcc

CFLAGS=-fPIC -Wall -g -O3 
LDFLAGS=-g -fPIC 

PYTHONCFLAGS = -I/usr/include/python2.x

YAELCONF=linux64
YAELCFLAGS=-I/home/andreff/pqnns-unb/parallel
YAELLDFLAGS=-L/home/andreff/pqnns-unb/parallel/yael_needs -Wl,-rpath,/home/andreff/pqnns-unb/parallel/yael_needs -lyael


SWIG=swig -python

WRAPLDFLAGS=-shared
LAPACKLDFLAGS=/usr/lib/libblas.so.3gf /usr/lib/liblapack.so.3gf
LAPACKCFLAGS=-DFINTEGER=int

USEARPACK=no
ARPACKLDFLAGS=/usr/lib64/libarpack.so.2

USETHREADS=yes
THREADCFLAGS=-DHAVE_THREADS

SHAREDEXT=so
SHAREDFLAGS=-shared
YAELSHAREDFLAGS=-shared

USENUMPY=
NUMPYCFLAGS=
NUMPYSWIGFLAGS=


USEOPENMP=yes
