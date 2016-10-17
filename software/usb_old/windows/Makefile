
CC=gcc
CFLAGS_DLL =-Wall -O0 -g -shared -Wl,--subsystem,windows -DDLL
LIBS= FTD3XXLibrary/Win32/FTD3XX.lib

all: libft601.dll


libft601.dll: FT601.c
	$(CC) -o $@ $(CFLAGS_DLL) $^ $(LIBS)
	cp libft601.dll ../libft601.dll
	cp FTD3XXLibrary/Win32/FTD3XX.dll ../FTD3XX.dll

clean:
	rm -f libft601.dll


