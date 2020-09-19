project(opcua_pubsub)

set(CMAKE_C_FLAGS "-std=c99 -Wall -Werror -Wextra -Wno-unused-parameter -pedantic -g -O0 --coverage -pthread -D_GNU_SOURCE -z muldefs")

add_executable(opcua_subscriber_x86 opcua_subscriber.c opcua_pubsub.c)
add_executable(opcua_publisher_x86 opcua_publisher.c opcua_pubsub.c)
