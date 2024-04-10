CC = gcc
CPP = g++
CFLAGS = -Wall -lpthread
OUTDIR := ./bin
THREAD_SAFE = -D MP_THREAD

ALL: test

test: mp.c
    if [ ! -f $(OUTDIR)/test ]; then \
        $(CC) $(CFLAGS) test.c mp.c -o $(OUTDIR)/$@; \
    fi  
    $(OUTDIR)/test

clean:
	rm -f $(OUTDIR)/*
.PHONY: ALL clean


# run_single_test:
# 	$(CPP) $(GCCFLAG) $(MAIN_SOURCES) $(SOURCES) -o $(MAIN_OUTPUT).out
# 	./$(MAIN_OUTPUT).out

# # 多线程
# run_multi_test:
# 	$(CPP) $(GCCFLAG) $(MAIN_SOURCES) $(SOURCES) $(THREAD_SAFE) -o $(MAIN_OUTPUT).out
# 	./$(MAIN_OUTPUT).out