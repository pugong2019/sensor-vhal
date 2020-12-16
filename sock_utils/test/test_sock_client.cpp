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
#include <fcntl.h>

#include "sock_client.h"

#define MAX_MESSAGE	 128

int main(int argc, char** argv)
{
	bool quit=false;
	char answer;
	const char* cmd_data = "cmd_d";
	const char* cmd_fd = "cmd_f";
	const char* hello_msg="Hello, I am a client.";
	const char* quit_msg="quit server.";
	sock_client_t* client = NULL;
	char buffer[MAX_MESSAGE];
	int fd = -1;


#if USE_INET_SOCKET
	client = sock_client_init(SOCK_CONN_TYPE_INET_SOCK, "127.0.0.1", SOCK_UTIL_DEFAULT_PORT);
#else
	client = sock_client_init(SOCK_CONN_TYPE_UNIX_SOCK, SOCK_UTIL_DEFAULT_PATH, 0);
#endif
	if(client == nullptr){
		printf("socket client create failed! \n");
		return -1;
	}
	/*
	 *	communicate with server
	 */
	quit=false;
	while(quit!=true)
	{
		printf("Choose action :\n");
		printf("'d' -- send data msg.\n");
		printf("'f' -- send fd msg.\n");
		printf("'c' -- close server.\n");
		printf("'q' -- quit now.\n");
		printf("other -- none.\n");

		scanf("%c", &answer);
		if('d'== answer)
		{
			sock_client_send(client, cmd_data, strlen(cmd_data));
			sock_client_send(client, hello_msg, strlen(hello_msg)+1);
		}
		if('f' == answer)
		{
			fd = open("/data/local/tmp/sock_test.txt", O_RDWR|O_CREAT, 0777);
			sock_client_send(client, cmd_fd, strlen(cmd_fd));
			if(fd < 0){
				printf("open file:%s failed. \n", "/data/local/tmp/sock_test.txt");
			}else {
				printf("local fd = %d \n", fd);
				sock_client_send_fd(client, &fd, 1);
				close(fd);
				fd = -1;
			}
		}
		else if('c' == answer)
		{
			sock_client_send(client, cmd_data, strlen(cmd_data));
			sock_client_send(client, quit_msg, strlen(quit_msg)+1);
		}
		else if('q' == answer)
		{
			quit=true;
		}
		else
		{

		}

		switch(sock_client_check_connect(client, 10))
		{
			case readable:
				sock_client_recv(client, buffer, MAX_MESSAGE);
				printf("message from server : %s\n", buffer);
				break;

			case disconnect:
				printf("server disconnected.\n");
				quit=true;
				break;

			default:
				break;
		}

	}

	if(fd != -1) {
		close(fd);
	}

	sock_client_close(client);

	return 0;
}
