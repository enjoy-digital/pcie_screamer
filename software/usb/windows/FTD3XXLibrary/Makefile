
CROSS_COMPILE := i686-w64-mingw32-

AS = $(CROSS_COMPILE)as
LD = $(CROSS_COMPILE)ld
CC = $(CROSS_COMPILE)gcc-posix
CXX = $(CROSS_COMPILE)g++-posix
OBJDUMP = $(CROSS_COMPILE)objdump

DEMO0=test.exe
LIBS = -L . -lftd3xx -static


COMMON_FLAGS = -ffunction-sections -finline-limit=65535 -fmerge-all-constants \
			   -fno-stack-protector $(ARCH)
COMMON_CFLAGS = -g -O3 -Wall -Wextra $(COMMON_FLAGS) -fno-stack-check
CFLAGS = -std=c99  $(COMMON_CFLAGS) -D_POSIX_C_SOURCE
CXXFLAGS = -std=c++11 $(COMMON_CFLAGS)

all: $(DEMO0) $(DEMO1) $(DEMO2)
	
$(DEMO0): test.o
	$(CC) -Wl,--gc-sections $(COMMON_FLAGS) -o $@ $^ $(LIBS) -lstdc++


clean:
	-rm -f test.o $(DEMO0) 
