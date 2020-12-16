cmake_minimum_required( VERSION 2.6 )
project(test_sock_util)

set (CMAKE_CXX_STANDARD 11)

add_definitions(-g -Wall -DLOG_TAG=\"test_sock_util\" -DHOST_BUILD=1)

add_executable( test_sock_server
	test_sock_server.cpp
	)
target_link_libraries( test_sock_server
	sock_util
	)

add_executable( test_sock_client
	test_sock_client.cpp
	)
target_link_libraries( test_sock_client
	sock_util
	)
