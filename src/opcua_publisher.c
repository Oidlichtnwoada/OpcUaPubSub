#include "opcua_pubsub.h"

static void
addPublishedDataSet(UA_Server *server, UA_NodeId *publishedDataSetIdent) {
    // add a published data set to the server
    UA_PublishedDataSetConfig publishedDataSetConfig;
    memset(&publishedDataSetConfig, 0, sizeof(UA_PublishedDataSetConfig));
    publishedDataSetConfig.publishedDataSetType = UA_PUBSUB_DATASET_PUBLISHEDITEMS;
    publishedDataSetConfig.name = UA_STRING("PublishedDataSetConfig");
    if (UA_Server_addPublishedDataSet(server, &publishedDataSetConfig, publishedDataSetIdent).addResult != UA_STATUSCODE_GOOD) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "UA_Server_addPublishedDataSet failed");
        exit(EXIT_FAILURE);
    }
}

static void
addDataSetFields(UA_Server *server, UA_DataValue **staticValueSource, UA_NodeId publishedDataSetIdent, UA_UInt64 *publishValue) {
    // create a basic configuration of a field in a PublishedDataSet and then add the published not to the data set
    UA_NodeId dataSetFieldIdent;
    *publishValue = VARIABLE_START_VALUE;
    UA_Variant_setScalar(&(*staticValueSource)->value, publishValue, &UA_TYPES[UA_TYPES_UINT64]);
    UA_DataSetFieldConfig dataSetFieldConfig;
    memset(&dataSetFieldConfig, 0, sizeof(UA_DataSetFieldConfig));
    dataSetFieldConfig.field.variable.rtValueSource.rtFieldSourceEnabled = UA_TRUE;
    dataSetFieldConfig.field.variable.rtValueSource.staticValueSource = staticValueSource;
    if (UA_Server_addDataSetField(server, publishedDataSetIdent, &dataSetFieldConfig, &dataSetFieldIdent).result != UA_STATUSCODE_GOOD) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "UA_Server_addDataSetField failed");
        exit(EXIT_FAILURE);
    }
}

static void
addWriterGroup(UA_Server *server, UA_UInt64 cycle_time_ns, UA_NodeId connectionIdent, UA_NodeId *writerGroupIdent) {
    // add a writer group to the pubsub connection
    UA_WriterGroupConfig writerGroupConfig;
    memset(&writerGroupConfig, 0, sizeof(UA_WriterGroupConfig));
    writerGroupConfig.name = UA_STRING("WriterGroupConfig");
    writerGroupConfig.publishingInterval = cycle_time_ns / 1000000.0;
    writerGroupConfig.enabled = UA_FALSE;
    writerGroupConfig.writerGroupId = WRITER_GROUP_ID;
    writerGroupConfig.rtLevel = UA_PUBSUB_RT_FIXED_SIZE;
    writerGroupConfig.encodingMimeType = UA_PUBSUB_ENCODING_UADP;
    writerGroupConfig.messageSettings.encoding = UA_EXTENSIONOBJECT_DECODED;
    writerGroupConfig.messageSettings.content.decoded.type = &UA_TYPES[UA_TYPES_UADPWRITERGROUPMESSAGEDATATYPE];
    UA_UadpWriterGroupMessageDataType *writerGroupMessage = UA_UadpWriterGroupMessageDataType_new();
    if (!writerGroupMessage) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "UA_UadpWriterGroupMessageDataType_new failed");
        exit(EXIT_FAILURE);
    }
    writerGroupMessage->networkMessageContentMask = (UA_UadpNetworkMessageContentMask) UA_UADPNETWORKMESSAGECONTENTMASK_PUBLISHERID |
                                                    (UA_UadpNetworkMessageContentMask) UA_UADPNETWORKMESSAGECONTENTMASK_GROUPHEADER |
                                                    (UA_UadpNetworkMessageContentMask) UA_UADPNETWORKMESSAGECONTENTMASK_WRITERGROUPID |
                                                    (UA_UadpNetworkMessageContentMask) UA_UADPNETWORKMESSAGECONTENTMASK_PAYLOADHEADER;
    writerGroupConfig.messageSettings.content.decoded.data = writerGroupMessage;
    if (UA_Server_addWriterGroup(server, connectionIdent, &writerGroupConfig, writerGroupIdent) != UA_STATUSCODE_GOOD) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "UA_Server_addWriterGroup failed");
        exit(EXIT_FAILURE);
    }
    UA_UadpWriterGroupMessageDataType_delete(writerGroupMessage);
}

static void
addDataSetWriter(UA_Server *server, UA_NodeId writerGroupIdent, UA_NodeId publishedDataSetIdent) {
    // add a data set writer to the writer group and create a link to the published data set
    UA_NodeId dataSetWriterIdent;
    UA_DataSetWriterConfig dataSetWriterConfig;
    memset(&dataSetWriterConfig, 0, sizeof(UA_DataSetWriterConfig));
    dataSetWriterConfig.name = UA_STRING("DataSetWriterConfig");
    dataSetWriterConfig.dataSetWriterId = DATA_SET_WRITER_ID;
    dataSetWriterConfig.keyFrameCount = 1;
    if (UA_Server_addDataSetWriter(server, writerGroupIdent, publishedDataSetIdent, &dataSetWriterConfig, &dataSetWriterIdent) != UA_STATUSCODE_GOOD) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "UA_Server_addDataSetWriter failed");
        exit(EXIT_FAILURE);
    }
    if (UA_Server_freezeWriterGroupConfiguration(server, writerGroupIdent) != UA_STATUSCODE_GOOD) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "UA_Server_freezeWriterGroupConfiguration failed");
        exit(EXIT_FAILURE);
    }
    if (UA_Server_setWriterGroupOperational(server, writerGroupIdent) != UA_STATUSCODE_GOOD) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "UA_Server_setWriterGroupOperational failed");
        exit(EXIT_FAILURE);
    }
}

static void *
customPublishLoop(void *args) {
    // this is the function responsible for publishing
    ThreadArguments *arguments = (ThreadArguments *) args;
    UA_Server *server = arguments->server;
    UA_WriterGroup *writerGroup = arguments->data;
    UA_UInt64 cycle_time_ns = arguments->cycle_time_ns;
    UA_UInt64 *publishValue = arguments->variable;
    UA_UInt64 *sentValues = arguments->values;
    UA_UInt64 *sentTimestamps = arguments->timestamps;
    UA_UInt64 measurements = arguments->measurements;
    UA_Boolean *running = arguments->running;
    UA_free(arguments);
    UA_UInt64 writeIndex = 0;
    while (*running) {
        *publishValue = *publishValue + 1;
        sentValues[writeIndex] = *publishValue;
        waitUntilNextEvent(0, cycle_time_ns);
        sentTimestamps[writeIndex] = getCurrentTimestamp();
        writeIndex++;
        UA_WriterGroup_publishCallback(server, writerGroup);
        if (writeIndex == measurements) {
            *running = false;
        }
    }
    return NULL;
}

static int
run(UA_String *transportProfile, UA_NetworkAddressUrlDataType *networkAddressUrl, int port, UA_UInt64 cycle_time_ns, UA_UInt64 measurements) {
    // start the server and publish the field
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    UA_Server *server = createServer(port);
    UA_NodeId connectionIdent, publishedDataSetIdent, writerGroupIdent;
    pthread_t publishThread, serverThread;
    UA_UInt64 *sentValues, *sentTimestamps;
    UA_DataValue *staticValueSource = UA_DataValue_new();
    UA_UInt64 *publishValue = UA_UInt64_new();
    addPubSubConnection(server, transportProfile, networkAddressUrl, &connectionIdent);
    setupSocket(server->pubSubManager.connections.tqh_first->channel->sockfd);
    addPublishedDataSet(server, &publishedDataSetIdent);
    addDataSetFields(server, &staticValueSource, publishedDataSetIdent, publishValue);
    addWriterGroup(server, cycle_time_ns, connectionIdent, &writerGroupIdent);
    addDataSetWriter(server, writerGroupIdent, publishedDataSetIdent);
    UA_Boolean running = true;
    startServerThread(server, &running, &serverThread, CPU_TWO);
    startPubSubThread(server, customPublishLoop, cycle_time_ns, publishValue, UA_WriterGroup_findWGbyId(server, writerGroupIdent),
                      &publishThread, &sentValues, &sentTimestamps, measurements, &running, CPU_ONE);
    waitForThreadTermination(publishThread, false);
    UA_StatusCode serverReturnValue = waitForThreadTermination(serverThread, true);
    writeLogFile(PUBLISH_LOG_FILE_NAME, sentValues, sentTimestamps, measurements);
    UA_free(publishValue);
    UA_free(staticValueSource);
    UA_Server_delete(server);
    return serverReturnValue == UA_STATUSCODE_GOOD ? EXIT_SUCCESS : EXIT_FAILURE;
}

int main(int argc, char **argv) {
    // parse and pass command line parameters
    if (argc == 6) {
        UA_String transportProfile;
        UA_NetworkAddressUrlDataType networkAddressUrl;
        int port;
        UA_UInt64 cycle_time_ns;
        UA_UInt64 measurements;
        fillArguments(argv, &transportProfile, &networkAddressUrl, &port, &cycle_time_ns, &measurements);
        return run(&transportProfile, &networkAddressUrl, port, cycle_time_ns, measurements);
    } else {
        usage(argv[0]);
        return EXIT_SUCCESS;
    }
}
