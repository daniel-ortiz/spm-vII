#CFLAGS=-g -O0
CC=gcc-4.8
CFLAGS=-O2 -g -std=gnu99 -lpthread -lnuma -DSTANDALONE=1

all: sampling-core.o perf_helpers.o sample_processing.o control.o force-remote.o
	$(CC) -o spm sampling-core.o perf_helpers.o sample_processing.o control.o force-remote.o $(CFLAGS)
	
control.o:	control.c spm.h
	$(CC) -c control.c $(CFLAGS)
	
sampling-core.o: sampling-core.c  spm.h
	$(CC) -c sampling-core.c  $(CFLAGS)
	
perf_helpers.o: perf_helpers.c spm.h
	$(CC) -c perf_helpers.c  $(CFLAGS)

sample_processing.o: sample_processing.c spm.h
	$(CC) -c sample_processing.c  $(CFLAGS)

force-remote.o: force-remote.c spm.h
	$(CC) -c force-remote.c $(CFLAGS)
		
clean:
	rm -f *.o 
