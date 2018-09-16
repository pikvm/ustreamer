CC = gcc
CFLAGS = -c -O3 -Wall -Wextra -pthread
LDFLAGS = -ljpeg -pthread
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
