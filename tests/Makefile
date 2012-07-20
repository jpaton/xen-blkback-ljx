CC = gcc
CFLAGS = -g -O2 -std=c99
LFLAGS = -lrt
OBJECTS = main.o
EXENAME = test

$(EXENAME) : $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o $(EXENAME) $(LFLAGS)

%.o : %.c
	$(CC) $(CFLAGS) -c $<
