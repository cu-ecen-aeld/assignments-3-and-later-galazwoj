# Compiler and flags
CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -Wall -Werror
LDFLAGS ?= -pthread -lrt

.c.o:
	$(CC) -c $(CFLAGS) $^

# Source files
SRCS = aesdsocket.c

# Object files
OBJS = $(SRCS:.c=.o)

# Output binary
TARGET = aesdsocket

# Default target
all: $(TARGET)

$(TARGET): $(OBJS)
#	$(CC) -o $@ -pthread $^ -lrt
	$(CC) -o $@ $^ $(LDFLAGS)

.PHONY: all clean distclean
# Clean target
distclean: clean

clean:
	rm -f $(OBJS) $(TARGET)

