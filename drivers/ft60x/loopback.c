#include <stdio.h>
#include <event2/event.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

void udp_callback(int fd, short event, void *arg)
{
	int val;

	char *buf = malloc(1024 * 128);
	val = read(fd, buf, 4096);
	printf("GOT EVENT %d!\n", val);

	if (val > 0)
		write(fd, buf, val);
	free(buf);
}

int main(int argc, char **argv)
{
	struct event_base *base;
	struct event *ev;

	int in;
	unsigned char *buf;
	int i;

	buf = malloc(128 * 1024);

	for (i = 0; i < 128 * 1024; i++) {
		buf[i] = 'A' + (i % 64);
	}

	in = open("/dev/ft60x0", O_RDWR | O_CLOEXEC);

	base = event_base_new();
	if (!base) {
		return 1;
	}

	ev = event_new(base, in, EV_READ | EV_PERSIST, &udp_callback, NULL);
	event_add(ev, NULL);

	write(in, buf, 2048);
	write(in, buf, 2048);
	write(in, buf, 2048);
	write(in, buf, 2048);

	event_base_dispatch(base);
	event_base_free(base);

	return 0;
}
