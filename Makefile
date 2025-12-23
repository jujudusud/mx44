# Simple Makefile updated to use GTK4
CC := gcc
SRCDIR := src
SRCS := $(wildcard $(SRCDIR)/*.c)
OBJS := $(SRCS:.c=.o)
TARGET := mx44

# Use pkg-config to get GTK4 flags
GTK_CFLAGS := $(shell pkg-config --cflags gtk4)
GTK_LIBS   := $(shell pkg-config --libs gtk4)

CFLAGS := -Wall -O2 $(GTK_CFLAGS)
LDFLAGS := $(GTK_LIBS)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) $(OBJS) $(TARGET)
