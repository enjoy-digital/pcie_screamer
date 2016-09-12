
CC=gcc
CFLAGS_DLL =-Wall -O0 -g -shared -Wl,--subsystem,windows -DDLL
LIBS= FTD3XXLibrary/Win32/FTD3XX.lib

all: libft601.dll
	

libft601.dll: FT601.c
	$(CC) -o $@ $(CFLAGS_DLL) $^ $(LIBS)

clean:
	rm -f libft601.dll


