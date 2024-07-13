##
# Finnhub websockets
#
# @file
# @version 0.1

SHELL := /bin/bash
CC := gcc
ARMCC = /usr/local/cross-pi-gcc-10.3.0-0/bin/arm-linux-gnueabihf-gcc
LDFLAGS = -lwebsockets -lpthread
RM = rm -rf

.PHONY: all
all: main
main: include/*.c main.c
	arm-linux-gnueabihf-gcc $^ -o $@ ${LDFLAGS}
clean:
	@${RM} main
# end
