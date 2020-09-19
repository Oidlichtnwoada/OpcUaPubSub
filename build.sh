#!/bin/bash

docker build -t opc_ua_pub_sub:latest ./docker && \
ID=$(docker run -id opc_ua_pub_sub:latest) && \
docker cp "$ID":/open62541/build/opcua_publisher_arm ./build && \
docker cp "$ID":/open62541/build/opcua_subscriber_arm ./build && \
docker cp "$ID":/open62541/build/open62541.h ./docker/open62541.h && \
docker cp "$ID":/open62541/build/open62541.c ./docker/open62541.c && \
docker kill "$ID"
