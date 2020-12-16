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
#include <signal.h>
#include <unistd.h>

#include "sock_server.h"

#define MAX_CLIENTS	 8
#define MAX_MESSAGE	 128


static void blockSigpipe()
{
#if 0
	sigset_t mask;


	printf("blockSigpipe\n");

	sigemptyset(&mask);
	sigaddset(&mask, SIGPIPE);
	if (sigprocmask(SIG_BLOCK, &mask, NULL) != 0)
		ALOGE("WARNING: SIGPIPE not blocked\n");
#endif
}

int main()
{
	bool quit=false;
	sock_server_t* server= NULL;
	sock_client_proxy_t* clients[MAX_CLIENTS];
	sock_client_proxy_t* p_client = NULL;
	int ncount=0;
	int	i=0;

	char cmd[6];
	char message[MAX_MESSAGE];
	char response[MAX_MESSAGE];

	const char* cmd_data = "cmd_d";
	const char* cmd_fd = "cmd_f";

	blockSigpipe();
	/*
	 *	test communicate with clients
	 */

#if USE_INET_SOCKET
	// server = sock_server_init(SOCK_CONN_TYPE_ABS_SOCK);
	server = sock_server_init(SOCK_CONN_TYPE_INET_SOCK, SOCK_UTIL_DEFAULT_PORT);
#else
	server = sock_server_init(SOCK_CONN_TYPE_UNIX_SOCK, 0);
#endif

	for(i=0; i<MAX_CLIENTS; i++)
	{
		clients[i] = NULL;
	}

	while(quit != true)
	{


		if(sock_server_has_newconn(server, 0))
		{
			printf("%s: %d\n", __func__, __LINE__);
#if 1
			p_client=sock_server_create_client(server);
			if(NULL != p_client)
			{
				clients[p_client->id] = p_client;
				ncount++;
				printf("create new client, now count of clients is %d\n", ncount);
				// sock_server_send(p_client, welcome, strlen(welcome)+1);
			}
#endif
		}


#if 1
		if(sock_server_clients_readable(server, 500))
		{
			printf("%s: %d\n", __func__, __LINE__);
			for(i = 0; i < MAX_CLIENTS; i++)
			{
				printf("%s: %d\n", __func__, __LINE__);
				if(NULL != clients[i])
				{
					printf("%s: %d\n", __func__, __LINE__);
					switch(sock_server_check_connect(server, clients[i]))
					{
						case readable:
							memset(cmd, 0, sizeof(cmd));
							sock_server_recv(server, clients[i], cmd, sizeof(cmd)-1);

							printf("cmd from client %d : %s\n", clients[i]->id, cmd);

							if(strcmp(cmd, cmd_data) == 0) {
								sock_server_recv(server, clients[i], message, MAX_MESSAGE);
								printf("message from client %d : %s\n", clients[i]->id, message);

								printf("write response msg to client.\n");
								sprintf(response, "hello client %d, server has received your message.\n", clients[i]->id);
								printf("%s: %d", __func__, __LINE__);
								
								sock_server_send(server, clients[i], response, strlen(response)+1);
								printf("%s: %d", __func__, __LINE__);
							
								if(strcmp(message, "quit server.")==0)
								{
									quit = true;
								}
								printf("%s: %d\n", __func__, __LINE__);
							}
							else if (strcmp(cmd, cmd_fd) == 0) {
								int fd = -1;
								sock_server_recv_fd(server, clients[i], &fd, 1);
								printf("receive fd = %d \n", fd);
								if(fd > 0){
									int fd2 = dup(fd);
									printf("dup fd %d to fd %d \n", fd, fd2);
									sprintf(response, "hello client %d, server has received your message.\n", clients[i]->id);
									write(fd, response, strlen(response)+1);
									if(fd2 != -1) close(fd2);
								}
							}
							else {
								printf("unknwon cmd \n");
							}
							break;

						case disconnect:
							printf("client %d disconnected, close it\n", clients[i]->id);
							sock_server_close_client(server, clients[i]);
							clients[i]=NULL;
							ncount--;
							break;

						default:
							break;
					}

				}

			}

		}

#endif
	}


	for(i=0; i<MAX_CLIENTS; i++)
	{
		if(NULL!=clients[i])
		{
			sock_server_close_client(server, clients[i]);
			clients[i]=NULL;
		}
	}

	sock_server_close(server);

	return 0;
}
