#include <stdio.h>
#ifndef WIN32
#include <sys/socket.h>
#endif

#include <event2/event.h>
#include "FTD3XX.h"

FT_HANDLE ft_handle;

#define FT_STREAM_PREAMBLE 0x5aa55aa5
#define FT_STREAM_HEADER_SIZE 12
#define FT_STREAM_PORTS 256

#define DEBUG

struct ft_stream {
    short udp_port;
    unsigned char usb_port;
    int fd;
    struct event_base * base;
    struct sockaddr_in client_addr;
};

struct ft_stream ft_streams[FT_STREAM_PORTS];

void udp_callback(int fd, short event, void *arg)
{
    struct ft_stream *s = (struct ft_stream*) arg;

    char udp_rxbuf[1500];
    int udp_rxlen;
    int udp_rxsize;

    int usb_txheader[FT_STREAM_HEADER_SIZE/4];
    unsigned char usb_txbuf[1500 + FT_STREAM_HEADER_SIZE];

    udp_rxsize = sizeof(struct sockaddr);
    udp_rxlen = recvfrom(fd, udp_rxbuf, sizeof(udp_rxbuf), 0,
      (struct sockaddr *)&s->client_addr, &udp_rxsize);

    if (udp_rxlen == -1) {
        perror("recvfrom()");
    } else if (udp_rxlen > 0) {
        usb_txheader[0] = FT_STREAM_PREAMBLE;
        usb_txheader[1] = s->usb_port;
        usb_txheader[2] = udp_rxlen;
        memcpy(usb_txbuf, usb_txheader, FT_STREAM_HEADER_SIZE);
        memcpy(usb_txbuf + FT_STREAM_HEADER_SIZE, udp_rxbuf, udp_rxlen);
        FT601_Write(ft_handle, usb_txbuf, udp_rxlen + FT_STREAM_HEADER_SIZE);
#ifdef DEBUG
        int i;
        printf("usb_tx: ");
        for(i=0; i<udp_rxlen + FT_STREAM_HEADER_SIZE; i++)
            printf("%02x", usb_txbuf[i]);
        printf("\n");
#endif
    }
}

int new_usb_udp_bridge(struct event_base *base, unsigned char usb_port,
  short udp_port)
{
    struct sockaddr_in socket_sin;
    int socket_fd;
    char socket_flag = 1;

    struct event *ev;

    struct ft_stream *s;

    s = &ft_streams[usb_port];
    s->udp_port = udp_port;
    s->usb_port = usb_port;
    s->base = base;

    memset(&socket_sin, 0, sizeof(socket_sin));
    socket_sin.sin_family = AF_INET;
    socket_sin.sin_port = htons(udp_port);

    if ((socket_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket()");
        return -1;
    }
    s->fd = socket_fd;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR,
      &socket_flag, sizeof(char)) < 0) {
        perror("setsockopt()");
        return 1;
    }
    if (bind(socket_fd, (struct sockaddr *)&socket_sin,
      sizeof(struct sockaddr)) < 0) {
        perror("bind()");
        return -1;
    } else {
        printf("new bridge: usb_port[%u] <--> udp_port[%u]\n",
          usb_port, udp_port);
    }

    ev = event_new(base, socket_fd, EV_READ | EV_PERSIST, &udp_callback, s);
    event_add(ev, NULL);
}


int usb_extract(char *usb_rxbuf, int usb_rxlen)
{
    int i;
    int usb_preamble = FT_STREAM_PREAMBLE;
    int usb_port;

    char *udp_txbuf;
    int udp_txlen;
    int size = sizeof(struct sockaddr);

    if(usb_rxlen < FT_STREAM_HEADER_SIZE)
        return 0;
    if(usb_rxbuf == NULL)
        return 0;
    for(i=0; i<usb_rxlen-4; i++) {
        if(!memcmp(usb_rxbuf + i, &usb_preamble, 4)) {
#ifdef DEBUG
            printf("usb_preamble found at %d\n", i);
#endif
	          break;
	      }
    }
    if(i == usb_rxlen - 4) {
#ifdef DEBUG
        printf("usb_preamble not found");
#endif
        return 0;
    }
    /* get rid of whatever before usb_preamble */
    if(i != 0) {
#ifdef DEBUG
        printf("usb_preamble found at %d\n", i);
#endif
        return i;
    }
    /* get port */
    memcpy(&usb_port, usb_rxbuf + 4, 4);
    /* get length */
    memcpy(&udp_txlen, usb_rxbuf + 8, 4);

    /* get data and send over udp */
    udp_txbuf = usb_rxbuf + FT_STREAM_HEADER_SIZE;
    if(udp_txlen + FT_STREAM_HEADER_SIZE <= usb_rxlen)
    {
        if(usb_port < FT_STREAM_PORTS) {
	          sendto(ft_streams[usb_port].fd, udp_txbuf, udp_txlen, 0,
              (struct sockaddr *)&ft_streams[usb_port].client_addr, size);
	      }
        return udp_txlen + FT_STREAM_HEADER_SIZE;
    }

    return 0;
}

void usb_callback(PVOID pvCallbackContext,
  E_FT_NOTIFICATION_CALLBACK_TYPE eCallbackType, PVOID pvCallbackInfo)
{
    unsigned char *usb_rxbuf;
    int usb_rxlen;

    static unsigned char *usb_cbfifo=NULL;
    unsigned char *usb_cbbuf;
    static unsigned int usb_cblen = 0;

    FT_NOTIFICATION_CALLBACK_INFO_DATA *data = pvCallbackInfo;
    usb_rxbuf = malloc(data->ulRecvNotificationLength);
    usb_rxlen = FT601_Read(ft_handle, usb_rxbuf, data->ulRecvNotificationLength);

 #ifdef DEBUG
    int i;
    printf("usb_rx: ", usb_rxlen);
    for(i = 0; i < usb_rxlen; i++)
        printf("%02x", usb_rxbuf[i]);
    printf("\n");
#endif

    usb_cbbuf = malloc(usb_cblen + usb_rxlen);
    memcpy(usb_cbbuf, usb_cbfifo, usb_cblen);
    memcpy(usb_cbbuf + usb_cblen, usb_rxbuf, usb_rxlen);
    usb_cblen += usb_rxlen;
    free(usb_rxbuf);
    free(usb_cbfifo);
    usb_cbfifo = usb_cbbuf;

    do {
        usb_rxlen = usb_extract(usb_cbfifo, usb_cblen);
        if(usb_rxlen == usb_cblen) {
	         free(usb_cbfifo);
	         usb_cbfifo = NULL;
	         usb_cblen = 0;
        } else if (usb_rxlen) {
	         usb_cbbuf = malloc(usb_cblen - usb_rxlen);
	         memcpy(usb_cbbuf, usb_cbfifo + usb_rxlen, usb_cblen - usb_rxlen);
           usb_cblen = usb_cblen - usb_rxlen;
           free(usb_cbfifo);
           usb_cbfifo = usb_cbbuf;
        }
    } while (usb_rxlen);
}

int main(int argc, char **argv)
{
    struct event_base *base;

#ifdef WIN32
    WSADATA wsa_data;
    WSAStartup(0x0201, &wsa_data);
#endif

    base = event_base_new();
    if (!base) {
        return 1;
    }

    FT601_Open(&ft_handle);
    FT_SetNotificationCallback(ft_handle, &usb_callback, NULL);

    new_usb_udp_bridge(base, 0, 1234);
    new_usb_udp_bridge(base, 1, 2345);

    event_base_dispatch(base);
    event_base_free(base);

    return 0;
}
