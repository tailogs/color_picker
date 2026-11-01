CC      = gcc
WINDRES = windres

CFLAGS  = -Wall -Wextra -O2 -g
LDFLAGS = -mwindows
LDLIBS  = -lgdi32 -lmsimg32 -lcomctl32 -lshell32 -luser32 -lm

OBJ = main.o resource.o
OUT = color_picker.exe

all: $(OUT)

$(OUT): $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS) $(LDLIBS)

main.o: main.c resource.h
	$(CC) $(CFLAGS) -c $< -o $@

resource.o: resource.rc resource.h
	$(WINDRES) $< -o $@

clean:
	-del /Q $(OBJ) $(OUT) 2>nul

.PHONY: all clean