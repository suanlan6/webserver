#ifndef HTTP_CONN_H_
#define HTTP_CONN_H_
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#define CHECK_RETURN(res, code) \
            do {\
            if ((res) == (code)) \
                return code;\
            } while (0)

/* status codes */
static const int HTTP_OK             = 200;
static const int HTTP_BAD_REQUEST    = 400;
static const int HTTP_FORBIDDEN      = 403;
static const int HTTP_NOT_FOUND      = 404;
static const int HTTP_INTERNAL_ERROR = 500;

/* status info */
static const char* HTTP_OK_FORM                   = "OK";
static const char* HTTP_BAD_REQUEST_FORM[2]       = {"Bad Request", "Your request have syntax\
 or is inherintely possible to satisfy.\n"};
static const char* HTTP_FORBIDDEN_FORM[2]         = {"Request FORBIDDEN", "Your are forbidden\
 to request this resourse.\n"};
static const char* HTTP_NOT_FOUND_FORM[2]         = {"Not Found", "Your request resourses are\
 not found.\n"};
static const char* HTTP_INTERNAL_ERROR_FORM[2]    = {"Internal Error", "Server internal error\
 , please contact us.\n"};

/* default file resourse */
static const char* RESOURSE_PATH = "/websourse";

class http_conn {
public:
    // MAX REQUEST LENGTH
    static const int MAX_REQUEST_LINE    = 2048;
    static const int MAX_RESPONSE_LINE   = 4096;
    static const int MAX_RESOURSE_LENGTH = 512;

    // http methods
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH};

    // check status
    enum CHECK_STATE {CHECK_LINE = 0, CHECK_HEADER, CHECK_CONTENT};

    // http code
    enum HTTP_CODE {NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURSE, 
                    FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSE_CONNECTION};

    // parse line
    enum LINE_RESULT {LINE_OK = 0, LINE_ERROR, LINE_OPEN};

    // the clients num linked to the server
    static int m_client_num;

    // the epoll fd
    static int m_epollfd;

    http_conn() = default;
    ~http_conn() = default;


    /* request fun */
    // parse request line
    HTTP_CODE parse_request_line(char*);
    // parse request header
    HTTP_CODE parse_request_header(char*);
    // parse line
    LINE_RESULT parse_line();
    // parse enter
    HTTP_CODE parse_enter();
    // get current line
    char* get_cur_line() { return m_request_line + m_start_index; }
    // parse content
    HTTP_CODE parse_content(char*);
    // find resourse
    HTTP_CODE do_process();


    /* response fun */
    bool add_line(int, const char*);
    bool add_header(int);
    bool add_keep_alive();
    bool add_blank();
    bool add_content(const char*);
    bool add_content_length(int);
    bool add_content_type();
    bool add_response(const char*, ...);
    bool process_write(HTTP_CODE);


    // init a client
    void init(int accept_fd, const sockaddr_in& client_info);
    void init();
    
    // close connect
    void close();

    // process task
    void process();

    // read request
    bool read();

    // return response
    bool write();

    // test fun
    void print_param();
private:
    /* resourse members */
    // resourse path
    char m_sourse_path[MAX_RESOURSE_LENGTH];
    // file stat
    struct stat m_sourse_stat;
    // resourse start position in memory
    char* m_resourse_address;
    // memory writev object
    struct iovec m_iovec[2];
    // memory block nums
    int m_iv_count;
    // mmap encapsole
    char* map_address(void *, size_t, int, int, int, __off_t);
    // unmap encapsole
    void unmap_address();

    /* request members */
    // read index
    int m_read_index;
    // check index
    int m_check_index;
    // start index
    int m_start_index;
    // content length
    int m_content_length;
    // method
    METHOD m_method;
    // url
    char* m_http_url;
    // version
    char* m_http_version;
    // host
    char* m_host;
    // keep_alive
    bool m_keep_alive;
    // state
    CHECK_STATE m_state;
    // request line
    char m_request_line[MAX_REQUEST_LINE];


    /* response members */
    // write index
    int m_write_index;
    // response line
    char m_response_line[MAX_RESPONSE_LINE];


    // client_fd
    int m_client_fd;

    // client information
    sockaddr_in m_client_info;
};
void addevent(int epollfd, int fd, bool oneshot = true);
void remevent(int epollfd, int fd);
void modevent(int epollfd, int fd, int event_state, int oneshot = true);
void set_nonblck(int fd);
void addsig(int sig, void(*handler)(int), bool restart = true);
#endif