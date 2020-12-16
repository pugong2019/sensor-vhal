cmake_minimum_required( VERSION 2.6 )
project(sock_util)

set (CMAKE_CXX_STANDARD 11)

add_definitions(-g -Wall -DLOG_TAG=\"[SOCK_UTIL]\" -DHOST_BUILD=1)

include_directories(${CMAKE_CURRENT_LIST_DIR}/include)

add_library( sock_util STATIC
	sock_client.cpp
	sock_server.cpp
	)

add_subdirectory(test)
