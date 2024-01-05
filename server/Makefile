# Compiler and flags
CC ?= $(CROSS_COMPILE)gcc
# The space after ?= is important !!!
CFLAGS ?=-Wall -Werror
LDFLAGS ?=-pthread

# Source files
SRCS = aesdsocket.c

# Object files
OBJS = $(SRCS:.c=.o)

# Output binary
TARGET = aesdsocket

# Default target
all: $(TARGET)

.PHONY: all clean distclean
# Clean target
distclean: clean

clean:
	rm -f $(OBJS) $(TARGET)
