#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

int client_count=0;

#define BUFFER_LENGTH   512
#define EPOLL_SIZE      1024
int epfd=0;
typedef int (*RCALLBACK)(int fd); // 使用类型转换   //函数指针指向回调函数

struct conn_item {
	int fd;
	
	char buffer[BUFFER_LENGTH];
	union {
		RCALLBACK accept_callback;
		RCALLBACK recv_callback;
	} recv_t;
	RCALLBACK send_callback;
};

struct conn_item connlist[BUFFER_LENGTH * BUFFER_LENGTH]={0};

//listenfd
int accept_cb(int fd);
//clientfd
int recv_cb(int fd);
int send_cb(int fd);

void set_event(int fd,int event,int flag){
    if(flag){  //1 add 0 mod
        struct epoll_event ev;
        ev.events=event;
        ev.data.fd=fd;
        epoll_ctl(epfd,EPOLL_CTL_ADD,fd,&ev);
    }else{
        struct epoll_event ev;
        ev.events=event;
        ev.data.fd=fd;
        epoll_ctl(epfd,EPOLL_CTL_MOD,fd,&ev);
    }
}

int accept_cb(int fd){   //处理新连接
    struct sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(struct sockaddr_in));
    socklen_t client_len = sizeof(client_addr);
    int clientfd = accept(fd, (struct sockaddr*)&client_addr, &client_len);

	set_event(clientfd,EPOLLIN,1);

    connlist[clientfd].fd = clientfd;
	memset(connlist[clientfd].buffer, 0, BUFFER_LENGTH);

    connlist[clientfd].recv_t.recv_callback=recv_cb;
    connlist[clientfd].send_callback=send_cb;
    // 设置新连接的套接字为非阻塞模式
    int flags = fcntl(clientfd, F_GETFL, 0);
    fcntl(clientfd, F_SETFL, flags | O_NONBLOCK);
    client_count++;
    return clientfd;
}

int recv_cb(int fd){    //收到数据
    char *buffer=connlist[fd].buffer;
    while(1){
        int count=recv(fd,buffer,BUFFER_LENGTH,0);
        if (count < 0) {
            if(errno==EAGAIN){
                //printf("数据已经接收完毕...\n");
                break;
            }else{
                close(fd);
                epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                break;
            }
        }else if (count == 0) { // disconnect   //recv返回0表示对端断开连接
            //printf("客户端已经断开连接!  \n");
            close(fd);
            epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
            client_count--;
            if(client_count<=0){
                break;
            }
        } else {
            //printf("Recv: %s, %d byte(s)\n", buffer, count);
            set_event(fd,EPOLLOUT,0);	
        }    
    }
    if(client_count<=0){
        close(epfd);
        close(fd);
        exit(0);
    } 
    return 0;       
}


int send_cb(int fd){   //发送数据
    char *buffer = connlist[fd].buffer;

	int count = send(fd, buffer, BUFFER_LENGTH, 0);
    //printf("Recv: %s, %d byte(s)\n", buffer,count);

	set_event(fd, EPOLLIN, 0);

	return count;
}

int init_server(unsigned short port) {

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);

	struct sockaddr_in serveraddr;
	memset(&serveraddr, 0, sizeof(struct sockaddr_in));

	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons(port);

	if (-1 == bind(sockfd, (struct sockaddr*)&serveraddr, sizeof(struct sockaddr))) {
		perror("bind");
		return -1;
	}
	listen(sockfd, 10);
	return sockfd;
}

int main(int argc, char *argv[]) {

    int port_count = 1;
	unsigned short port = 2049;
	int i = 0;

	epfd = epoll_create(1); // int size

	for (i = 0;i < port_count;i ++) {
		int sockfd = init_server(port + i);  // 2048, 2049, 2050, 2051 ... 2057
		connlist[sockfd].fd = sockfd;
		connlist[sockfd].recv_t.accept_callback = accept_cb;
		set_event(sockfd, EPOLLIN, 1);
	}

    struct epoll_event events[EPOLL_SIZE] = {0};

    while (1) { // mainloop();
		int nready = epoll_wait(epfd, events, 1024, -1); // 
		int i = 0;
		for (i = 0;i < nready;i ++) {
	    	int connfd = events[i].data.fd;
			if (events[i].events & EPOLLIN) { //
				int count = connlist[connfd].recv_t.recv_callback(connfd);
				//printf("recv count: %d <-- buffer: %s\n", count, connlist[connfd].rbuffer);
			} else if (events[i].events & EPOLLOUT) { 
				// printf("send --> buffer: %s\n",  connlist[connfd].wbuffer);				
			    int count = connlist[connfd].send_callback(connfd);
			}
		}
	}
    return 0;
}
