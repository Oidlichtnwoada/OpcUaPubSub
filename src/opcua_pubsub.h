#pragma once

// included amalgamated source file because header files lack essential declarations
#include "open62541.c"

#include <pthread.h>
#include <linux/ip.h>

// do not change defines
#define NS_IN_ONE_SECOND 1000000000ULL
#define RT_THREAD_PRIORITY 99
#define NON_RT_THREAD_PRIORITY 1
#define CPU_ONE 0
#define CPU_TWO 1
#define CLOCK CLOCK_REALTIME
#define SOCKET_PRIORITY 7
#define TYPE_OF_SERVICE (IPTOS_MINCOST | IPTOS_RELIABILITY | IPTOS_THROUGHPUT)
#define PUBLISH_LOG_FILE_NAME "publish.csv"
#define SUBSCRIBE_LOG_FILE_NAME "subscribe.csv"
#define TRANSPORT_PROFILE "http://opcfoundation.org/UA-Profile/Transport/pubsub-udp-uadp"
#define VARIABLE_NAMESPACE_INDEX 1

// change defines as you wish
#define VARIABLE_START_VALUE 0
#define VARIABLE_NAME "Counter"
#define PUBLISHER_ID 1
#define WRITER_GROUP_ID 1
#define DATA_SET_WRITER_ID 1

typedef struct {
    UA_Server *server;
    void *data;
    UA_UInt64 cycle_time_ns;
    void *variable;
    UA_UInt64 *values;
    UA_UInt64 *timestamps;
    UA_UInt64 measurements;
    UA_Boolean *running;
} ThreadArguments;

void
usage(char *name);

void
writeLogFile(char *fileName, UA_UInt64 *values, UA_UInt64 *timestamps, UA_UInt64 measurements);

UA_UInt64
getCurrentTimestamp(void);

void
waitUntilNextEvent(UA_UInt64 ns_offset, UA_UInt64 cycle_time_ns);

void
startThread(pthread_t *thread, void *(*routine)(void *), ThreadArguments *args, int priority, int cpu);

UA_StatusCode
waitForThreadTermination(pthread_t thread, UA_Boolean useReturnValue);

void
setupSocket(int sockfd);

void
signalHandler(int sig);

UA_StatusCode
UA_PubSubManager_addRepeatedCallback(UA_Server *server, UA_ServerCallback callback, void *data, UA_Double interval_ms, UA_UInt64 *callbackId);

void
UA_PubSubManager_removeRepeatedPubSubCallback(UA_Server *server, UA_UInt64 callbackId);

void *
startServer(void *args);

void
startServerThread(UA_Server *server, UA_Boolean *running, pthread_t *serverThread, int cpu);

void
addPubSubConnection(UA_Server *server, UA_String *transportProfile, UA_NetworkAddressUrlDataType *networkAddressUrl, UA_NodeId *connectionIdent);

void
startPubSubThread(UA_Server *server, void *(*routine)(void *), UA_UInt64 cycle_time_ns, void *variable, void *group, pthread_t *thread,
                  UA_UInt64 **values, UA_UInt64 **timestamps, UA_UInt64 measurements, UA_Boolean *running, int cpu);

void
fillArguments(char **argv, UA_String *transportProfile, UA_NetworkAddressUrlDataType *networkAddressUrl,
              int *port, UA_UInt64 *cycle_time_ns, UA_UInt64 *measurements);

UA_Server *
createServer(int port);
