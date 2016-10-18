
CC=gcc
INCLUDES=-I./windows -I./libevent/include
LIBS=windows/FTD3XXLibrary/Win32/FTD3XX.lib

all:
	$(CC) usb2udp.c FT601.c -o usb2udp $(INCLUDES) ./libevent/.libs/*.a -lws2_32 $(LIBS)
	cp windows/FTD3XXLibrary/Win32/FTD3XX.dll FTD3XX.dll