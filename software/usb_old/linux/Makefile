rw:
	gcc -Wl,--gc-sections -ffunction-sections -finline-limit=65535 -fmerge-all-constants -fno-stack-protector -m64 -o rw rw.c -L . -lftd3xx -pthread -lrt

test:
	./rw 1024 1024 1 0

clean:
	-rm -f rw.o rw
