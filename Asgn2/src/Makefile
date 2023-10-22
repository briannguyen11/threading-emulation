CC     = gcc

CFLAGS  = -Wall -g -I .

LD     = gcc

LDFLAGS  = -Wall -g 

PROGS    = nums snakes hungry

SNAKEOBJS  = randomsnakes.o 

HUNGRYOBJS = hungrysnakes.o 

NUMOBJS    = numbersmain.o

OBJS    = $(NUMOBJS) $(SNAKEOBJS) $(HUNGRYOBJS)  

SRCS    = numbersmain.c randomsnakes.c hungrysnakes.c

HDRS    = 

EXTRACLEAN = core $(PROGS)

all:     $(PROGS)

allclean: clean
    @rm -f $(EXTRACLEAN)

clean:    
    rm -f $(OBJS) *~ TAGS

snakes: randomsnakes.o libLWP.a libsnakes.so util.o
    $(LD) $(LDFLAGS) -o snakes randomsnakes.o util.o -L. -lncurses -lsnakes -lLWP

hungry: hungrysnakes.o libLWP.a libsnakes.so
    $(LD) $(LDFLAGS) -o hungry hungrysnakes.o -L. -lncurses -lsnakes -lLWP

nums: numbersmain.o libLWP.a 
    $(LD) $(LDFLAGS) -o nums numbersmain.o -L. -lLWP

hungrysnakes.o: lwp.h snakes.h

randomsnakes.o: lwp.h snakes.h

numbermain.o: lwp.h

libLWP.a: lwp.c rr.c util.c
    gcc -c rr.c util.c lwp.c magic64.S 
    ar r libLWP.a util.o lwp.o rr.o magic64.o randomsnakes.o
    rm lwp.o

submission: lwp.c rr.c util.c Makefile README
    tar -cf project2_submission.tar lwp.c rr.c Makefile README
    gzip project2_submission.tar

	