#include "opcua_pubsub.h"

void
usage(char *name) {
    // prints the usage string
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "usage: %s <pubsub_interface> <pubsub_url> <opc_ua_server_port> <cycle_time_ns>", name);
}

void
writeLogFile(char *fileName, UA_UInt64 *values, UA_UInt64 *timestamps, UA_UInt64 measurements) {
    // writes the acquired data into the log file
    FILE *logFile = fopen(fileName, "w");
    if (logFile == NULL) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "fopen failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    for (UA_UInt64 i = 0; i < measurements; i++) {
        fprintf(logFile, "%llu,%llu\n",
                (unsigned long long) values[i],
                (unsigned long long) timestamps[i]);
    }
    fclose(logFile);
    UA_free(values);
    UA_free(timestamps);
}

UA_UInt64
getCurrentTimestamp(void) {
    // returns the current timestamp in ns since the epoch
    struct timespec timespecTimestamp;
    UA_UInt64 integerTimestamp = 0;
    if (clock_gettime(CLOCK, &timespecTimestamp)) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "clock_gettime failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    integerTimestamp += (UA_UInt64) timespecTimestamp.tv_sec * NS_IN_ONE_SECOND;
    integerTimestamp += (UA_UInt64) timespecTimestamp.tv_nsec;
    return integerTimestamp;
}

void
waitUntilNextEvent(UA_UInt64 ns_offset, UA_UInt64 cycle_time_ns) {
    // wait until the next cyclic event
    struct timespec wakeUpTime;
    UA_UInt64 currentTimestamp = getCurrentTimestamp();
    UA_UInt64 remainder = currentTimestamp % cycle_time_ns;
    currentTimestamp += cycle_time_ns - ((remainder + ns_offset) % cycle_time_ns);
    wakeUpTime.tv_sec = currentTimestamp / NS_IN_ONE_SECOND;
    wakeUpTime.tv_nsec = currentTimestamp % NS_IN_ONE_SECOND;
    if (clock_nanosleep(CLOCK, TIMER_ABSTIME, &wakeUpTime, NULL)) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "clock_nanosleep failed");
        exit(EXIT_FAILURE);
    }
}

void
startThread(pthread_t *thread, void *(*routine)(void *), ThreadArguments *args, int priority, int cpu) {
    // start a thread with the given parameters
    pthread_attr_t threadAttributes;
    struct sched_param schedParam;
    cpu_set_t cpuSet;
    if (pthread_attr_init(&threadAttributes)) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "pthread_attr_init failed");
        exit(EXIT_FAILURE);
    }
    if (pthread_attr_getschedparam(&threadAttributes, &schedParam)) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "pthread_attr_getschedparam failed");
        exit(EXIT_FAILURE);
    }
    schedParam.sched_priority = priority;
    if (pthread_attr_setschedpolicy(&threadAttributes, SCHED_FIFO)) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "pthread_attr_setschedpolicy failed");
        exit(EXIT_FAILURE);
    }
    if (pthread_attr_setschedparam(&threadAttributes, &schedParam)) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "pthread_attr_setschedparam failed");
        exit(EXIT_FAILURE);
    }
    if (pthread_attr_setinheritsched(&threadAttributes, PTHREAD_EXPLICIT_SCHED)) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "pthread_attr_setinheritsched failed");
        exit(EXIT_FAILURE);
    }
    CPU_ZERO(&cpuSet);
    CPU_SET(cpu, &cpuSet);
    if (pthread_attr_setaffinity_np(&threadAttributes, sizeof(cpu_set_t), &cpuSet)) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "pthread_attr_setaffinity_np failed");
        exit(EXIT_FAILURE);
    }
    if (pthread_create(thread, &threadAttributes, routine, (void *) args)) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "pthread_create failed");
        exit(EXIT_FAILURE);
    }
    if (pthread_attr_destroy(&threadAttributes)) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "pthread_attr_destroy failed");
        exit(EXIT_FAILURE);
    }
}

UA_StatusCode
waitForThreadTermination(pthread_t thread, UA_Boolean useReturnValue) {
    // wait for the thread to terminate and output the return value as status code if requested
    void *returnValue;
    if (pthread_join(thread, &returnValue)) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "pthread_join failed");
        exit(EXIT_FAILURE);
    }
    if (useReturnValue) {
        UA_StatusCode statusCode = *(UA_StatusCode *) returnValue;
        UA_free(returnValue);
        return statusCode;
    } else {
        return UA_STATUSCODE_GOOD;
    }

}

void
setupSocket(int sockfd) {
    // setup the used socket for the pubsub communication with the right options
    int socketPriority = SOCKET_PRIORITY;
    if (setsockopt(sockfd, SOL_SOCKET, SO_PRIORITY, &socketPriority, sizeof(int))) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "setsockopt failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    int typeOfService = TYPE_OF_SERVICE;
    if (setsockopt(sockfd, IPPROTO_IP, IP_TOS, &typeOfService, sizeof(int))) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "setsockopt failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

void
signalHandler(int sig) {
    // display info message and exit the process
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "signalHandler triggered: %s", strsignal(sig));
    exit(EXIT_FAILURE);
}

UA_StatusCode
UA_PubSubManager_addRepeatedCallback(UA_Server *server, UA_ServerCallback callback, void *data, UA_Double interval_ms, UA_UInt64 *callbackId) {
    // this callback is called from framework, not used since own pubsub callback was implemented
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "UA_PubSubManager_addRepeatedCallback called: "
                                                      "%p %s %p %lf %p", (void *) server, (unsigned char *) &callback, data, interval_ms, (void *) callbackId);
    return UA_STATUSCODE_GOOD;
}

void
UA_PubSubManager_removeRepeatedPubSubCallback(UA_Server *server, UA_UInt64 callbackId) {
    // this callback is called from framework, not used since own pubsub callback was implemented
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "UA_PubSubManager_removeRepeatedPubSubCallback called: "
                                                      "%p %llu", (void *) server, (unsigned long long) callbackId);

}

void *
startServer(void *args) {
    // this routine executes the server tasks
    ThreadArguments *arguments = (ThreadArguments *) args;
    UA_Server *server = arguments->server;
    UA_Boolean *running = arguments->running;
    UA_free(arguments);
    UA_StatusCode *serverReturnValue = (UA_StatusCode *) UA_malloc(sizeof(UA_StatusCode));
    if (serverReturnValue == NULL) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "UA_malloc failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    *serverReturnValue = UA_Server_run(server, running);
    return serverReturnValue;
}

void
startServerThread(UA_Server *server, UA_Boolean *running, pthread_t *serverThread, int cpu) {
    // this function starts the server thread
    ThreadArguments *args = (ThreadArguments *) UA_malloc(sizeof(ThreadArguments));
    if (args == NULL) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "UA_malloc failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    args->server = server;
    args->running = running;
    startThread(serverThread, startServer, args, NON_RT_THREAD_PRIORITY, cpu);
}

void
addPubSubConnection(UA_Server *server, UA_String *transportProfile, UA_NetworkAddressUrlDataType *networkAddressUrl, UA_NodeId *connectionIdent) {
    // creates the pubsub connection with the specified transport layer
    UA_PubSubConnectionConfig connectionConfig;
    memset(&connectionConfig, 0, sizeof(UA_PubSubConnectionConfig));
    connectionConfig.name = UA_STRING("ConnectionConfig");
    connectionConfig.transportProfileUri = *transportProfile;
    connectionConfig.enabled = UA_TRUE;
    connectionConfig.publisherId.numeric = PUBLISHER_ID;
    UA_Variant_setScalar(&connectionConfig.address, networkAddressUrl, &UA_TYPES[UA_TYPES_NETWORKADDRESSURLDATATYPE]);
    if (UA_Server_addPubSubConnection(server, &connectionConfig, connectionIdent) != UA_STATUSCODE_GOOD) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "UA_Server_addPubSubConnection failed");
        exit(EXIT_FAILURE);
    }
    if (UA_PubSubConnection_regist(server, connectionIdent) != UA_STATUSCODE_GOOD) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "UA_PubSubConnection_regist failed");
        exit(EXIT_FAILURE);
    }
}

void
startPubSubThread(UA_Server *server, void *(*routine)(void *), UA_UInt64 cycle_time_ns, void *variable, void *group, pthread_t *thread,
                  UA_UInt64 **values, UA_UInt64 **timestamps, UA_UInt64 measurements, UA_Boolean *running, int cpu) {
    // build arguments for pubsub thread and start it
    *values = (UA_UInt64 *) UA_malloc(sizeof(UA_UInt64) * measurements);
    if (*values == NULL) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "UA_malloc failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    *timestamps = (UA_UInt64 *) UA_malloc(sizeof(UA_UInt64) * measurements);
    if (*timestamps == NULL) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "UA_malloc failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    ThreadArguments *args = (ThreadArguments *) UA_malloc(sizeof(ThreadArguments));
    if (args == NULL) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "UA_malloc failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    args->server = server;
    args->data = group;
    args->cycle_time_ns = cycle_time_ns;
    args->variable = variable;
    args->values = *values;
    args->timestamps = *timestamps;
    args->measurements = measurements;
    args->running = running;
    startThread(thread, routine, args, RT_THREAD_PRIORITY, cpu);
}

void
fillArguments(char **argv, UA_String *transportProfile, UA_NetworkAddressUrlDataType *networkAddressUrl,
              int *port, UA_UInt64 *cycle_time_ns, UA_UInt64 *measurements) {
    // fill the argument pointers with the parameters from the command line
    *transportProfile = UA_STRING(TRANSPORT_PROFILE);
    networkAddressUrl->networkInterface = UA_STRING(argv[1]);
    networkAddressUrl->url = UA_STRING(argv[2]);
    *port = atoi(argv[3]);
    unsigned long long temp;
    sscanf(argv[4], "%llu", &temp);
    *cycle_time_ns = temp;
    sscanf(argv[5], "%llu", &temp);
    *measurements = temp;
}

UA_Server *
createServer(int port) {
    // create a server struct configured to start at the specified port
    UA_Server *server = UA_Server_new();
    if (!server) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "UA_Server_new failed");
        exit(EXIT_FAILURE);
    }
    UA_ServerConfig *config = UA_Server_getConfig(server);
    UA_ServerConfig_setMinimal(config, port, NULL);
    config->pubsubTransportLayers = (UA_PubSubTransportLayer *) UA_calloc(1, sizeof(UA_PubSubTransportLayer));
    if (!config->pubsubTransportLayers) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "UA_calloc failed");
        exit(EXIT_FAILURE);
    }
    config->pubsubTransportLayers[0] = UA_PubSubTransportLayerUDPMP();
    config->pubsubTransportLayersSize++;
    return server;
}
