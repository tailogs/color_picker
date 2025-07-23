# Makefile
CC = gcc
CFLAGS = -Wall -Wextra -mwindows
LDFLAGS = -mwindows -Wl,--subsystem,windows -lgdi32 -lmsimg32 -lcomctl32
OBJ = main.o resource.o
OUT = color_picker.exe

all: $(OUT)

$(OUT): $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

main.o: main.c
	$(CC) $(CFLAGS) -c $< -o $@

resource.o: resource.rc
	windres $< -o $@

clean:
	del $(OBJ) $(OUT)