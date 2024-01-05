# Compiler and flags
CC ?= $(CROSS_COMPILE)gcc
CFLAGS += -Wall -Werror
LDFLAGS += -pthread
LDLIBS += -lrt

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

