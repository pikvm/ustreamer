CC = gcc
CFLAGS = -c -Wall -Wextra -DDEBUG
LDFLAGS =
SOURCES = $(shell ls src/*.c)
OBJECTS = $(SOURCES:.c=.o)
PROG = ustreamer


all: $(SOURCES) $(PROG)


$(PROG): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@


.c.o:
	$(CC) $(CFLAGS) $< -o $@


clean:
	rm -f src/*.o $(PROG)
