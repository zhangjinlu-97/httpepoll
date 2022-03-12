## 一个Linux轻量级HTTP服务器

![](https://img.shields.io/github/license/zhangjinlu-97/httpepoll)
![](https://img.shields.io/badge/language-C-lightgrey)

## 简介

使用C语言实现的Linux下的轻量级HTTP服务器，支持GET、POST方法，提供静态资源处理和CGI支持。


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

## 关键技术点

+ 解析HTTP请求，目前支持 HTTP GET、POST方法，支持CGI脚本

+ 使用epoll + 非阻塞IO + 边缘触发(ET) 实现高效的请求处理

+ 实现线程池、阻塞等待队列提高并发度，并降低频繁创建线程的开销

+ 利用pipe和dup2 IO重定向实现CGI
