# @author     Chloe Kelly
# @file       Makefile
# 


CC = /opt/gcc-8.3.0/bin/g++
CFLAGS = -g

all: server client

server: p3ser.cpp p3.hpp
	$(CC) $(CFLAGS) -o server p3ser.cpp p3.hpp

client: p3cli.cpp p3.hpp
	$(CC) $(CFLAGS) -o client p3cli.cpp p3.hpp

clean:
	rm -rf *~ server client log.ser log.cli
