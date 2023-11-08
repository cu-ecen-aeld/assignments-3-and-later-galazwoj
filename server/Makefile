# Compiler and flags
CC = $(CROSS_COMPILE)gcc

# Source files
SRCS = aesdsocket.c

# Object files
OBJS = $(SRCS:.c=.o)

# Output binary
TARGET = aesdsocket

# Default target
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^

.PHONY: all clean distclean
# Clean target
distclean: clean

clean:
	rm -f $(OBJS) $(TARGET)

