#include "opcua_pubsub.h"

static void
addReaderGroup(UA_Server *server, UA_NodeId connectionIdent, UA_NodeId *readerGroupIdent) {
    // add a reader group to the pubsub connection
    UA_ReaderGroupConfig readerGroupConfig;
    memset(&readerGroupConfig, 0, sizeof(UA_ReaderGroupConfig));
    readerGroupConfig.name = UA_STRING("ReaderGroupConfig");
    readerGroupConfig.rtLevel = UA_PUBSUB_RT_FIXED_SIZE;
    if (UA_Server_addReaderGroup(server, connectionIdent, &readerGroupConfig, readerGroupIdent) != UA_STATUSCODE_GOOD) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "UA_Server_addReaderGroup failed");
        exit(EXIT_FAILURE);
    }
}

static void
addDataSetReader(UA_Server *server, UA_DataSetReaderConfig *readerConfig, UA_NodeId readerGroupIdent, UA_NodeId *readerIdent) {
    // add a data set reader to the reader group
    UA_UInt16 publisherIdentifier = PUBLISHER_ID;
    memset(readerConfig, 0, sizeof(UA_DataSetReaderConfig));
    readerConfig->name = UA_STRING("ReaderConfig");
    readerConfig->publisherId.type = &UA_TYPES[UA_TYPES_UINT16];
    readerConfig->publisherId.data = &publisherIdentifier;
    readerConfig->writerGroupId = WRITER_GROUP_ID;
    readerConfig->dataSetWriterId = DATA_SET_WRITER_ID;
    readerConfig->messageSettings.encoding = UA_EXTENSIONOBJECT_DECODED;
    readerConfig->messageSettings.content.decoded.type = &UA_TYPES[UA_TYPES_UADPDATASETREADERMESSAGEDATATYPE];
    UA_UadpDataSetReaderMessageDataType *dataSetReaderMessage = UA_UadpDataSetReaderMessageDataType_new();
    dataSetReaderMessage->networkMessageContentMask = (UA_UadpNetworkMessageContentMask) UA_UADPNETWORKMESSAGECONTENTMASK_PUBLISHERID |
                                                      (UA_UadpNetworkMessageContentMask) UA_UADPNETWORKMESSAGECONTENTMASK_GROUPHEADER |
                                                      (UA_UadpNetworkMessageContentMask) UA_UADPNETWORKMESSAGECONTENTMASK_WRITERGROUPID |
                                                      (UA_UadpNetworkMessageContentMask) UA_UADPNETWORKMESSAGECONTENTMASK_PAYLOADHEADER;
    readerConfig->messageSettings.content.decoded.data = dataSetReaderMessage;
    UA_DataSetMetaDataType *metaData = &readerConfig->dataSetMetaData;
    UA_DataSetMetaDataType_init(metaData);
    metaData->name = UA_STRING("SubscribedDataSet");
    metaData->fieldsSize = 1;
    metaData->fields = (UA_FieldMetaData *) UA_Array_new(metaData->fieldsSize, &UA_TYPES[UA_TYPES_FIELDMETADATA]);
    if (!metaData->fields) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "UA_Array_new failed");
        exit(EXIT_FAILURE);
    }
    UA_FieldMetaData_init(&metaData->fields[0]);
    UA_NodeId_copy(&UA_TYPES[UA_TYPES_UINT64].typeId, &metaData->fields[0].dataType);
    metaData->fields[0].builtInType = UA_NS0ID_UINT64;
    metaData->fields[0].name = UA_STRING(VARIABLE_NAME);
    metaData->fields[0].valueRank = -1;
    if (UA_Server_addDataSetReader(server, readerGroupIdent, readerConfig, readerIdent) != UA_STATUSCODE_GOOD) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "UA_Server_addDataSetReader failed");
        exit(EXIT_FAILURE);
    }
    UA_UadpDataSetReaderMessageDataType_delete(dataSetReaderMessage);
}

static void
addSubscribedVariables(UA_Server *server, UA_NodeId dataSetReaderId, UA_DataSetReaderConfig *readerConfig, UA_NodeId *subscribedValueNodeId,
                       UA_NodeId readerGroupIdent) {
    // add a folder for the subscribed data set to the nodeset and link it to the data set reader and initialize the field
    UA_NodeId folderId;
    UA_Variant variant;
    UA_String folderName = readerConfig->dataSetMetaData.name;
    UA_ObjectAttributes objectAttributes = UA_ObjectAttributes_default;
    UA_QualifiedName folderBrowseName;
    objectAttributes.displayName.locale = UA_STRING("en-US");
    objectAttributes.displayName.text = folderName;
    folderBrowseName.namespaceIndex = VARIABLE_NAMESPACE_INDEX;
    folderBrowseName.name = folderName;
    if (UA_Server_addObjectNode(server, UA_NODEID_NULL, UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER), UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
                                folderBrowseName, UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE), objectAttributes, NULL, &folderId) != UA_STATUSCODE_GOOD) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "UA_Server_addObjectNode failed");
        exit(EXIT_FAILURE);
    }
    if (UA_Server_DataSetReader_addTargetVariables(server, &folderId, dataSetReaderId, UA_PUBSUB_SDS_TARGET) != UA_STATUSCODE_GOOD) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "UA_Server_DataSetReader_addTargetVariables failed");
        exit(EXIT_FAILURE);
    }
    UA_QualifiedName browsePath[] = {UA_QUALIFIEDNAME(VARIABLE_NAMESPACE_INDEX, VARIABLE_NAME)};
    UA_BrowsePathResult browsePathResult = UA_Server_browseSimplifiedBrowsePath(server, folderId, 1, browsePath);
    if (browsePathResult.statusCode != UA_STATUSCODE_GOOD || browsePathResult.targetsSize == 0) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "UA_Server_browseSimplifiedBrowsePath failed");
        exit(EXIT_FAILURE);
    }
    *subscribedValueNodeId = browsePathResult.targets[0].targetId.nodeId;
    UA_UInt64 value = VARIABLE_START_VALUE;
    UA_Variant_setScalar(&variant, &value, &UA_TYPES[UA_TYPES_UINT64]);
    if (UA_Server_writeValue(server, *subscribedValueNodeId, variant) != UA_STATUSCODE_GOOD) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "UA_Server_writeValue failed");
        exit(EXIT_FAILURE);
    }
    UA_BrowsePathResult_deleteMembers(&browsePathResult);
    UA_free(readerConfig->dataSetMetaData.fields);
    if (UA_Server_freezeReaderGroupConfiguration(server, readerGroupIdent) != UA_STATUSCODE_GOOD) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "UA_Server_freezeReaderGroupConfiguration failed");
        exit(EXIT_FAILURE);
    }
    if (UA_Server_setReaderGroupOperational(server, readerGroupIdent) != UA_STATUSCODE_GOOD) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "UA_Server_setReaderGroupOperational failed");
        exit(EXIT_FAILURE);
    }
}

static void *
customSubscribeLoop(void *args) {
    // this is the function responsible for subscribing
    ThreadArguments *arguments = (ThreadArguments *) args;
    UA_Server *server = arguments->server;
    UA_ReaderGroup *readerGroup = arguments->data;
    UA_NodeId subscribedValueNodeId = *(UA_NodeId *) arguments->variable;
    UA_UInt64 *receivedValues = arguments->values;
    UA_UInt64 *receivedTimestamps = arguments->timestamps;
    UA_UInt64 measurements = arguments->measurements;
    UA_Boolean *running = arguments->running;
    UA_free(arguments);
    UA_UInt64 writeIndex = 0;
    UA_Variant beforeVariant, afterVariant;
    while (*running) {
        UA_Server_readValue(server, subscribedValueNodeId, &beforeVariant);
        UA_ReaderGroup_subscribeCallback(server, readerGroup);
        UA_UInt64 currentTimestamp = getCurrentTimestamp();
        UA_Server_readValue(server, subscribedValueNodeId, &afterVariant);
        UA_UInt64 beforeValue = *(UA_UInt64 *) beforeVariant.data;
        UA_UInt64 afterValue = *(UA_UInt64 *) afterVariant.data;
        UA_Variant_deleteMembers(&beforeVariant);
        UA_Variant_deleteMembers(&afterVariant);
        if (beforeValue != afterValue) {
            receivedValues[writeIndex] = afterValue;
            receivedTimestamps[writeIndex] = currentTimestamp;
            writeIndex++;
        }
        if (writeIndex == measurements) {
            *running = false;
        }
    }
    return NULL;
}

static int
run(UA_String *transportProfile, UA_NetworkAddressUrlDataType *networkAddressUrl, int port, UA_UInt64 cycle_time_ns, UA_UInt64 measurements) {
    // start the server and subscribe to the published fields
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    UA_Server *server = createServer(port);
    UA_DataSetReaderConfig readerConfig;
    UA_NodeId connectionIdent, readerGroupIdent, readerIdent, subscribedValueNodeId;
    pthread_t subscribeThread, serverThread;
    UA_UInt64 *receivedValues, *receivedTimestamps;
    addPubSubConnection(server, transportProfile, networkAddressUrl, &connectionIdent);
    setupSocket(server->pubSubManager.connections.tqh_first->channel->sockfd);
    addReaderGroup(server, connectionIdent, &readerGroupIdent);
    addDataSetReader(server, &readerConfig, readerGroupIdent, &readerIdent);
    addSubscribedVariables(server, readerIdent, &readerConfig, &subscribedValueNodeId, readerGroupIdent);
    UA_Boolean running = true;
    startServerThread(server, &running, &serverThread, CPU_ONE);
    startPubSubThread(server, customSubscribeLoop, cycle_time_ns, &subscribedValueNodeId, UA_ReaderGroup_findRGbyId(server, readerGroupIdent),
                      &subscribeThread, &receivedValues, &receivedTimestamps, measurements, &running, CPU_TWO);
    waitForThreadTermination(subscribeThread, false);
    UA_StatusCode serverReturnValue = waitForThreadTermination(serverThread, true);
    writeLogFile(SUBSCRIBE_LOG_FILE_NAME, receivedValues, receivedTimestamps, measurements);
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
