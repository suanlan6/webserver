#include <arpa/inet.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <iostream>
#include <sys/signal.h>
#include <memory>

#include "thread_pool.hpp"
#include "http_conn.hpp"

static const int DEFAULT_MAX_CLIENTS = 65536;
static const int DEFAULT_MAX_EVENTS = 10000;

int main(int argc, char* argv[]) {

    if (argc != 2) {
        printf("error params, need 2\n");
        exit(1);
    }

    // thread_pool
    std::unique_ptr<thread_pool<http_conn>> pool(new thread_pool<http_conn>(8, 10000));

    // clients
    std::unique_ptr<http_conn[]> http_requests(new http_conn[DEFAULT_MAX_CLIENTS]);

    // init socket
    int socketfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(socketfd != -1);

    // port reuse
    int optval = 1;
    setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    // register signal handler
    addsig(SIGPIPE, SIG_IGN);
    
    // bind socket
    const char* ip = "10.0.4.17";
    const int port = atoi(argv[1]);
    sockaddr_in sock;
    inet_pton(AF_INET, ip, &sock.sin_addr.s_addr);
    sock.sin_family = AF_INET;
    sock.sin_port = htons(port);
    socklen_t socklen = sizeof(sock);
    int ret = bind(socketfd, (sockaddr*)&sock, socklen);

    // listen to
    ret = listen(socketfd, 8);
    assert(ret != -1);

    // epoll events
    epoll_event events[DEFAULT_MAX_EVENTS];
    int epoll_fd = epoll_create(8);
    addevent(epoll_fd, socketfd, false);
    http_conn::m_epollfd = epoll_fd;
    
    int changed_num;
    // main process
    while (1) {
        changed_num = epoll_wait(epoll_fd, events, DEFAULT_MAX_EVENTS, -1);
        if (changed_num == -1) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            perror("error while epolling");
            break;
        }
        for (int i = 0; i < changed_num; ++i) {
            int client_fd = events[i].data.fd;
            if (client_fd == socketfd) {
                sockaddr_in client;
                socklen_t client_len = sizeof(client);
                ret = accept(socketfd, (sockaddr*)&client, &client_len);
                if (http_conn::m_client_num == DEFAULT_MAX_CLIENTS) {
                    // call client to close
                    close(ret);
                }
                http_requests[ret].init(ret, client);
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLERR)) {
                http_requests[client_fd].close();
            }
            else if (events[i].events & EPOLLIN) {
                if (http_requests[client_fd].read())
                    pool->append_task(&http_requests[client_fd]);
                else
                    http_requests[client_fd].close();
            }
            else if (events[i].events & EPOLLOUT) {
                if (!http_requests[client_fd].write())
                    http_requests[client_fd].close();
            }
            else
                continue;
        }
    }

    http_requests.release();
    close(epoll_fd);
    close(socketfd);
    return 0;
}