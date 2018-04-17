/*
  This example program provides a trivial server program that listens for TCP
  connections on port 9995.  When they arrive, it writes a short message to
  each client connection, and closes each connection once it is flushed.

  Where possible, it exits cleanly in response to a SIGINT (ctrl-c).
*/
//显示客户端请求，并且原封不动发回去

#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#ifndef _WIN32
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

#define min(a,b) (a>b?b:a)  // C语言竟然没有取小值函数


static const char MESSAGE[] = "Hello, World!\n";

static const int PORT = 9995;

static void listener_cb(struct evconnlistener *, evutil_socket_t,
    struct sockaddr *, int socklen, void *);
static void conn_readcb(struct bufferevent *bev, void *user_data);
static void conn_writecb(struct bufferevent *, void *);
static void conn_eventcb(struct bufferevent *, short, void *);
static void signal_cb(evutil_socket_t, short, void *);

int
main(int argc, char **argv)
{
	struct event_base *base;
	struct evconnlistener *listener;
	struct event *signal_event;

	struct sockaddr_in sin;
#ifdef _WIN32
	WSADATA wsa_data;
	WSAStartup(0x0201, &wsa_data);
#endif

	base = event_base_new();
	if (!base) {
		fprintf(stderr, "Could not initialize libevent!\n");
		return 1;
	}

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(PORT);
	listener = evconnlistener_new_bind(base, listener_cb, (void *)base,
	    LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE, -1,
	    (struct sockaddr*)&sin,
	    sizeof(sin));

	if (!listener) {
		fprintf(stderr, "Could not create a listener!\n");
		return 1;
	}

	signal_event = evsignal_new(base, SIGINT, signal_cb, (void *)base);

	if (!signal_event || event_add(signal_event, NULL)<0) {
		fprintf(stderr, "Could not create/add a signal event!\n");
		return 1;
	}

	event_base_dispatch(base);
	evconnlistener_free(listener);
	event_free(signal_event);
	event_base_free(base);

	printf("done\n");
	return 0;
}

static void
listener_cb(struct evconnlistener *listener, evutil_socket_t fd,
    struct sockaddr *sa, int socklen, void *user_data)
{
	struct event_base *base = user_data;
	struct bufferevent *bev;

	bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
	if (!bev) {
		fprintf(stderr, "Error constructing bufferevent!");
		event_base_loopbreak(base);
		return;
	}
	bufferevent_setcb(bev, conn_readcb, conn_writecb, conn_eventcb, NULL);

	bufferevent_enable(bev, EV_READ | EV_PERSIST);

	//bufferevent_write(bev, MESSAGE, strlen(MESSAGE));  //写给客户端显示
}

//读取一行输入并返回
static void conn_readcb(struct bufferevent *bev, void *user_data)
{
    //MY_SIMPLE_LOG_DEBUG("conn_readcb!\n");

    //只有读到一行完整行才进行处理 所以这里需要检查缓冲区是否存在换行符
    //如果存在才往下走
    struct evbuffer *input = bufferevent_get_input(bev);
    struct evbuffer_ptr ptrInput;
    if ((ptrInput = evbuffer_search(input , "\n", 1, NULL)).pos == -1)
    {
        return;
    }

    char line[1024] = {0};
    int nRead = 0; //已读取字节数
    int nExpect = ptrInput.pos + 1; //期望读取的字节数
    //把这一行读取出来（如果大于1024则需要分次读取）
    while (nRead < nExpect)
    {
        int nLeft = nExpect - nRead;
        int nReadOnce = min(nLeft, sizeof(line) - 1);

        int n = bufferevent_read(bev, line, nReadOnce);
        //输出客户端请求
        printf("%s",line);
        if (n <= 0)
        {
            //MY_SIMPLE_LOG_ERROR("expect to read %d bytes,but get %d.", nReadOnce, n);
            break;
        }
        line[n] = '\0';
        //把待发送数据添加到发送缓冲中，这里不会立即发送数据，要等conn_readcb返回后才会发送的
        bufferevent_write(bev, line, n);
        nRead += nReadOnce;
        //MY_SIMPLE_LOG_DEBUG("n = %d nRead = %d nExpect = %d! line = [%s]", n, nRead, nExpect, line);
    }

    //启动写事件，发送缓冲内容 //写事件是默认开启的，这里不需要额外设置
    //bufferevent_enable(bev, EV_WRITE | EV_PERSIST);

    //为避免粘包，这里要判断缓冲里面是否还有剩余数据，如果有要特别处理
    //这是因为libevent是边缘触发的，而不是水平触发
    if (evbuffer_get_length(input) != 0)
    {
        //本来是想直接激活读事件
        //但是发现要访问bufferevent未公开的成员，就不这样搞了
        //event_active(&(bev->ev_read), EV_READ, 1);
        conn_readcb(bev, user_data);
    }
}

static void//服务器端显示
conn_writecb(struct bufferevent *bev, void *user_data)
{
	struct evbuffer *output = bufferevent_get_output(bev);

	if (evbuffer_get_length(output) == 0) {
		printf("flushed answer\n");
		bufferevent_free(bev);
	}
}

static void
conn_eventcb(struct bufferevent *bev, short events, void *user_data)
{
	if (events & BEV_EVENT_EOF) {
		printf("Connection closed.\n");
	} else if (events & BEV_EVENT_ERROR) {
		printf("Got an error on the connection: %s\n",
		    strerror(errno));/*XXX win32*/
	}
	/* None of the other events can happen here, since we haven't enabled
	 * timeouts */
	bufferevent_free(bev);
}

static void
signal_cb(evutil_socket_t sig, short events, void *user_data)
{
	struct event_base *base = user_data;
	struct timeval delay = { 2, 0 };

	printf("Caught an interrupt signal; exiting cleanly in two seconds.\n");

	event_base_loopexit(base, &delay);
}
