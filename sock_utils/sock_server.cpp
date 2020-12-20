/*
** Copyright 2016 Intel Corporation
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include "sock_server.h"

#if DEBUG_SOCK_SERVER
#define SOCK_SERVER_LOG(a)	sock_log a;
#else
#define SOCK_SERVER_LOG(a)
#endif

int sock_server_find_empty_slot(sock_server_t* server) {
    int id = 0;
    for(id = 0; id < SOCK_MAX_CLIENTS; ++id) {
        if(-1 == server->client_slots[id]) break;
    }
    return id;
}

sock_server_t* sock_server_init(int type, int port) {
    int socket_domain = 0;
    sock_server_t* server = NULL;
    int ret = 0;

    sock_log("sock_server_init(%s, %d) ...", type == SOCK_CONN_TYPE_INET_SOCK?"INET":"UNIX", port);

    if(SOCK_CONN_TYPE_INET_SOCK != type) {
        socket_domain = AF_UNIX;
    } else {
        socket_domain = AF_INET;
    }

    int socketfd = socket(socket_domain, SOCK_STREAM, 0);
    if(socketfd < 0) {
        sock_log("sock error: create server socket failed!");
        return NULL;
    }

    int on = 1;
    ret = setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR,(const void *)&on , sizeof(int));
    if (ret < 0) {
        close(socketfd);
        perror("setsockopt");
        _exit(-1);
    }
    ret = setsockopt(socketfd, IPPROTO_TCP, TCP_NODELAY,(const void *)&on , sizeof(int));
    if (ret < 0) {
        close(socketfd);
        perror("setsockopt");
        _exit(-1);
    }


#if 1
    int flag = 1;
    ret = ioctl(socketfd, FIONBIO, &flag);
    if(ret < 0) {
        sock_log("sock error: set server socket to FIONBIO failed!");
        close(socketfd);
        return NULL;
    }
#endif

    if(SOCK_CONN_TYPE_INET_SOCK != type) {
        struct sockaddr_un serv_addr;
        memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sun_family = AF_UNIX;

        if(SOCK_CONN_TYPE_ABS_SOCK == type) {
            serv_addr.sun_path[0] = 0; /* Make abstract */
        } else {
            unlink(SOCK_UTIL_DEFAULT_PATH);
            strncpy(serv_addr.sun_path, SOCK_UTIL_DEFAULT_PATH, sizeof(serv_addr.sun_path) - 1);
        }
        sock_log("%s: %d : bind to %s", __func__, __LINE__, serv_addr.sun_path);
        ret = bind(socketfd, (struct sockaddr*)&serv_addr, sizeof(struct sockaddr_un));
    } else {
        struct sockaddr_in  serv_addr;
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        serv_addr.sin_port = htons(port);
        ret = bind(socketfd, (struct sockaddr*)&serv_addr, sizeof(struct sockaddr_in));
    }

    if(ret < 0) {
        sock_log("sock error : bind server socket failed! error = %d(%s) \n", errno, strerror(errno));
        close(socketfd);
        return NULL;
    }

    if (SOCK_CONN_TYPE_UNIX_SOCK == type) {
        int unix_fd = open(SOCK_UTIL_DEFAULT_PATH, O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR|S_IROTH|S_IWOTH);
        if(unix_fd < 0){
            sock_log("open file:%s failed!", SOCK_UTIL_DEFAULT_PATH);
        }
        ret = fchmod(unix_fd, S_IRUSR|S_IWUSR|S_IROTH); //limit permissions
        if(ret < 0) {
            sock_log("sock error: chmod %s to 064 failed!", SOCK_UTIL_DEFAULT_PATH);
        }
        close(unix_fd);
    }

    if(listen(socketfd, SOCK_MAX_CLIENTS) < 0) {
        sock_log("sock error: listen server socket failed!");
        close(socketfd);
        return NULL;
    }

    // server = (sock_server_t*)malloc(sizeof(sock_server_t));
    server = new sock_server_t();
    if(NULL==server) {
        sock_log("sock error: create sock_server_t instance failed!");
        close(socketfd);
        return NULL;
    }
    memset(server, 0, sizeof(sock_server_t));
    server->type = type;
    server->socketfd = socketfd;

    if(SOCK_CONN_TYPE_INET_SOCK != type) {
        strncpy(server->path, SOCK_UTIL_DEFAULT_PATH, sizeof(server->path) - 1);
    }

    for(int i = 0; i < SOCK_MAX_CLIENTS; ++i) {
        server->client_slots[i] = -1;
    }

    sock_log("sock_server_init(%s, %d) returns %p \n", type == SOCK_CONN_TYPE_INET_SOCK?"INET":"UNIX", port, server);
    return server;
}

void sock_server_close (sock_server_t* server) {
    sock_log("sock_server_close() ...");

    if(NULL != server) {
        close(server->socketfd);

        for(int id = 0; id < SOCK_MAX_CLIENTS; ++id) {
            if(-1 != server->client_slots[id]) {
                close(server->client_slots[id]);
                server->client_slots[id] = -1;
            }
        }

        if(SOCK_CONN_TYPE_UNIX_SOCK == server->type) {
            unlink(server->path);
        }

        // free(server);
        delete server;
        server = NULL;
    }

    sock_log("sock_server_close() successful.");
}

int sock_server_has_newconn(sock_server_t* server, int timeout_ms) {
    bool result = SOCK_FALSE;
    fd_set rfds, wfds, efds;
    struct timeval timeout;

    if (!server) {
        return SOCK_FALSE;
    }

#if DEBUG_SOCK_SERVER
    // sock_log("sock_server_has_newconn() ...");
#endif

    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_ZERO(&efds);
    FD_SET(server->socketfd, &rfds);
    timeout.tv_sec = 0;
    timeout.tv_usec = timeout_ms * 1000;

    int nsel = select(server->socketfd+1, &rfds, NULL, NULL, &timeout);
    switch (nsel) {
        case -1:
            sock_log("sock error: select failed!");
            result = SOCK_FALSE;
            break;

        case 0:
            result = SOCK_FALSE;
            break;

        default:
            if(FD_ISSET(server->socketfd, &rfds)) {
                if(sock_server_find_empty_slot(server) < SOCK_MAX_CLIENTS) {
                    sock_log("sock server has new connection.");
                    result = SOCK_TRUE;
                } else {
                    int clientfd = 0;

                    if(SOCK_CONN_TYPE_INET_SOCK != server->type) {
                        struct sockaddr_un client_addr;
                        socklen_t addr_len = sizeof(struct sockaddr_un);
                        clientfd = accept(server->socketfd, (struct sockaddr*)&client_addr, &addr_len);
                    } else {
                        struct sockaddr_in client_addr;
                        socklen_t addr_len = sizeof(struct sockaddr_in);
                        clientfd = accept(server->socketfd, (struct sockaddr*)&client_addr, &addr_len);
                    }
                    close(clientfd);

                    sock_log("sock server has new connection, but client_slots is full!");
                    result = SOCK_FALSE;
                }
            }
            break;
    }

#if DEBUG_SOCK_SERVER
    // sock_log("sock_server_has_newconn() result = %d, nsel = %d", result, nsel);
#endif
    return result;
}

sock_client_proxy_t* sock_server_create_client (sock_server_t* server) {
    int clientfd = 0;
    sock_client_proxy_t* p_client = NULL;
    int ret = 0;

    SOCK_SERVER_LOG(("sock_server_create_client() ..."));

    if (!server) {
        return NULL;
    }

    if(SOCK_CONN_TYPE_INET_SOCK != server->type) {
        struct sockaddr_un client_addr_un;
        socklen_t addr_len = sizeof(struct sockaddr_un);
        clientfd = accept(server->socketfd, (struct sockaddr*)&client_addr_un, &addr_len);
    } else {
        struct sockaddr_in client_addr_in;
        socklen_t addr_len = sizeof(struct sockaddr_in);
        clientfd = accept(server->socketfd, (struct sockaddr*)&client_addr_in, &addr_len);
    }

    if(clientfd < 0) {
        sock_log("sock error: accept socketfd failed!");
        return NULL;
    }

#if 1
    int flag = 1;
    ret = ioctl(clientfd, FIONBIO, &flag);
    if(ret < 0) {
        sock_log("sock error: set client socket to FIONBIO failed!");
        close(clientfd);
        return NULL;
    }
#endif

    int id = sock_server_find_empty_slot(server);
    if(id >= SOCK_MAX_CLIENTS) {
        sock_log("sock error: the client_slots is full!");
        close(clientfd);
        return NULL;
    }

    // p_client = (sock_client_proxy_t*)malloc(sizeof(sock_client_proxy_t));
    p_client = new sock_client_proxy_t();
    if(NULL == p_client) {
        sock_log("sock error: malloc sock_client_proxy_t failed!");
        close(clientfd);
        return NULL;
    }

    server->client_slots[id] = clientfd;
    p_client->id = id;
    p_client->m_msg_buf.reserve(CLIENT_BUF_CAPACITY);

    SOCK_SERVER_LOG(("sock_server_create_client() successful, client id is %d", id));
    return p_client;
}

void sock_server_close_client(sock_server_t* server, sock_client_proxy_t* p_client) {
    SOCK_SERVER_LOG(("sock_server_close_client() ..."));
    if (!server) {
        return;
    }

    if (!p_client) {
        return;
    }

    if(-1 != server->client_slots[p_client->id]) {
        close(server->client_slots[p_client->id]);
        server->client_slots[p_client->id] = -1;
    }

    // free(p_client);
    delete p_client;

    SOCK_SERVER_LOG(("sock_server_close_client() completed."));
}

int sock_server_clients_readable(sock_server_t* server, int timeout_ms) {
    int result = SOCK_FALSE;
    int maxfd = 0;
    if (!server) {
        return result;
    }

    FD_ZERO(&(server->rfds));
    for(int i = 0; i < SOCK_MAX_CLIENTS; ++i) {
        if(server->client_slots[i] != -1) {
            FD_SET(server->client_slots[i], &(server->rfds));
            maxfd = (maxfd > server->client_slots[i]) ? maxfd : server->client_slots[i];
        }
    }

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = timeout_ms * 1000;

    int nsel = select(maxfd+1, &(server->rfds), NULL, NULL, &timeout);
    switch (nsel) {
        case -1:
            sock_log("sock error: check clients readable select failed!");
            result = SOCK_FALSE;
            break;

        case 0:
            result = SOCK_FALSE;
            break;

        default:
            result = SOCK_TRUE;
            break;
    }
    return result;
}

int sock_server_clients_writeable(sock_server_t* server, const sock_client_proxy_t* client, int timeout_ms) {
    int result = SOCK_FALSE;
    if (!server || !client) {
        return result;
    }

    FD_ZERO(&(server->wfds));
    FD_SET(client->id, &(server->wfds));
    int maxfd = client->id;
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = timeout_ms * 1000;

    int nsel = select(maxfd+1, nullptr, &(server->wfds), nullptr, &timeout);
    switch (nsel) {
        case -1:
            sock_log("sock error: check clients writeable select failed!");
            result = SOCK_FALSE;
            break;

        case 0:
            result = SOCK_FALSE;
            break;

        default:
            result = SOCK_TRUE;
            break;
    }
    return result;
}

sock_conn_status_t sock_server_check_connect(sock_server_t* server, const sock_client_proxy_t* p_client) {
    sock_conn_status_t result = normal;

#if 0
    sock_log("sock_server_check_connect() : client %d", p_client->id);
#endif

    if (!server) {
        return result;
    }

    if(!p_client) {
        return result;
    }

    int clientfd = server->client_slots[p_client->id];
    if(FD_ISSET(clientfd, &server->rfds)) {
        int nread = 0;
        if(-1 != ioctl(clientfd, FIONREAD, &nread)){
            if(nread != 0) {
                result = readable;
            } else {
                result = disconnect;
            }
        } else {
            sock_log("server ioctl error: %d, %s!", errno, strerror(errno));
            result = normal;
        }
    } else {
        result = normal;
    }

#if 0
    switch(result) {
        case disconnect:
            sock_log("sock_server_check_connect() : client %d is disconnect", p_client->id);
            break;
        case readable:
            sock_log("sock_server_check_connect() : client %d is readable", p_client->id);
            break;
        default:
            sock_log("sock_server_check_connect() : client %d is normal", p_client->id);
    }
#endif

    return result;
}

int sock_server_send(sock_server_t* server, const sock_client_proxy_t* sender,  const void* data, size_t datalen) {
    if (server == nullptr || sender == nullptr) return -1;
    int ret = send(server->client_slots[sender->id], data, datalen, MSG_NOSIGNAL);
    return ret;
}

int sock_server_recv(sock_server_t* server, const sock_client_proxy_t* receiver, void* data, size_t datalen) {
    if (server == nullptr || receiver == nullptr) return -1;
    int ret = recv(server->client_slots[receiver->id], data, datalen, MSG_DONTWAIT);
    return ret;
}

#if 0
int sock_server_send(sock_server_t* server, const sock_client_proxy_t* sender,  const void* data, size_t datalen, int timeout_ms) {
    if (server == nullptr || sender == nullptr) {
        sock_log("invalid socket server or client");
        return -1;
    }

    unsigned char *p_src = (unsigned char *)data;
    size_t left_size = datalen;
    int retry_count = timeout_ms;
    int total_size = 0;

    while(left_size > 0){
        int ret = send(server->client_slots[sender->id], p_src, left_size, MSG_NOSIGNAL);
        if (ret > 0) {
            left_size -= ret;
            p_src += ret;
            total_size += ret;
        } else {
            if ((errno == EINTR) || (errno == EAGAIN)) {
                if((retry_count--) < 0) {
                    sock_log("sock_server_send() target send %d bytes, already send %d bytes, left %d bytes, errno = %d (%s)",
                            (int)datalen, (int)(datalen - left_size), (int)left_size, errno, strerror(errno));
                    break;
                }else {
                    usleep(1000);
                    continue;
                }
            } else {
                sock_log("sock_server_send() target send %d bytes, already send %d bytes, left %d bytes, errno = %d (%s)",
                        (int)datalen, (int)(datalen - left_size), (int)left_size, errno, strerror(errno));
                break;
            }
        }
    }
    return (int)total_size;
}
#endif

#if 0
int sock_server_recv(sock_server_t* server, const sock_client_proxy_t* receiver, void* data, size_t datalen, int timeout_ms) {
    if (server == nullptr || receiver == nullptr) return -1;

    unsigned char *p_src = (unsigned char *)data;
    size_t left_size = datalen;
    int retry_count = timeout_ms;
    int total_size = 0;

    while(left_size > 0){
        int ret = recv(server->client_slots[receiver->id], data, left_size, MSG_DONTWAIT);
        if (ret > 0) {
            left_size -= ret;
            p_src += ret;
            total_size += ret;
        } else {
            if ((errno == EINTR) || (errno == EAGAIN)) {
                if((retry_count--) < 0) {
                    sock_log("sock_server_recv() target recv %d bytes, already recv %d bytes, left %d bytes, errno = %d (%s)",
                            (int)datalen, (int)(datalen - left_size), (int)left_size, errno, strerror(errno));
                    break;
                }else {
                    usleep(1000);
                    continue;
                }
            } else {
                sock_log("sock_server_recv() target recv %d bytes, already recv %d bytes, left %d bytes, errno = %d (%s)",
                        (int)datalen, (int)(datalen - left_size), (int)left_size, errno, strerror(errno));
                break;
            }
        }
    }
    return (int)total_size;
}
#endif

int sock_server_send_fd(sock_server_t* server, const sock_client_proxy_t* sender, int* pfd, size_t fdlen) {
    int ret = 0;
    int i = 0;
    int count = 0;
    int *p_fds = NULL;
    int sdata[4] = {0x88};

    struct iovec vec;
    vec.iov_base = &sdata;
    vec.iov_len = 16;

    char cmsgbuf[CMSG_SPACE(fdlen * sizeof(int))];
    struct msghdr msg;
    msg.msg_control = cmsgbuf;
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = &vec;
    msg.msg_iovlen = 1;
    msg.msg_flags = 0;
    msg.msg_controllen = sizeof(cmsgbuf);

    struct cmsghdr* p_cmsg = CMSG_FIRSTHDR(&msg);
    if (p_cmsg == NULL) {
        sock_log("%s : %d : no msg hdr", __func__, __LINE__);
        ret = -1;
    } else {
        p_cmsg->cmsg_level = SOL_SOCKET;
        p_cmsg->cmsg_type = SCM_RIGHTS;
        p_cmsg->cmsg_len = CMSG_LEN(fdlen * sizeof(int));
        p_fds = (int *)CMSG_DATA(p_cmsg);
    }

    SOCK_SERVER_LOG(("sock_server_send_fd(cliend %d, pfd=%p, %d)", sender->id, pfd, (int)fdlen));

#if DEBUG_SOCK_SERVER
    for (i = 0; i < (int)fdlen; i++) {
        sock_log("pfd[%d]=%d\n", i, pfd[i]);
        if(i > 8) break;
    }
#endif

    if (!server) {
        ret = -1;
        return ret;
    }

    if (p_fds) {
        for (i = 0; i < (int)fdlen; i++) {
            p_fds[i] = pfd[i];
        }
    }

    count = sendmsg(server->client_slots[sender->id], &msg, MSG_NOSIGNAL);
    if (count < 0) {
        sock_log("%s : %d : sendmsg failed, count = %d", __func__, __LINE__, count);
        ret = -1;
    }

    return ret;
}

int sock_server_recv_fd(sock_server_t* server, const sock_client_proxy_t* receiver, int* pfd, size_t fdlen) {
    int ret = 0;
    int i = 0;
    int rdata[4] = {0x0};
    char cmsgbuf[CMSG_SPACE(fdlen*sizeof(int))];

    struct iovec vec;
    vec.iov_base = rdata;
    vec.iov_len = 16;

    struct msghdr msg;
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = &vec;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsgbuf;
    msg.msg_controllen = sizeof(cmsgbuf);
    msg.msg_flags = 0;

    SOCK_SERVER_LOG(("sock_server_recv_fd(cliend %d, pfd=%p, %d)", receiver->id, pfd, (int)fdlen));

    if (!server) {
        ret = -1;
        return ret;
    }

    int* p_fds = (int *)CMSG_DATA(CMSG_FIRSTHDR(&msg));
    if (p_fds) *p_fds = -1;

    int count = recvmsg(server->client_slots[receiver->id], &msg, MSG_WAITALL);
    if (count < 0) {
        sock_log("%s : %d : recvmsg failed, count = %d, error = %d (%s)", __func__, __LINE__, count, errno, strerror(errno));
        ret = -1;
    } else {
        struct cmsghdr *p_cmsg;
        p_cmsg = CMSG_FIRSTHDR(&msg);
        if (p_cmsg == NULL) {
            sock_log("%s : %d : no msg hdr", __func__, __LINE__);
            ret = -1;
        } else {
            p_fds = (int *)CMSG_DATA(p_cmsg);
            for (i = 0; i < (int)fdlen; i++) {
                pfd[i] = p_fds[i];
            }
        }
    }

#if DEBUG_SOCK_SERVER
    for (i = 0; i< (int)fdlen; i++) {
        sock_log("pfd[%d]=%d", i, pfd[i]);
        if(i > 8) break;
    }
#endif

    return ret;
}



const char* sock_server_get_addr(sock_server_t* server, const char* ifname) {
    const char* addr = "";
    if (server == nullptr) return addr;

    if (server->type != SOCK_CONN_TYPE_INET_SOCK) {
        addr = server->path;
    } else {
        struct ifreq ifr;
        memset(&ifr, 0, sizeof(ifr));
        memset(server->path, 0, SOCK_MAX_PATH_LEN);
        if (strlen(ifname) > (sizeof(ifr.ifr_name) - 1)) {
            sock_log("set ifr_name failed");
        } else {
            strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name) - 1);
        }
        int ret = ioctl(server->socketfd, SIOCGIFADDR, &ifr);
        if ( ret < 0) {
            sock_log("%s : %d : ioctl SIOCGIFADDR failed, ret = %d, error = %d(%s)", __func__, __LINE__, ret, errno, strerror(errno));
        } else {
            char* in_addr = inet_ntoa(((struct sockaddr_in*)&(ifr.ifr_addr))->sin_addr);
            if (strlen(in_addr) > (sizeof(server->path) - 1)) {
                sock_log("set server path failed");
            } else {
                strncpy(server->path, in_addr, sizeof(server->path) - 1);
            }
        }
        addr = server->path;
    }

    SOCK_SERVER_LOG(("%s : %d : addr is %s\n", __func__, __LINE__, addr));

    return addr;
}
