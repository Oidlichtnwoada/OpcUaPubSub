FROM debian:latest

# define compiler and linker flags
ARG COMPILER_FLAGS="-std=c99 -O3 -Wall -Werror -Wextra -Wno-unused-parameter -pedantic"
ARG LINKER_FLAGS="-pthread -z muldefs"
ARG OPEN62541_HASH="7d1d21a88ca1a89ae7bddc4e64c2c52330fdf6ff"

# update packages and install the required ones
RUN apt update
RUN apt install git build-essential cppcheck gcc pkg-config cmake python libc6-armel-cross libc6-dev-armel-cross binutils-arm-linux-gnueabi libncurses5-dev bison flex libssl-dev bc gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf -y

# create open62541 library source files
RUN git clone https://github.com/open62541/open62541.git
WORKDIR open62541
RUN git reset --hard $OPEN62541_HASH
RUN git submodule update --init --recursive
RUN mkdir build
WORKDIR build
RUN cmake -DUA_ENABLE_AMALGAMATION=ON -DUA_ENABLE_PUBSUB=ON -DUA_ENABLE_PUBSUB_ETH_UADP=ON -DUA_ENABLE_PUBSUB_CUSTOM_PUBLISH_HANDLING=ON -DUA_ENABLE_IMMUTABLE_NODES:BOOL=ON ..
RUN make

# check and compile opcua_pubsub.c
WORKDIR ..
ADD opcua_pubsub.h .
ADD opcua_pubsub.c .
RUN cppcheck opcua_pubsub.c
RUN cp opcua_pubsub.h build/opcua_pubsub.h && cp opcua_pubsub.c build/opcua_pubsub.c
WORKDIR build
RUN arm-linux-gnueabihf-gcc -c $COMPILER_FLAGS -D_GNU_SOURCE opcua_pubsub.c -o opcua_pubsub.o

# check and compile opcua_publisher.c
WORKDIR ..
ADD opcua_publisher.c .
RUN cppcheck opcua_publisher.c
RUN cp opcua_publisher.c build/opcua_publisher.c
WORKDIR build
RUN arm-linux-gnueabihf-gcc -c $COMPILER_FLAGS opcua_publisher.c -o opcua_publisher.o

# link the publisher application
RUN arm-linux-gnueabihf-gcc $LINKER_FLAGS opcua_publisher.o opcua_pubsub.o -o opcua_publisher_arm

# check and compile opcua_subscriber.c
WORKDIR ..
ADD opcua_subscriber.c .
RUN cppcheck opcua_subscriber.c
RUN cp opcua_subscriber.c build/opcua_subscriber.c
WORKDIR build
RUN arm-linux-gnueabihf-gcc -c $COMPILER_FLAGS opcua_subscriber.c -o opcua_subscriber.o

# link the subscriber application
RUN arm-linux-gnueabihf-gcc $LINKER_FLAGS opcua_subscriber.o opcua_pubsub.o -o opcua_subscriber_arm
