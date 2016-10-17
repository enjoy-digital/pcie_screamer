#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#ifndef WIN32
#include <netinet/in.h>
# ifdef _XOPEN_SOURCE_EXTENDED
#  include <arpa/inet.h>
# endif
#include <sys/socket.h>
#endif

#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/event.h>
#include "FTD3XX.h"

FT_HANDLE ftHandle;

#define STRM_MAGIC 0x5aa55aa5
struct ft_stream{
  short port;
  int fd;
  unsigned char canal;
  struct event_base *base;
  struct sockaddr_in  client_addr;
};

struct ft_stream streams[256];



void read_cb(int fd, short event, void *arg)
{
  struct ft_stream *st = (struct ft_stream*)arg;
  char                buf[1500];
  int                 len;
  int                 size = sizeof(struct sockaddr);
  int header[3];
  unsigned char *out;
  int i;

  memset(buf, 0, sizeof(buf));
  len = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&st->client_addr, &size);

  if (len == -1) {
    perror("recvfrom()");
  } else if (len == 0) {
    printf("Connection Closed\n");
  } else {
    printf("Read: len [%d] - content [%s]\n", len, buf);
    //header[0] = ntohl(STRM_MAGIC);
    //header[1] = ntohl(st->canal);
    //header[2] = ntohl(len);
    header[0] = STRM_MAGIC;
    header[1] = st->canal;
    header[2] = len;
    out = malloc(len + 12);
    memcpy(out, header, 12);
    memcpy(out + 12, buf, len);
    FT601_Write(ftHandle, out, len + 12);
    printf("sending:\n");
    for(i = 0; i < len + 12; i++)
      printf("%02x ", out[i]);
    printf("\n\n");
    free(out);
    /* Echo */
    //sendto(fd, buf, len, 0, (struct sockaddr *)&client_addr, size);
  }
}

int new_canal(struct event_base *base, unsigned char canal, short port)
{
  struct sockaddr_in sin;
  struct evconnlistener *listener;
  struct event *ev;
  int sock_fd;
  char flag = 1;
  struct ft_stream *st;

  st = &streams[canal];
  memset(st, 0, sizeof(struct ft_stream));
  st->port = port;
  st->canal = canal;
  st->base = base;


  memset(&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_port = htons(port);

  if ((sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("socket()");
    return -1;
  }
  st->fd = sock_fd;
  if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(char)) < 0) {
    perror("setsockopt()");
    return 1;
  }

  if (bind(sock_fd, (struct sockaddr *)&sin, sizeof(struct sockaddr)) < 0) {
    perror("bind()");
    return -1;
  } else {
    printf("bind() success - [%s] [%u]\n", "local", port);
  }

  ev = event_new(base, sock_fd, EV_READ | EV_PERSIST, &read_cb, st);
  event_add(ev, NULL);
}


int extract_packet(char *buf, int len)
{
  char *tmp=buf;
  int i;
  //int magic = ntohl(STRM_MAGIC);
  int magic = STRM_MAGIC;
  int canal;
  int buflen;
  char *out;
  int                 size = sizeof(struct sockaddr);

  if(len < 12)
    return 0;
  if(buf == NULL)
    return 0;
  for(i = 0; i < len - 4; i++)
    {

      if(!memcmp(tmp + i, &magic, 4))
	{
	  printf("found magic at %d\n", i);
	  break;
	}
    }

  if(i == len -4)
    {
      printf("didn't find magic");
	return 0;
    }
  if(i != 0) /*get rid of whatever before magic*/
    {
      printf("found magic at pos %d\n", i);
      return i;
    }
  tmp +=4;
  memcpy(&canal, tmp, 4);
  //canal=ntohl(canal);
  tmp+=4;
  memcpy(&buflen, tmp, 4);
  //buflen = ntohl(buflen);
  tmp+=4;
  printf("buflen and canal: %d %d\n", buflen, canal);
  if(buflen + 12 <= len)
    {
      printf("Extracted %d bytes for canal %d\n", buflen, canal );
      if(canal < 256)
	{
	  sendto(streams[canal].fd, tmp, buflen, 0, (struct sockaddr *)&streams[canal].client_addr, size);
	}
      for(i = 0; i < buflen; i++)
	printf("%02x ", tmp[i]);
      printf("\n");
      return buflen + 12;
    }
  return 0;
}

void ft_rd_cb(PVOID pvCallbackContext, E_FT_NOTIFICATION_CALLBACK_TYPE eCallbackType, PVOID pvCallbackInfo)
{

  unsigned char *buf;
  int ret;
  int i;
  static unsigned char *fifo=NULL;
  unsigned char *tmp, *tmp2;
  static unsigned int len = 0;
  int magic = STRM_MAGIC;
  int buflen;
  int canal;

  FT_NOTIFICATION_CALLBACK_INFO_DATA *data = pvCallbackInfo;
  printf("CALLED %d %d %02x\n", eCallbackType, data->ulRecvNotificationLength, data->ucEndpointNo);

  buf = malloc( data->ulRecvNotificationLength);
  ret = FT601_Read(ftHandle, buf, data->ulRecvNotificationLength);
  printf("ret: %d\n", ret);
  for(i = 0; i < ret; i++)
    printf("%02x ", buf[i]);
      printf("\n");

  tmp = malloc(len + ret);
  memcpy(tmp, fifo, len);
  memcpy(tmp + len, buf, ret);
  len += ret;
  free(buf);
  free(fifo);
  fifo = tmp;
  printf("New LEN = %d\n", len);
  do {
    printf("current fifo:\n");
    for(i = 0; i < len; i++)
    printf("%02x ", fifo[i]);
      printf("\n\n");

    ret = extract_packet(fifo, len);
    printf("extracted %d\n", ret);
    if(ret == len)
      {
	free(fifo);
	fifo = NULL;
	len = 0;
      }
    else if(ret)
      {
	tmp = malloc(len - ret);
	memcpy(tmp, fifo + ret, len - ret);
	len = len - ret;
	free(fifo);
	fifo = tmp;
      }
  }while(ret);

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
    fprintf(stderr, "Could not initialize libevent!\n");
    return 1;
  }

  FT601_Open(&ftHandle);
  FT_SetNotificationCallback(ftHandle, &ft_rd_cb, NULL);

  new_canal(base,0, 1234);
  new_canal(base,1, 2345);

  event_base_dispatch(base);

  event_base_free(base);

  printf("done\n");
  return 0;
}
