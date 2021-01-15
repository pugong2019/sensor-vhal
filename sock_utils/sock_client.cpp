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

#include "sock_client.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#if DEBUG_SOCK_CLIENT
#define SOCK_CLIENT_LOG(a) sock_log a;
#else
#define SOCK_CLIENT_LOG(a)
#endif

sock_client_t *sock_client_init(int type, const char *server_path, int port) {
    sock_client_t *client = NULL;
    int socketfd;
    int socket_domain;
    int flag  = 1;
    pid_t pid = -1;
    char path[SOCK_MAX_PATH_LEN];
    int ret = 0;

    UNUSED(pid);
    UNUSED(path);
    SOCK_CLIENT_LOG(("sock_client_init(%s, %s, %d)  ...", type == SOCK_CONN_TYPE_INET_SOCK ? "INET" : "UNIX", server_path, port));

    if (SOCK_CONN_TYPE_INET_SOCK != type) {
        socket_domain = AF_UNIX;
    } else {
        socket_domain = AF_INET;
    }
    socketfd = socket(socket_domain, SOCK_STREAM, 0);
    if (socketfd < 0) {
        sock_log("sock error: create client socket failed!");
        return NULL;
    }

    if (type == SOCK_CONN_TYPE_INET_SOCK) {
        int on = 1;
        if ((setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) < 0) {
            sock_log("sock error: set socketopt(REUSEADDR) failed!\n");
            close(socketfd);
            _exit(-1);
        }

        ret = setsockopt(socketfd, IPPROTO_TCP, TCP_NODELAY, (const void *)&on, sizeof(int));
        if (ret < 0) {
            sock_log("sock error: set socketopt(TCP_NODELAY) failed!\n");
            close(socketfd);
            _exit(-1);
        }
    }

    if (SOCK_CONN_TYPE_INET_SOCK != type) {
        struct sockaddr_un serv_addr_un;
        memset(&serv_addr_un, 0, sizeof(struct sockaddr_un));
        serv_addr_un.sun_family = AF_UNIX;

        if (SOCK_CONN_TYPE_ABS_SOCK == type) {
            serv_addr_un.sun_path[0] = 0; /* Make abstract */
        } else {
            if (strlen(server_path) > (sizeof(serv_addr_un.sun_path) - 1)) {
                sock_log("sock error: server path config failed!");
                close(socketfd);
                return NULL;
            } else {
                // strcpy(serv_addr_un.sun_path, server_path);
                strncpy(serv_addr_un.sun_path, server_path, sizeof(serv_addr_un.sun_path));
            }
        }
        ret = connect(socketfd, (struct sockaddr *)&serv_addr_un, sizeof(struct sockaddr_un));
    } else {
        struct sockaddr_in serv_addr_in;
        memset(&serv_addr_in, 0, sizeof(struct sockaddr_in));
        serv_addr_in.sin_family = AF_INET;
        serv_addr_in.sin_port   = htons(port);
        // serv_addr.sin_addr.s_addr = inet_addr(server_ip);
        inet_pton(AF_INET, server_path, &serv_addr_in.sin_addr);
        ret = connect(socketfd, (struct sockaddr *)&serv_addr_in, sizeof(struct sockaddr_in));
    }
    if (ret < 0) {
        // sock_log("sock error: client connect to server failed! prot: %d", port);
        close(socketfd);
        return NULL;
    }

    // client = (sock_client_t*)malloc(sizeof(sock_client_t));
    client = new sock_client_t();
    if (NULL == client) {
        sock_log("sock error: create sock_client_t instance failed!");
        close(socketfd);
        // free(client);
        delete client;
        return NULL;
    }

    flag = 1;
    if (ioctl(socketfd, FIONBIO, &flag) < 0) {
        sock_log("sock error: set client socket to FIONBIO failed!");
        close(socketfd);
        // free(client);
        delete client;
        return NULL;
    }

    // memset(client, 0, sizeof(sock_client_t));
    client->socketfd = socketfd;
    if (strlen(server_path) > (sizeof(client->path) - 1)) {
        sock_log("sock error: set client path failed!");
        close(socketfd);
        // free(client);
        delete client;
        return NULL;
    } else {
        strncpy(client->path, server_path, sizeof(client->path));
    }

    SOCK_CLIENT_LOG(("sock_client_init(%s, %s, %d) returns %p", type == SOCK_CONN_TYPE_INET_SOCK ? "INET" : "UNIX", server_path, port, client));
    client->m_msg_buf.reserve(CLIENT_BUF_CAPACITY);
    return client;
}

void sock_client_close(sock_client_t *client) {
    SOCK_CLIENT_LOG(("sock_client_close() ..."));

    if (NULL != client) {
        close(client->socketfd);

        // free(client);
        delete client;
        client = NULL;
    }

    SOCK_CLIENT_LOG(("sock_client_close() completed."));
}

sock_conn_status_t sock_client_check_connect(sock_client_t *client, int timeout_ms) {
    sock_conn_status_t result = normal;
    int nsel                  = 0;
    fd_set rfds;
    struct timeval timeout;
    int nread = 0;

    if (!client) {
        return result;
    }

    FD_ZERO(&rfds);
    FD_SET(client->socketfd, &rfds);

    timeout.tv_sec  = 0;
    timeout.tv_usec = timeout_ms * 1000;
    nsel            = select(client->socketfd + 1, &rfds, NULL, NULL, &timeout);
    switch (nsel) {
        case -1:
            sock_log("sock error: select failed!");
            result = normal;
            break;

        case 0:
            result = normal;
            break;

        default:
            if (FD_ISSET(client->socketfd, &rfds)) {
                ioctl(client->socketfd, FIONREAD, &nread);
                if (nread != 0) {
                    result = readable;
                } else {
                    result = disconnect;
                }
            } else {
                result = normal;
            }
    }
    return result;
}

int sock_client_send(sock_client_t *client, const void *data, size_t datalen) {
    if (!client) return -1;
    int ret = send(client->socketfd, data, datalen, MSG_NOSIGNAL);
    return ret;
}

int sock_client_recv(sock_client_t *client, void *data, size_t datalen) {
    if (!client) return -1;
    int ret = recv(client->socketfd, data, datalen, MSG_DONTWAIT);
    return ret;
}

#if 0
int sock_client_send(sock_client_t *client, const void *data, size_t datalen, int timeout_ms) {
  SOCK_CLIENT_LOG(("sock_client_send(%08X,  %d bytes)", data, datalen));
  if (!client) {
    sock_log("invalid socket client");
    return -1;
  }
  unsigned char *p_src = (unsigned char *)data;
  size_t left_size = datalen;
  int retry_count = timeout_ms;
  int total_size = 0;

  while (left_size > 0) {
    int ret = send(client->socketfd, p_src, left_size, MSG_NOSIGNAL);
    if (ret > 0) {
      left_size -= ret;
      p_src += ret;
      total_size += ret;
    } else {
      if ((errno == EINTR) || (errno == EAGAIN)) {
        if ((retry_count--) < 0) {
          sock_log("sock_client_send() target send %d bytes, already send %d bytes, left %d bytes, errno = %d (%s)",
                   (int)datalen, (int)(datalen - left_size), (int)left_size, errno, strerror(errno));
          break;
        } else {
          usleep(1000);
          continue;
        }
      } else {
        sock_log("sock_client_send() target send %d bytes, already send %d bytes, left %d bytes, errno = %d (%s)",
                 (int)datalen, (int)(datalen - left_size), (int)left_size, errno, strerror(errno));
        break;
      }
    }
  }
  return (int)total_size;
}
#endif

#if 0
int sock_client_recv(sock_client_t *client, void *data, size_t datalen, int timeout_ms) {
  unsigned char *p_src = (unsigned char *)data;
  size_t left_size = datalen;
  int retry_count = timeout_ms;
  int total_size = 0;

  while (left_size > 0) {
    int ret = recv(client->socketfd, data, left_size, MSG_DONTWAIT);
    if (ret > 0) {
      left_size -= ret;
      p_src += ret;
      total_size += ret;
    } else {
      if ((errno == EINTR) || (errno == EAGAIN)) {
        if ((retry_count--) < 0) {
          sock_log("sock_server_recv() target recv %d bytes, already recv %d bytes, left %d bytes, errno = %d (%s)",
                   (int)datalen, (int)(datalen - left_size), (int)left_size, errno, strerror(errno));
          break;
        } else {
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

int sock_client_send_fd(sock_client_t *client, int *pfd, size_t fdlen) {
    int ret   = 0;
    int count = 0;
    int i     = 0;
    struct msghdr msg;
    struct cmsghdr *p_cmsg;
    struct iovec vec;
    char cmsgbuf[CMSG_SPACE(fdlen * sizeof(int))];
    int *p_fds         = NULL;
    int sdata[4]       = {0x88};
    msg.msg_control    = cmsgbuf;
    msg.msg_controllen = sizeof(cmsgbuf);
    p_cmsg             = CMSG_FIRSTHDR(&msg);
    if (p_cmsg == NULL) {
        sock_log("%s : %d : no msg hdr", __func__, __LINE__);
        ret = -1;
    } else {
        p_cmsg->cmsg_level = SOL_SOCKET;
        p_cmsg->cmsg_type  = SCM_RIGHTS;
        p_cmsg->cmsg_len   = CMSG_LEN(fdlen * sizeof(int));
        p_fds              = (int *)CMSG_DATA(p_cmsg);
    }

    SOCK_CLIENT_LOG(("sock_client_send_fd(pfd=%p, %d)", pfd, fdlen));

    for (i = 0; i < (int)fdlen; i++) {
        SOCK_CLIENT_LOG(("pfd[%d]=%d", i, pfd[i]));
        if (i > 8) {
            break;
        }
    }

    if (!client) {
        ret = -1;
        return ret;
    }

    if (p_fds) {
        for (i = 0; i < (int)fdlen; i++) {
            p_fds[i] = pfd[i];
        }
    }

    msg.msg_name    = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov     = &vec;
    msg.msg_iovlen  = 1;
    msg.msg_flags   = 0;

    vec.iov_base = sdata;
    vec.iov_len  = 16;
    do {
        count = sendmsg(client->socketfd, &msg, 0);
        if (count < 0) {
#if DEBUG_SOCK_CLIENT
            sock_log("%s : %d : sendmsg failed, count = %d, error = %d (%s)", __func__, __LINE__, count, errno, strerror(errno));
#endif
        }
    } while ((count < 0) && ((errno == EINTR) || (errno == EAGAIN)));

    if (count < 0) {
        sock_log("%s : %d : sendmsg failed, count = %d, error = %d (%s)", __func__, __LINE__, count, errno, strerror(errno));
        ret = -1;
    }

    return ret;
}

int sock_client_recv_fd(sock_client_t *client, int *pfd, size_t fdlen) {
    int ret   = 0;
    int count = 0;
    int i     = 0;
    struct msghdr msg;
    int rdata[4] = {0};
    struct iovec vec;
    char cmsgbuf[CMSG_SPACE(fdlen * sizeof(int))];
    struct cmsghdr *p_cmsg;
    int *p_fds;
    vec.iov_base       = rdata;
    vec.iov_len        = 16;
    msg.msg_name       = NULL;
    msg.msg_namelen    = 0;
    msg.msg_iov        = &vec;
    msg.msg_iovlen     = 1;
    msg.msg_control    = cmsgbuf;
    msg.msg_controllen = sizeof(cmsgbuf);
    msg.msg_flags      = 0;

    SOCK_CLIENT_LOG(("sock_client_recv_fd(pfd=%p, %d)", pfd, fdlen));

    if (!client) {
        ret = -1;
        return ret;
    }

    p_fds = (int *)CMSG_DATA(CMSG_FIRSTHDR(&msg));
    if (p_fds) {
        *p_fds = -1;
    }
    do {
        count = recvmsg(client->socketfd, &msg, MSG_WAITALL);
        if (count < 0) {
#if DEBUG_SOCK_CLIENT
            sock_log("%s : %d : recvmsg failed, count = %d, error = %d (%s)", __func__, __LINE__, count, errno, strerror(errno));
#endif
        }
    } while ((count < 0) && ((errno == EINTR) || (errno == EAGAIN)));

    if (count < 0) {
        sock_log("%s : %d : recvmsg failed, count = %d, error = %d (%s)", __func__, __LINE__, count, errno, strerror(errno));
        ret = -1;
    } else {
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

    for (i = 0; i < (int)fdlen; i++) {
        SOCK_CLIENT_LOG(("pfd[%d]=%d", i, pfd[i]));
        if (i > 8) {
            break;
        }
    }

    return ret;
}
