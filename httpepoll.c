#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <errno.h>
#include <fcntl.h>
#include <arpa/inet.h>

#define SERVER "Server: HttpEpoll\r\n"

#define MAX_EVENTS 8192

void accept_conn(int, int);

void recv_from(int);

int init_listenfd(u_short *, int);

void exec_cgi(int, const char *, const char *, const char *);

void bad_request(int);

void send_file(int, FILE *);

void cannot_execute(int);

void err_quit(const char *);

ssize_t readline(int, char *, int);

void send_header(int);

void not_found(int);

void serve_file(int, const char *);

void unimplemented(int);


static ssize_t read_cnt;
static char *read_ptr;
static char read_buf[8192];

// 带用户空间buffer的read方法，每次读一字节
static ssize_t buff_read(int fd, char *ptr) {
    if (read_cnt <= 0) {
        while ((read_cnt = read(fd, read_buf, sizeof(read_buf))) < 0) {
            if (errno != EINTR)
                return -1;
        }
        if (read_cnt == 0)
            return 0;
        read_ptr = read_buf;
    }

    read_cnt--;
    *ptr = *read_ptr++;
    return 1;
}

// 查看套接字中下一个字符，但不读取
static ssize_t peek_msg(int fd, char *ptr) {
    if (read_cnt <= 0) {
        while ((read_cnt = recv(fd, read_buf, sizeof(read_buf), 0)) < 0) {
            if (errno != EINTR)
                return -1;
        }
        if (read_cnt == 0)
            return 0;
        read_ptr = read_buf;
    }

    *ptr = *read_ptr;
    return 1;
}

void accept_conn(int listenfd, int epfd) {
    struct sockaddr_in cliaddr;
    ssize_t clilen = sizeof(cliaddr);

    int connfd = accept(listenfd, (struct sockaddr *) &cliaddr, &clilen);
    if (connfd == -1) {
        perror("accept connection error");
        return;
    }

    // 设置connfd为非阻塞
    int flag = fcntl(connfd, F_GETFL);
    flag |= O_NONBLOCK;
    fcntl(connfd, F_SETFL, flag);

    // 打印客户端信息
    char ip[64] = {0};
    printf("New Client IP: %s, Port: %d, cfd = %d\n",
           inet_ntop(AF_INET, &cliaddr.sin_addr.s_addr, ip, sizeof(ip)),
           ntohs(cliaddr.sin_port), connfd);

    // 设置为边缘触发
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = connfd;

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &ev) == -1)
        perror("epoll_ctl add fd error");
}

void recv_from(int connfd) {
    char buf[1024];
    int numchars;
    char method[255];
    char url[255];
    char path[512];
    size_t i, j;
    struct stat st;
    int cgi = 0;

    char *query_string = NULL;

    numchars = readline(connfd, buf, sizeof(buf));

    // 读取请求方法
    i = 0, j = 0;
    while (!isspace((int) buf[j]) && (i < sizeof(method) - 1))
        method[i++] = buf[j++];
    method[i] = '\0';

    // 仅实现了GET和POST方法，其余方法不支持
    if (strcasecmp(method, "GET") && strcasecmp(method, "POST")) {
        unimplemented(connfd);
        close(connfd);
        return;
    }

    // 若为POST则开启CGI
    if (strcasecmp(method, "POST") == 0)
        cgi = 1;

    // 读取url
    i = 0;
    while (isspace((int) buf[j]) && (j < sizeof(buf)))  // 丢弃多余空格
        j++;
    while (!isspace((int) buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf)))
        url[i++] = buf[j++];
    url[i] = '\0';

    // 方法为GET时读取url参数
    if (strcasecmp(method, "GET") == 0) {
        query_string = url;
        while ((*query_string != '?') && (*query_string != '\0'))
            query_string++;
        if (*query_string == '?') {
            cgi = 1;  // 有参数开启CGI支持
            *query_string = '\0';  // 截断url
            query_string++;
        }
    }

    sprintf(path, "htdocs%s", url);
    if (path[strlen(path) - 1] == '/')
        strcat(path, "index.html");  // url为路径，则默认访问index.html
    if (stat(path, &st) == -1) {  // 获取文件状态信息
        // 找不到相应文件，读取并丢弃后续报文
        while ((numchars > 0) && strcmp("\n", buf))
            numchars = readline(connfd, buf, sizeof(buf));
        not_found(connfd);
    } else {
        if ((st.st_mode & S_IFMT) == S_IFDIR)  // 是路径
            strcat(path, "/index.html");
        if ((st.st_mode & S_IXUSR) ||  // 具有用户可执行权限
            (st.st_mode & S_IXGRP) ||  // 具有用户组可执行权限
            (st.st_mode & S_IXOTH))  // 其他用户具可执行权限
            cgi = 1;
        if (!cgi) {
            serve_file(connfd, path);  // 非CGI请求，直接返回相应文件
        } else {
            exec_cgi(connfd, path, method, query_string);  // 否则运行CGI程序
        }

    }
    close(connfd);  // 关闭连接，该连接自动从epoll兴趣列表移除
}

// 启动监听套接字，并添加到epoll兴趣列表
int init_listenfd(u_short *port, int epfd) {
    int httpd;
    struct sockaddr_in srvaddr;

    httpd = socket(PF_INET, SOCK_STREAM, 0);
    if (httpd == -1)
        err_quit("socket failed");

    memset(&srvaddr, 0, sizeof(srvaddr));
    srvaddr.sin_family = AF_INET;
    srvaddr.sin_port = htons(*port);
    srvaddr.sin_addr.s_addr = htonl(INADDR_ANY);

    // 端口复用
    int flag = 1;
    setsockopt(httpd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));


    if (bind(httpd, (struct sockaddr *) &srvaddr, sizeof(srvaddr)) < 0)
        err_quit("bind error");

    if (*port == 0) {
        socklen_t namelen = sizeof(srvaddr);
        if (getsockname(httpd, (struct sockaddr *) &srvaddr, &namelen) == -1)
            err_quit("getsockname error");
        *port = ntohs(srvaddr.sin_port);
    }

    if (listen(httpd, 1024) < 0)
        err_quit("listen error");

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = httpd;

    // 将listenfd加入到epoll兴趣列表
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, httpd, &ev) < 0) {
        err_quit("epoll_ctl add fd error");
    }

    return (httpd);
}

// 执行CGI脚本
void exec_cgi(int connfd, const char *path,
              const char *method, const char *query_string) {
    char buf[1024] = {0};
    int cgi_output[2];
    int cgi_input[2];
    pid_t pid;
    int status;
    int i;
    char c;
    int numchars = 1;
    int content_length = -1;

    if (strcasecmp(method, "GET") == 0)
        while ((numchars > 0) && strcmp("\n", buf))  /* read & discard send_header */
            numchars = readline(connfd, buf, sizeof(buf));
    else {
        // POST
        numchars = readline(connfd, buf, sizeof(buf));
        while ((numchars > 0) && strcmp("\n", buf)) {
            buf[15] = '\0';  // 截取Content-Length
            if (strcasecmp(buf, "Content-Length:") == 0)
                content_length = atoi(&(buf[16]));
            numchars = readline(connfd, buf, sizeof(buf));
        }
        if (content_length == -1) {
            bad_request(connfd);
            return;
        }
    }
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(connfd, buf, strlen(buf), 0);

    // 创建输出和输入管道 数组中 0:读管道 1:写管道
    if (pipe(cgi_output) < 0) {
        cannot_execute(connfd);
        return;
    }
    if (pipe(cgi_input) < 0) {
        cannot_execute(connfd);
        return;
    }

    if ((pid = fork()) < 0) {
        cannot_execute(connfd);
        return;
    }
    if (pid == 0) {
        // 子进程运行CGI脚本
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        // 将输入输出管道重定向为stdin和stdout
        dup2(cgi_output[1], fileno(stdout));
        dup2(cgi_input[0], fileno(stdin));

        // 关闭写管道中的读通道 和 读管道中的写通道
        close(cgi_output[0]);
        close(cgi_input[1]);

        // 将HTTP请求方法加入环境变量
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env);

        if (strcasecmp(method, "GET") == 0) {
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env);
        } else {
            // POST
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }
        execl(path, path, NULL);
        exit(0);
    }
    // 父进程
    close(cgi_output[1]);
    close(cgi_input[0]);
    if (strcasecmp(method, "POST") == 0) {
        while (peek_msg(connfd, &c) > 0 && (c == '\n' || c == '\r')) {
            buff_read(connfd, &c);
        }
        // 读请求体内容，并写入CGI输入管道
        for (i = 0; i < content_length; i++) {
            buff_read(connfd, &c);
            write(cgi_input[1], &c, 1);
        }
    }
    // 读CGI输出管道并发送给客户端
    send_header(connfd);
    while (buff_read(cgi_output[0], &c) > 0)
        send(connfd, &c, 1, 0);
    close(cgi_output[0]);
    close(cgi_input[1]);
    waitpid(pid, &status, 0);
}

// 错误的HTTP请求
void bad_request(int client) {
    char buf[1024];

    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "<P>Your browser sent a bad request, ");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "such as a POST without a Content-Length.\r\n");
    send(client, buf, sizeof(buf), 0);
}

// 发送文件
void send_file(int connfd, FILE *resource) {
    char buf[1024];

    fgets(buf, sizeof(buf), resource);
    while (!feof(resource)) {
        send(connfd, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource);
    }
}

// CGI脚本无法执行
void cannot_execute(int client) {
    char buf[1024];

    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
    send(client, buf, strlen(buf), 0);
}

// 打印错误信息并退出
void err_quit(const char *sc) {
    perror(sc);
    exit(1);
}

// 读取一行
ssize_t readline(int sock, char *buf, int size) {
    int i = 0;
    char c = '\0';
    int n;

    while ((i < size - 1) && (c != '\n')) {
        n = buff_read(sock, &c);
        if (n > 0) {
            if (c == '\r') {
                n = (int) peek_msg(sock, &c);
                if ((n > 0) && (c == '\n')) {
                    buff_read(sock, &c);
                } else
                    c = '\n';
            }
            buf[i] = c;
            i++;
        } else
            c = '\n';
    }
    buf[i] = '\0';
    return (i);
}

// 返回成功的头信息 200
void send_header(int client) {
    char buf[1024];

    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, SERVER);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}

// 返回404
void not_found(int connfd) {
    char buf[1024];

    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(connfd, buf, strlen(buf), 0);
    sprintf(buf, SERVER);
    send(connfd, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(connfd, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(connfd, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(connfd, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(connfd, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(connfd, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(connfd, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(connfd, buf, strlen(buf), 0);
}

// 发送文件
void serve_file(int connfd, const char *filename) {
    FILE *resource = NULL;
    int n = 1;
    char buf[1024] = {0};

    while ((n > 0) && strcmp("\n", buf)) {
        // 读取并丢弃头信息
        n = readline(connfd, buf, sizeof(buf));
    }

    resource = fopen(filename, "r");
    if (resource == NULL)
        not_found(connfd);
    else {
        send_header(connfd);  // 发送头信息
        send_file(connfd, resource);  // 发送文件信息
    }
    fclose(resource);
}

// 未支持的方法
void unimplemented(int connfd) {
    char buf[1024];
    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(connfd, buf, strlen(buf), 0);
    sprintf(buf, SERVER);
    send(connfd, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(connfd, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(connfd, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(connfd, buf, strlen(buf), 0);
    sprintf(buf, "</TITLE></HEAD>\r\n");
    send(connfd, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
    send(connfd, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(connfd, buf, strlen(buf), 0);
}

int main(int argc, char **argv) {
    int listenfd, epfd, nready;
    u_short port = 0;
    struct epoll_event evlist[MAX_EVENTS];

    epfd = epoll_create(MAX_EVENTS);

    if (argc == 2) {
        port = (u_short) atoi(argv[1]);
    }

    listenfd = init_listenfd(&port, epfd);

    printf("Server listening on port %d\n", port);

    for (;;) {
        nready = epoll_wait(epfd, evlist, MAX_EVENTS, -1);
        if (nready == -1) {
            perror("epoll_wait error");
            continue;
        }

        for (int i = 0; i < nready; i++) {
            // 只处理读事件，其他事件默认不处理
            if (!(evlist[i].events & EPOLLIN))
                continue;
            if (evlist[i].data.fd == listenfd) {
                accept_conn(listenfd, epfd);
            } else {
                recv_from(evlist[i].data.fd);
            }
        }
    }

    close(listenfd);

    return (0);
}