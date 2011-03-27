MAKE = make                                                                                                                                  
CFLAGS = -g -Wall
CC = gcc 

all: schwesterbot

schwesterbot: schwesterbot.c
	$(CC) $(CFLAGS) schwesterbot.c -o schwesterbot

 
clean:
	rm -f *.o schwesterbot

