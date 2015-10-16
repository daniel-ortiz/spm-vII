#CFLAGS=-g -O0
CC=gcc
CFLAGS=-O0 -g -std=gnu99 -lpthread -lnuma -fdiagnostics-color -DSTANDALONE=1

all: sampling-core.o perf_helpers.o sample_processing.o control.o
	$(CC) -o spm sampling-core.o perf_helpers.o sample_processing.o control.o $(CFLAGS)
	
control.o:	control.c spm.h
	$(CC) -c control.c $(CFLAGS)
	
sampling-core.o: sampling-core.c  spm.h
	$(CC) -c sampling-core.c  $(CFLAGS)
	
perf_helpers.o: perf_helpers.c spm.h
	$(CC) -c perf_helpers.c  $(CFLAGS)

sample_processing.o: sample_processing.c spm.h
	$(CC) -c sample_processing.c  $(CFLAGS)
		
clean:
	rm -f *.o 
