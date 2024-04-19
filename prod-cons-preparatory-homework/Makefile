##
# prod-cons
#
# @file
# @version 0.1
SHELL := /bin/bash
CC = gcc
ARMCC = /usr/local/cross-pi-gcc-10.3.0-0/bin/arm-linux-gnueabihf-gcc
CFLAGS = -O3 -lpthread -lm
RM = rm -f

all: prodcons prodcons-arm

prodcons: prod-cons.c
	@$(CC) $^ -o prod-cons $(CFLAGS)

prodcons-arm: prod-cons.c
	@$(ARMCC) $^ -o prod-cons-arm $(CFLAGS)

clean:
	@$(RM) prod-cons prod-cons-arm
# end
