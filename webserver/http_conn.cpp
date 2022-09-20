#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <cstring>
#include <assert.h>
#include <iostream>
#include <cstdarg>
#include <sys/epoll.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>

#include "http_conn.hpp"


int http_conn::m_epollfd = -1;
int http_conn::m_client_num = 0;

/* print ip */
void http_conn::print_param() {
    char ip[20];
    inet_ntop(AF_INET, &m_client_info.sin_addr, ip, sizeof(m_client_info));
    int port = ntohs(m_client_info.sin_port);
    printf("receive from ip:%s port:%d\n", ip, port);
    printf("%s", m_request_line);
}

/* add event */
void addevent(int epollfd, int fd, bool oneshot) {
    set_nonblck(fd);
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if (oneshot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
}

/* remove event */
void remevent(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
}

/* modify event */
void modevent(int epollfd, int fd, int event_state, int oneshot) {
    epoll_event event;
    event.data.fd = fd;
    event.events = event_state | EPOLLET | EPOLLRDHUP;
    if (oneshot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

/* set fd nonblocking */
void set_nonblck(int fd) {
    int old_type = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, old_type | O_NONBLOCK);
}

/* add signal handler */
void addsig(int sig, void(*handler)(int), bool restart) {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handler;
    if (restart)
        action.sa_flags |= SA_RESTART;
    sigfillset(&action.sa_mask);
    assert(sigaction(sig, &action, nullptr) != -1);
}

/* init client */
void http_conn::init(int accept_fd, const sockaddr_in& client_info) {
    m_client_fd = accept_fd;
    memcpy(&m_client_info, &client_info, sizeof(client_info));
    addevent(m_epollfd, accept_fd);
    ++m_client_num;

    print_param();

    init();
}

void http_conn::init() {
    m_write_index = m_read_index = m_start_index = m_check_index = m_content_length = 0;
    m_method = GET;
    m_http_url = m_http_version = m_host = nullptr;
    m_keep_alive = false;
    m_state = CHECK_LINE;

    m_resourse_address = nullptr;
    m_iv_count = 0;
    memset(m_sourse_path, 0, MAX_RESOURSE_LENGTH);
    memset(&m_sourse_stat, 0, sizeof(m_sourse_stat));
    memset(m_iovec, 0, sizeof(m_iovec));
    memset(m_request_line, 0, MAX_REQUEST_LINE);
    memset(m_response_line, 0, MAX_RESPONSE_LINE);
}

/* close client */
void http_conn::close() {
    remevent(m_epollfd, m_client_fd);
    --m_client_num;
}

/* read operation */
bool http_conn::read() {
    // read
    printf("read\n");
    if (m_read_index > MAX_REQUEST_LINE)
        return false;
    int buf_size;
    while (true) {
        // be careful of read content
        if ((buf_size = recv(m_client_fd, m_request_line + m_read_index, 
                            MAX_REQUEST_LINE - m_read_index, 0)) < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("error while recv");
                return false;
            }
            return true;
        }
        else if (buf_size == 0) {
            printf("client lose\n");
            return false;
        }
        else {
            print_param();
            m_read_index += buf_size;
        }
    }
    return true;
}

/* write operation */
bool http_conn::write() {
    // write
    printf("write\n");
    int write_byte = 0, byte_have_send = 0, total_byte = m_write_index;
    if (m_write_index == 0) {
        printf("error request tyep");
        modevent(m_epollfd, m_client_fd, EPOLLIN);
        init();
        return true;
    }
    printf("%s\n", m_response_line);
    
    while (1) {
        if ((write_byte = writev(m_client_fd, m_iovec, m_iv_count)) < 0) {
            if (errno == EAGAIN) {
                printf("wait for space\n");
                modevent(m_epollfd, m_client_fd, EPOLLOUT);
                return true;
            }
            unmap_address();
            return false;
        }
        total_byte -= write_byte;
        byte_have_send += write_byte;
        printf("write %d\n", write_byte);
        if (total_byte <= byte_have_send) {
            unmap_address();
            // keep alive
            if (m_keep_alive) {
                printf("keep alive\n");
                init();
                modevent(m_epollfd, m_client_fd, EPOLLIN);
                return true;
            }
            else {
                printf("not keep alive\n");
                modevent(m_epollfd, m_client_fd, EPOLLIN);
                return false;
            }
        }
    }
    
    return true;
}

/* main process */
void http_conn::process() {
    // parse the requests and responce
    printf("process\n");
    HTTP_CODE ret = parse_enter();
    if (ret == NO_REQUEST) {
        printf("wait for more\n");
        modevent(m_epollfd, m_client_fd, EPOLLIN);
        return;
    }
    if (!process_write(ret)) {
        printf("error response\n");
        close();
        return;
    }
    modevent(m_epollfd, m_client_fd, EPOLLOUT);
}

/* parse request and split into line, slave state machine */
http_conn::LINE_RESULT http_conn::parse_line() {
    m_start_index = m_check_index;
    for (; m_check_index < m_read_index; ++m_check_index) {
        char parse_char = m_request_line[m_check_index];
        if (parse_char == '\r') {
            if (m_check_index + 1 == m_read_index)
                return LINE_OPEN;
            if (m_request_line[m_check_index + 1] == '\n') {
                m_request_line[m_check_index++] = '\0';
                m_request_line[m_check_index++] = '\0';
                return LINE_OK;
            }
            return LINE_ERROR;
        }
        else if (parse_char == '\n') {
            if (m_request_line[m_check_index - 1] == '\r') {
                m_request_line[m_check_index - 1] = '\0';
                m_request_line[m_check_index++] = '\0';
                return LINE_OK;
            }
            return LINE_ERROR;
        }
    }
    return LINE_OPEN;
}

/* parse request line */
http_conn::HTTP_CODE http_conn::parse_request_line(char* text) {
    // split the method
    char* url = strpbrk(text, " \t");
    if (!url)
        return BAD_REQUEST;
    *url++ = '\0';
    // this version surport only get method
    if (strcmp(text, "GET") == 0) {
        m_method = GET;
    }
    else {
        return BAD_REQUEST;
    }

    // split the url
    url += strspn(url, " \t");
    if (!url)
        return BAD_REQUEST;
    m_http_url = url;
    url = strpbrk(url, " \t");
    if (!url)
        return BAD_REQUEST;
    *url++ = '\0';

    //split version
    url += strspn(url, " \t");
    if (!url)
        return BAD_REQUEST;
    m_http_version = url;

    // url = strpbrk(url, " \t\r");
    // if (!url)
    //     return BAD_REQUEST;
    // *url = '\0';
    
    // surport in old version of http, now abloish
    /*
    // this version surport only http/1.1
    if (strcmp(m_http_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;

    // parse request url
    if (strncmp(m_http_url, "http://", 7) != 0)
        return BAD_REQUEST;
    m_http_url += 7;
    */
    m_http_url = strchr(m_http_url, '/');
    if (!m_http_url || *m_http_url != '/')
        return BAD_REQUEST;
    m_state = CHECK_HEADER;
    return NO_REQUEST;
}

/* parse request header */
http_conn::HTTP_CODE http_conn::parse_request_header(char* text) {
    // end of request
    if (text[0] == '\0') {
        if (!m_content_length) {
            m_state = CHECK_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    // parse connection section
    else if (strncmp(text, "Connection:", 11) == 0) {
        text += 11;
        text += strspn(text, " \t");
        if (strcmp(text, "keep-alive") == 0)
            m_keep_alive = true;
    }
    else if (strncmp(text, "Proxy-Connection:", 17) == 0) {
        text += 17;
        text += strspn(text, " \t");
        if (strcmp(text, "keep-alive") == 0)
            m_keep_alive = true;
    }
    // parse content-length section
    else if (strncmp(text, "Content-Length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atoi(text);
    }
    // parse host
    else if (strncmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else {
        printf("unkonwn header %s\n, update later\n", text);
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_content(char* text) {
    if (m_read_index >= m_content_length + m_check_index) {
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

/* main state machine */
http_conn::HTTP_CODE http_conn::parse_enter() {
    LINE_RESULT line_state = LINE_OK;
    HTTP_CODE res_code = NO_REQUEST;
    char* text = nullptr;
    while ((m_state == CHECK_CONTENT && line_state == LINE_OK) 
            || ((line_state = parse_line()) == LINE_OK)) {
        text = get_cur_line();
        switch (m_state) {
            case CHECK_LINE: {
                res_code = parse_request_line(text);
                CHECK_RETURN(res_code, BAD_REQUEST);
                m_state = CHECK_HEADER;
                break;
            }
            case CHECK_HEADER: {
                res_code = parse_request_header(text);
                CHECK_RETURN(res_code, BAD_REQUEST);
                if(res_code ==  GET_REQUEST)
                    return do_process();
                break;
            }
            case CHECK_CONTENT: {
                // parse content
                res_code = parse_content(text);
                CHECK_RETURN(res_code, BAD_REQUEST);
                if (res_code == GET_REQUEST)
                    return do_process();
                line_state = LINE_OPEN;
                break;
            }
            default: {
                printf("unkown state\n");
                return INTERNAL_ERROR;
            }
        }
    }
}

/* get resourse */
http_conn::HTTP_CODE http_conn::do_process() {
    strcpy(m_sourse_path, RESOURSE_PATH);
    int len = strlen(RESOURSE_PATH);
    strncpy(m_sourse_path + len, m_http_url, strlen(m_http_url));
    if (stat(m_sourse_path, &m_sourse_stat) < 0) {
        return NO_RESOURSE;
    }
    if (!(m_sourse_stat.st_mode & S_IROTH)) {
        return FORBIDDEN_REQUEST;
    }
    if (S_ISDIR(m_sourse_stat.st_mode)) {
        return BAD_REQUEST;
    }
    int fd = open(m_sourse_path, O_RDONLY);
    m_resourse_address = map_address(nullptr, m_sourse_stat.st_size, 
                                    PROT_READ, MAP_PRIVATE, fd, 0);
    if (!m_resourse_address)
        unmap_address();
    ::close(fd);
    return FILE_REQUEST;
}

/* mmap encapsole */
char* http_conn::map_address(void *addr, size_t len, int prot,
		   int flags, int fd, __off_t offset) {
               return (char*)mmap(addr, len, prot, 
                        flags, fd, offset);
           }

/* unmap encapsole */
void http_conn::unmap_address() {
    if (m_resourse_address) {
        munmap(m_resourse_address, m_sourse_stat.st_size);
        m_resourse_address = nullptr;
    }
}

/* process write */
bool http_conn::process_write(HTTP_CODE ret) {
    switch (ret) {
        case BAD_REQUEST: {
            add_line(HTTP_BAD_REQUEST, HTTP_BAD_REQUEST_FORM[0]);
            add_header(strlen(HTTP_BAD_REQUEST_FORM[1]));
            if (!add_content(HTTP_BAD_REQUEST_FORM[1]))
                return false;
            break;
        }
        case FORBIDDEN_REQUEST: {
            add_line(HTTP_FORBIDDEN, HTTP_FORBIDDEN_FORM[0]);
            add_header(strlen(HTTP_FORBIDDEN_FORM[1]));
            if (!add_content(HTTP_FORBIDDEN_FORM[1]))
                return false;
            break;
        }
        case NO_RESOURSE: {
            add_line(HTTP_NOT_FOUND, HTTP_NOT_FOUND_FORM[0]);
            add_header(strlen(HTTP_NOT_FOUND_FORM[1]));
            if (!add_content(HTTP_NOT_FOUND_FORM[1]))
                return false;
            break;
        }
        case INTERNAL_ERROR: {
            add_line(HTTP_INTERNAL_ERROR, HTTP_INTERNAL_ERROR_FORM[0]);
            add_header(strlen(HTTP_INTERNAL_ERROR_FORM[1]));
            if (!add_content(HTTP_INTERNAL_ERROR_FORM[1]))
                return false;
            break;
        }
        // get request
        case FILE_REQUEST: {
            add_line(HTTP_OK, HTTP_OK_FORM);
            if (m_sourse_stat.st_size) {
                add_header(m_sourse_stat.st_size);
                m_iovec[0].iov_base = m_response_line;
                m_iovec[0].iov_len  = m_write_index;
                m_iovec[1].iov_base = m_resourse_address;
                m_iovec[1].iov_len  = m_sourse_stat.st_size;
                m_iv_count = 2;
                return true;
            }
            else {
                const char* ok_return = "<html><body></body><html>";
                add_header(strlen(ok_return));
                if (!add_content(ok_return))
                    return false;
            }
            break;
        }
        default: {
            printf("unkown request type\n");
            return false;
        }
    }
    m_iovec[0].iov_base = m_response_line;
    m_iovec[0].iov_len = m_write_index;
    m_iv_count = 1;
    return true;
}

/* add response */
bool http_conn::add_response(const char* format, ...) {
    if (m_write_index >= MAX_RESPONSE_LINE)
        return false;
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_response_line + m_write_index, MAX_RESPONSE_LINE - 1 - m_write_index,
                        format, arg_list);
    if (len >= MAX_RESPONSE_LINE - 1 - m_write_index)
        return false;
    va_end(arg_list);
    m_write_index += len;
    return true;
}

/* add response line */
bool http_conn::add_line(int status, const char* text) {
    add_response("%s %d %s \r\n", "HTTP/1.1", status, text);
}

/* add response header */
bool http_conn::add_header(int content_length) {
    add_content_length(content_length);
    add_content_type();
    add_keep_alive();
    add_blank();
}

/* add Connection */
bool http_conn::add_keep_alive() {
    return add_response("Connection: %s\r\n", (m_keep_alive ? "keep-alive" : "close"));
}

/* add blank */
bool http_conn::add_blank() {
    return add_response("%s", "\r\n");
}

/* add content */
bool http_conn::add_content(const char* text) {
    return add_response("%s", text);
}

/* add content tyep */
bool http_conn::add_content_type() {
    return add_response("Content-tpye: text/html\r\n");
}

/* add content length */
bool http_conn::add_content_length(int length) {
    return add_response("Content-Length: %d\r\n", length);
}