## 一个C语言轻量级HTTP服务器



## 简介

一个轻量级的HTTP服务器，支持GET、POST方法，提供静态资源处理和CGI支持。


## 开发部署环境

+ 操作系统: Ubuntu 20.04

+ 编译器: gcc 9.4.0

+ 版本控制: git

+ 自动化构建: make

+ 集成开发工具: CLion



## Usage

```
make 

./httpepoll [port] [thread number]

```

## 核心功能及技术

+ 解析HTTP请求，目前支持 HTTP GET、POST方法，支持CGI脚本

+ 使用epoll + 非阻塞IO + 边缘触发(ET) 实现高效的请求处理

+ 实现线程池、阻塞等待队列提高并发度，并降低频繁创建线程的开销

+ 使用pipe、dup2将标准输入输出重定向到管道，利用execl在子进程执行脚本，从而实现CGI

+ 实现基于用户空间缓冲区的read与peek（窥看消息而不读取）方法，从而实现高效的readline方法
