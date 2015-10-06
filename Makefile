#CFLAGS=-g -O0
CC=gcc
CFLAGS=-O2 -g -std=gnu99 -lpthread -fdiagnostics-color

all: launcher.o perf_helpers.o sample_processing.o
	$(CC) -o spm launcher.o perf_helpers.o sample_processing.o  $(CFLAGS)
	
launcher.o: launcher.c  spm.h
	$(CC) -c launcher.c  $(CFLAGS)
	
perf_helpers.o: perf_helpers.c spm.h
	$(CC) -c perf_helpers.c  $(CFLAGS)

sample_processing.o: sample_processing.c spm.h
	$(CC) -c sample_processing.c  $(CFLAGS)
		
clean:
	rm -f *.o 
