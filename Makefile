CC = gcc
#CFLAGS = -g -Wall -O3 -fPIC
CFLAGS = -g3 -Wall -O0 -fPIC
LDFLAGS = -shared
INCLUDES = -I/home/sshyun/ss/hemem/linux/usr/include/
LIBS = -lm -lpthread
HEMEM_LIBS = $(LIBS) -ldl -L/home/sshyun/ss/hemem/syscall_intercept/cmake -lsyscall_intercept -L/home/sshyun/ss/hemem/Hoard/src -lhoard

default: libhemem.so mig-test 

all: hemem-libs

hemem-libs: libhemem.so

mig-test: mig-test.o
	$(CC) $(CFLAGS) $(INCLUDES) -o mig-test mig-test.o $(HEMEM_LIBS) -L/home/sshyun/dma-test/use-uffd -lhemem

test: test.o res-record.o timer.o
	$(CC) $(CFLAGS) $(INCLUDES) -pthread -o test test.o res-record.o timer.o

mig-test.o: mig-test.c
	$(CC) $(CFLAGS) $(INCLUDES) -c mig-test.c

libhemem.so: hemem.o pebs.o timer.o interpose.o fifo.o res-record.o
	$(CC) $(LDFLAGS) -o libhemem.so hemem.o timer.o interpose.o pebs.o fifo.o res-record.o $(HEMEM_LIBS)

hemem.o: hemem.c hemem.h pebs.h interpose.h fifo.h res-record.h
	$(CC) $(CFLAGS) $(INCLUDES) -D ALLOC_HEMEM -c hemem.c -o hemem.o
	
interpose.o: interpose.c interpose.h hemem.h
	$(CC) $(CFLAGS) $(INCLUDES) -c interpose.c

timer.o: timer.c timer.h
	$(CC) $(CFLAGS) $(INCLUDES) -c timer.c

pebs.o: pebs.c pebs.h hemem.h fifo.h res-record.h
	$(CC) $(CFLAGS) $(INCLUDES) -c pebs.c

fifo.o: fifo.c fifo.h hemem.h
	$(CC) $(CFLAGS) $(INCLUDES) -c fifo.c

res-record.o: res-record.c res-record.h timer.h
	$(CC) $(CFLAGS) $(INCLUDES) -c res-record.c

test.o: test.c res-record.h timer.h
	$(CC) $(CFLAGS) $(INCLUDES) -c test.c

clean:
	$(RM) *.o *.so
