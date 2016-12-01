# the compiler: gcc for C program, define as g++ for C++
CC = gcc

# compiler flags:
#  -g    adds debugging information to the executable file
#  -Wall turns on most, but not all, compiler warnings
CFLAGS  = -g -Wall -O3 `pkg-config --cflags --libs jack sndfile`
PREFIX=/usr/local/
# the build target executable:
TARGET = jack-pipe

all: $(TARGET)

$(TARGET): $(TARGET).c
	$(CC) -o $(TARGET) $(TARGET).c $(CFLAGS)

clean:
	rm $(TARGET)

install: $(TARGET)
	mkdir -p $(PREFIX)/bin
	cp $(TARGET) $(PREFIX)/bin/

uninstall:
	rm -f $(PREFIX)/bin/$(TARGET)
