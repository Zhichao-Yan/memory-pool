CC = gcc
CPP = g++
CFLAGS = -Wall -lpthread
OUTDIR := ./bin
THREAD_SAFE = -D MP_THREAD

ALL: test

test: mp.c
	@if [ ! -f $(OUTDIR)/test ]; then \
	    $(CC) $(CFLAGS) test.c mp.c -o $(OUTDIR)/$@; \
	fi  
	@$(OUTDIR)/test

clean:
	rm -f $(OUTDIR)/*
.PHONY: ALL clean

