# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g

# Libraries
LIBS = -lmodbus -lcurl -ljson-c -lgpiod

# Target executable
TARGET = pzem

# Source files
SRCS = pzem.c

# Object files
OBJS = $(SRCS:.c=.o)

# Default target
all: $(TARGET)

# Link object files to create the executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS)

# Compile source files into object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean target to remove object files and the executable
clean:
	rm -f $(OBJS) $(TARGET)

