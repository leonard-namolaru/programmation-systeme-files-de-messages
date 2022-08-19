CC=gcc
CFLAGS=-Wall -g -std=c11 -pedantic -D_POSIX_C_SOURCE # Les fichiers doivent être compilés avec les options -Wall -g -pedantic
CPPFLAGS= -D_GNU_SOURCE -DNDEBUG
LDLIBS=-lrt -pthread  # Linux

-D_POSIX_C_SOURCE=200809L

ALL = main

all : $(ALL)

m_file.o : m_file.c m_file.h

main : main.c m_file.o

clean:
	rm -rf *~
cleanall:
	rm -rf *~ $(ALL) *.o