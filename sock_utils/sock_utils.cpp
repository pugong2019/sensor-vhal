/*
** Copyright 2018 Intel Corporation
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

#include <sys/cdefs.h>
#include <sys/types.h>
#include <stdint.h>
#include <pthread.h>

#include <stdio.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/netlink.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <string.h>
#include <poll.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/time.h>
#include <errno.h>
#include "sock_utils.h"

using namespace std;

SockServer::SockServer(int port, int sockType) {
    m_port = port;
    m_sock_type = sockType;
}

SockServer::~SockServer() {
    stop();
    join();
    sock_server_close(m_server);
    m_server = nullptr;

    for(int i = 0; i < MAX_CLIENTS; i++) {
        delete m_clients[i];
        m_clients[i] = nullptr;
    }
    m_thread.reset();

    m_pclient_ = nullptr;
    m_thread = nullptr;
}

int SockServer::start() {
    m_be_working = true;
    ALOGI("create new server: %s", m_sock_type == SOCK_CONN_TYPE_INET_SOCK ? "INET":"UNIX");
    m_server = sock_server_init(m_sock_type, m_port);
    if (m_server == nullptr) return -1;

    for(int i = 0; i < MAX_CLIENTS; i++) {
        delete m_clients[i];
        m_clients[i] = nullptr;
    }
    m_thread.reset(new std::thread(task, this));
    return 0;
}

void SockServer::stop() {
    m_be_working = false;
}

void SockServer::join() {
    if (m_thread && m_thread->joinable()) {
        m_thread->join();
    }
}

void SockServer::listener() {
    ALOGI("server listener thread start!");
    if(!m_server) {
        ALOGI("server == nullptr");
    }
    if(!m_be_working) {
        ALOGI("m_be_working == false");
    }
    while(m_be_working && (m_server != nullptr)) {
        check_new_connection();
        check_new_message();
    }
    ALOGI("server listener thread quit!");
}

sock_client_proxy_t* SockServer::get_sock_client(){
    return m_pclient_;
}

int SockServer::check_new_connection() {
    if(sock_server_has_newconn(m_server, m_connection_timeout_ms)) {
        auto client = sock_server_create_client(m_server);
        if(client != nullptr) {
            {
                std::lock_guard<std::mutex> lock(m_pclient_mutex_);
                if(m_pclient_ == nullptr) { //choose first as default
                    m_pclient_ = client;
                }
            }

            m_clients[client->id] = client;
            m_ncount++;
            ALOGI("create new client[%d], now count of clients is %d", client->id, m_ncount);

            if(m_connected_callback){
                m_connected_callback(this, client);
            }
        }
    }
    return m_ncount;
}

/**
 * @brief check if there is new message from client
 *
 * @return 0 for failure, (instance + 1) for disconnect instance, 0 for success
 */
int32_t SockServer::check_new_message() {

    if (sock_server_clients_readable(m_server, m_client_status_check_timeout) != SOCK_TRUE)
        return -1;
    for (int i = 0; i<MAX_CLIENTS; i++) {
        if (m_clients[i] == nullptr) continue;
        switch (sock_server_check_connect(m_server, m_clients[i])) {
            case readable:
                if(m_listener_callback) {
                    m_listener_callback(this, m_clients[i]);
                }
                break;

            case disconnect:
                if(m_disconnected_callback){
                    m_disconnected_callback(this, m_clients[i]);
                }
                {
                    std::lock_guard<std::mutex> lock(m_pclient_mutex_);
                    sock_server_close_client(m_server, m_clients[i]);
                    if(m_pclient_ == m_clients[i]) m_pclient_ = nullptr;
                }
                ALOGI("client %d disconnected, close it", m_clients[i]->id);
                m_clients[i] = nullptr;
                m_ncount--;
                if (m_ncount == 0) return i + 1;
                break;

            default:
                break;
        }
    }

    //chose a new default client
    {
        std::lock_guard<std::mutex> lock(m_pclient_mutex_);
        if(m_pclient_==nullptr){
            for (int i = 0; i<MAX_CLIENTS; i++) {
                if (m_clients[i] == nullptr) continue;
                m_pclient_ = m_clients[i];
                break;
            }
        }
    }
    return 0;
}

/**
 * @brief send data

 * @param client: serrver choose which client to send
 * @param data: message data
 * @param len: message len
 * @param mode: include three mode: set SOCK_TIMEOUT_MODE as default mode
 * @param timeout_ms: on when set mode as SOCK_TIMEOUT_MODE, this param is needed, default is 5ms
 * @return the data size that already send , and -1 if invalid socket and other socket error
 */
int SockServer::send_data(const sock_client_proxy_t* client,  const void* data, int len, sock_work_mode_t mode, int timeout_ms) {
    // AutoMutex auto_mutex(m_client_sync_mutex);    //ensure socket client is synced before sending message
    if (m_server == nullptr || client == nullptr) {
        ALOGE("invalid socket server or client has not connetcted to server");
        return -1;
    }

    unsigned char *p_src = (unsigned char *)data;
    int left_size = len;
    int retry_count = timeout_ms;
    int total_size = 0;
    switch (mode)
    {
    case SOCK_NONBLOCK_MODE:
        total_size = sock_server_send(m_server, client, p_src, left_size);
        break;

    case SOCK_TIMEOUT_MODE:
        while(left_size > 0){
            int ret = sock_server_send(m_server, client, p_src, left_size);
            if (ret > 0) {
                left_size -= ret;
                p_src += ret;
                total_size += ret;
            } else {
                if ((errno == EINTR) || (errno == EAGAIN)) {
                    if((retry_count--) < 0) {
                        break;
                    }else {
                        usleep(1000);
                        continue;
                    }
                } else {
                    ALOGE("socket error, errno[%d]:%s", errno, strerror(errno));
                    break;
                }
            }
        }
        break;

    case SOCK_BLOCK_MODE:
        while(left_size > 0){
            int ret = sock_server_send(m_server, client, p_src, left_size);
            if (ret > 0) {
                left_size -= ret;
                p_src += ret;
                total_size += ret;
            } else {
                if ((errno == EINTR) || (errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                        usleep(1000);
                        continue;
                } else {
                        ALOGE("socket error, errno[%d]:%s", errno, strerror(errno));
                        break;
                }
            }
        }
        break;

    default:
        ALOGE("not supported socket workmode");
        break;
    }
    return total_size;
}

/**
 * @brief same as send_data, just choose the default client to send data
 *
 * @param data
 * @param len
 * @param mode
 * @param timeout_ms
 * @return int
 */
int SockServer::send_data_default(const void* data, int len, sock_work_mode_t mode, int timeout_ms) {
    return send_data(m_pclient_, data, len, mode, timeout_ms);
}

sock_conn_status_t SockServer::check_connection_default() {
    std::lock_guard<std::mutex> lock(m_pclient_mutex_);
    if(!m_pclient_) return disconnect;
    return normal;
}
/**
 * @brief recv data from client
 *
 * @param client
 * @param data
 * @param len
 * @param mode : include three mode: SOCK_NONBLOCK_MODE as default mode
 * @param timeout_ms
 * @return received data size
 */
int SockServer::recv_data(const sock_client_proxy_t* client, void* data, int len, sock_work_mode_t mode, int timeout_ms) {
    if (m_server == nullptr || client == nullptr) return -1;
    unsigned char *p_src = (unsigned char *)data;
    int left_size = len;
    int retry_count = timeout_ms;
    int total_size = 0;

    switch (mode){
    case SOCK_BLOCK_MODE:
        while (left_size > 0) {
            int ret = sock_server_recv(m_server, client, p_src, left_size);
            if (ret > 0) {
                left_size -= ret;
                p_src += ret;
                total_size += ret;
            } else {
                if ((errno == EINTR) || (errno == EAGAIN) || (errno)== EWOULDBLOCK) {
                    usleep(1000);
                    continue;
                }else {
                    ALOGE("socket error: errno[%d]:%s, client fd: %d", errno, strerror(errno), m_server->client_slots[client->id]);
                    break;
                }
            }
        }
        break;

    case SOCK_NONBLOCK_MODE:
        total_size = sock_server_recv(m_server, client, p_src, left_size);
        break;

    case SOCK_TIMEOUT_MODE:
        while (left_size > 0) {
            int ret = sock_server_recv(m_server, client, p_src, left_size);
            if (ret > 0) {
                left_size -= ret;
                p_src += ret;
                total_size += ret;
            } else {
                if ((errno == EINTR) || (errno == EAGAIN)) {
                    if ((retry_count--) < 0) {
                        break;
                    } else {
                        usleep(1000);
                        continue;
                    }
                } else {
                    ALOGE("socket error: errno[%d]:%s", errno, strerror(errno));
                    break;
                }
            }
        }
        break;

    default:
        ALOGE("not supported socket workmode");
        break;
    }
    return total_size;
}

void SockServer::register_connected_callback(connected_callback_t func){
    m_connected_callback = func;
}

void SockServer::register_listener_callback(listener_callback_t callback) {
    m_listener_callback = callback;
}

void SockServer::register_disconnected_callback(disconnect_callback_t callback){
    m_disconnected_callback = callback;
}

/*
---------------------------------------------------------------------------
*/

SockClient::SockClient(char* ip, int port, float timeout) {
    m_port = port;
    m_ip = new char[strlen(ip) + 1];
    strcpy(m_ip, ip);

    if (timeout >= 0) m_timeout = timeout;
    m_conn_type = SOCK_CONN_TYPE_INET_SOCK;
}

SockClient::SockClient(const char *server_path, float timeout) {
    m_ip = new char[strlen(server_path) + 1];
    strcpy(m_ip, server_path);

    if (timeout >= 0) m_timeout = timeout;

    m_conn_type = SOCK_CONN_TYPE_UNIX_SOCK;
}

SockClient::~SockClient(){
    stop();
    join();
    sock_client_close(m_client);
    m_client = nullptr;

    if (m_ip) {
        delete [] m_ip;
        m_ip = nullptr;
    }
    m_thread.reset();
    m_thread = nullptr;
}

int SockClient::start(){
    m_be_working = true;
    m_thread.reset(new std::thread(task, this));
    return 0;
}

void SockClient::stop(){
    m_be_working = false;
}

void SockClient::join(){
    if (m_thread && m_thread->joinable()) {
        m_thread->join();
    }
}

int SockClient::connect_to_server(){
    while (!m_client){
        m_client = sock_client_init(m_conn_type, m_ip, m_port);
        if(!m_client){
            ALOGW("falied to connect to server [%s:%d], [errno: %d] %s", m_ip, m_port, errno, strerror(errno));
            usleep(m_timeout * 1000 * 1000);
            ALOGI("reconnect to server, timeout = %.3fs\n", m_timeout);
        }

        if(!m_be_working) {
            return 0;
        }
    }
    ALOGI("connected to [%s:%d] successful", m_ip, m_port);
    if(m_connected_callback){
        m_connected_callback(this);
    }
    return 0;
}

/**
 * @brief send data

 * @param data: message data
 * @param len: message len
 * @param mode: include three mode: set SOCK_TIMEOUT_MODE as default mode
 * @param timeout_ms: on when set mode as SOCK_TIMEOUT_MODE, this param is needed, default is 5ms
 * @return the data size that already send , and -1 if invalid socket and other socket error
 */
int SockClient::send_data(const void* data, int len, sock_work_mode_t mode, int timeout_ms) {
    if (!m_client) {
        ALOGE("invalid socket client");
        return -1;
    }
    unsigned char *p_src = (unsigned char *)data;
    int left_size = len;
    int retry_count = timeout_ms;
    int total_size = 0;
    switch (mode)
    {
    case SOCK_NONBLOCK_MODE:
        total_size = sock_client_send(m_client, p_src, left_size);
        break;

    case SOCK_TIMEOUT_MODE:
        while(left_size > 0){
            int ret = sock_client_send(m_client, p_src, left_size);
            if (ret > 0) {
                left_size -= ret;
                p_src += ret;
                total_size += ret;
            } else {
                if ((errno == EINTR) || (errno == EAGAIN)) {
                    if((retry_count--) < 0) {
                        ALOGE("sock client send timeout");
                        break;
                    }else {
                        usleep(1000);
                        continue;
                    }
                } else {
                    ALOGE("socket client error, errno[%d]:%s", errno, strerror(errno));
                    break;
                }
            }
        }
        break;

    case SOCK_BLOCK_MODE:
        while(left_size > 0){
            int ret = sock_client_send(m_client, p_src, left_size);
            if (ret > 0) {
                left_size -= ret;
                p_src += ret;
                total_size += ret;
            } else {
                if ((errno == EINTR) || (errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                        usleep(1000);
                        continue;
                } else {
                        ALOGE("socket client error, errno[%d]:%s", errno, strerror(errno));
                        break;
                }
            }
        }
        break;

    default:
        ALOGE("not supported socket workmode");
        break;
    }
    return total_size;
}

/**
 * @brief recv data from client
 *
 * @param data
 * @param len
 * @param mode : include three mode: SOCK_TIMEOUT_MODE as default mode
 * @param timeout_ms
 * @return received data size
 */
int SockClient::recv_data(void* data, int len, sock_work_mode_t mode, int timeout_ms) {
    if (!m_client) {
        ALOGE("invalid socket client");
        return -1;
    }

    unsigned char *p_src = (unsigned char *)data;
    int left_size = len;
    int retry_count = timeout_ms;
    int total_size = 0;

    switch (mode){
    case SOCK_BLOCK_MODE:
        while (left_size > 0) {
            int ret = sock_client_recv(m_client, p_src, left_size);
            if (ret > 0) {
                left_size -= ret;
                p_src += ret;
                total_size += ret;
            } else {
                if ((errno == EINTR) || (errno == EAGAIN) || (errno)== EWOULDBLOCK) {
                    usleep(1000);
                    continue;
                }else {
                    ALOGE("socket error: errno[%d]:%s", errno, strerror(errno));
                    break;
                }
            }
        }
        break;

    case SOCK_NONBLOCK_MODE:
        total_size = sock_client_recv(m_client, data, left_size);
        break;

    case SOCK_TIMEOUT_MODE:
        while (left_size > 0) {
            int ret = sock_client_recv(m_client, p_src, left_size);
            if (ret > 0) {
                left_size -= ret;
                p_src += ret;
                total_size += ret;
            } else {
                if ((errno == EINTR) || (errno == EAGAIN)) {
                    if ((retry_count--) < 0) {
                        break;
                    } else {
                        usleep(1000);
                        continue;
                    }
                } else {
                    ALOGE("socket error: errno[%d]:%s", errno, strerror(errno));
                    break;
                }
            }
        }
        break;
    default:
        break;
    }
    return total_size;
}

void SockClient::register_listener_callback(listener_callback_t callback) {
    m_listener_callback = callback;
}
void SockClient::register_connected_callback(connected_callback_t callback) {
    m_connected_callback = callback;
}

void SockClient::register_disconnected_callback(disconnected_callback_t callback){
    m_disconnected_callback = callback;
}

void SockClient::listener() {
    ALOGI("client listener thread start!");
    connect_to_server();

    while(m_be_working) {
        check_new_message();
    }
    ALOGI("client listener thread quit!");
}

int32_t SockClient::check_new_message(){
    switch (sock_client_check_connect(m_client, m_msg_check_timeout_ms)) {
        case readable:
            if (m_listener_callback){
                m_listener_callback(this);
            }
            break;

        case disconnect:
            ALOGI("server disconnected [%s : %d]\n", m_ip, m_port);

            if(m_disconnected_callback){
                m_disconnected_callback(this);
            }

            // TODO: fix bug, m_client is not NULL
            sock_client_close(m_client);
            m_client = NULL;
            connect_to_server();
            break;

        default:
            break;
    }
    return 0;
}
