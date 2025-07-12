#include "open62541.h"
#include <pthread.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>

typedef struct
{
    void *value;
    const UA_DataType *type;
    pthread_mutex_t mutex;
} VariableContext;

UA_Boolean running = true;

// 信号处理函数，用于优雅停止服务器
static void stopHandler(int sig) {
    printf("\n收到停止信号，正在关闭服务器...\n");
    running = false;
}

static void onReadCallBack(UA_Server *server,
                           const UA_NodeId *sessionId,
                           void *sessionContext,
                           const UA_NodeId *nodeId,
                           void *nodeContext,
                           const UA_NumericRange *range,
                           UA_DataValue *value)
{
    VariableContext *context = (VariableContext *)nodeContext;
    if (!context) return;

    pthread_mutex_lock(&context->mutex);
    
    // 为不同类型进行深拷贝以确保线程安全
    if (context->type == &UA_TYPES[UA_TYPES_INT32]) {
        UA_Int32 *copyValue = UA_malloc(sizeof(UA_Int32));
        *copyValue = *(UA_Int32*)context->value;
        UA_Variant_setScalar(&value->value, copyValue, &UA_TYPES[UA_TYPES_INT32]);
    }
    else if (context->type == &UA_TYPES[UA_TYPES_FLOAT]) {
        UA_Float *copyValue = UA_malloc(sizeof(UA_Float));
        *copyValue = *(UA_Float*)context->value;
        UA_Variant_setScalar(&value->value, copyValue, &UA_TYPES[UA_TYPES_FLOAT]);
    }
    else if (context->type == &UA_TYPES[UA_TYPES_STRING]) {
        UA_String *copyValue = UA_malloc(sizeof(UA_String));
        UA_String_copy((UA_String*)context->value, copyValue);
        UA_Variant_setScalar(&value->value, copyValue, &UA_TYPES[UA_TYPES_STRING]);
    }
    
    value->hasValue = true;
    pthread_mutex_unlock(&context->mutex);
}

static UA_StatusCode writeVariableValue(const UA_Variant *value,
                                        void *targetValue,
                                        const UA_DataType *expectedType)
{
    if (value->type != expectedType)
    {
        return UA_STATUSCODE_BADTYPEMISMATCH;
    }

    if (expectedType == &UA_TYPES[UA_TYPES_INT32])
    {
        *(UA_Int32 *)targetValue = *(UA_Int32 *)value->data;
        printf("接收到Int32值: %d\n", *(UA_Int32 *)value->data);
    }
    else if (expectedType == &UA_TYPES[UA_TYPES_FLOAT])
    {
        *(UA_Float *)targetValue = *(UA_Float *)value->data;
        printf("接收到Float值: %f\n", *(UA_Float *)value->data);
    }
    else if (expectedType == &UA_TYPES[UA_TYPES_STRING])
    {
        UA_String *src = (UA_String *)value->data;
        UA_String *dst = (UA_String *)targetValue;
        
        // 先清理旧的字符串
        UA_String_clear(dst);
        
        UA_StatusCode status = UA_String_copy(src, dst);
        if (status == UA_STATUSCODE_GOOD)
        {
            printf("接收到String值: %.*s\n", (int)dst->length, dst->data);
        }
        else
        {
            printf("复制String值失败\n");
            return status;
        }
    }
    else
    {
        printf("不支持的类型: %s\n", value->type->typeName);
        return UA_STATUSCODE_BADTYPEMISMATCH;
    }

    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode onWriteCallback(UA_Server *server,
                                     const UA_NodeId *sessionId,
                                     void *sessionContext,
                                     const UA_NodeId *nodeId,
                                     void *nodeContext,
                                     const UA_Variant *value,
                                     const UA_DataSource *dataSource)
{
    VariableContext *context = (VariableContext *)nodeContext;
    if (!context) return UA_STATUSCODE_BADINTERNALERROR;

    pthread_mutex_lock(&context->mutex);
    UA_StatusCode status = writeVariableValue(value, context->value, context->type);
    pthread_mutex_unlock(&context->mutex);
    
    return status;
}

// 清理VariableContext资源
static void cleanupVariableContext(VariableContext *context) {
    if (!context) return;
    
    pthread_mutex_destroy(&context->mutex);
    
    // 根据类型清理值
    if (context->type == &UA_TYPES[UA_TYPES_STRING]) {
        UA_String_clear((UA_String*)context->value);
    }
    
    UA_free(context->value);
    UA_free(context);
}

static UA_NodeId addVariable(UA_Server *server,
                             UA_UInt16 nsIndex,
                             char *nodeName,
                             const UA_DataType *type,
                             void *value)
{
    UA_VariableAttributes attr = UA_VariableAttributes_default;
    UA_Variant_setScalar(&attr.value, value, type);
    attr.displayName = UA_LOCALIZEDTEXT("zh-CN", nodeName);
    attr.description = UA_LOCALIZEDTEXT("zh-CN", nodeName);
    attr.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
    attr.userAccessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;

    UA_NodeId variableNodeId = UA_NODEID_STRING(nsIndex, nodeName);

    VariableContext *context = (VariableContext *)UA_malloc(sizeof(VariableContext));
    
    // 分配并复制值
    if (type == &UA_TYPES[UA_TYPES_INT32]) {
        context->value = UA_malloc(sizeof(UA_Int32));
        *(UA_Int32*)context->value = *(UA_Int32*)value;
    }
    else if (type == &UA_TYPES[UA_TYPES_FLOAT]) {
        context->value = UA_malloc(sizeof(UA_Float));
        *(UA_Float*)context->value = *(UA_Float*)value;
    }
    else if (type == &UA_TYPES[UA_TYPES_STRING]) {
        context->value = UA_malloc(sizeof(UA_String));
        UA_String_copy((UA_String*)value, (UA_String*)context->value);
    }
    
    context->type = type;
    if (pthread_mutex_init(&context->mutex, NULL) != 0) {
        printf("互斥锁初始化失败\n");
        UA_free(context);
        return UA_NODEID_NULL;
    }

    UA_StatusCode retval = UA_Server_addVariableNode(server,
                              variableNodeId,
                              UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
                              UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
                              UA_QUALIFIEDNAME(nsIndex, nodeName),
                              UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
                              attr, NULL, &variableNodeId);

    if (retval != UA_STATUSCODE_GOOD) {
        printf("添加变量节点失败: %s\n", UA_StatusCode_name(retval));
        cleanupVariableContext(context);
        return UA_NODEID_NULL;
    }

    UA_ValueCallback callback;
    callback.onRead = onReadCallBack;
    callback.onWrite = onWriteCallback;  // 启用写入功能
    
    retval = UA_Server_setVariableNode_valueCallback(server, variableNodeId, callback);
    if (retval != UA_STATUSCODE_GOOD) {
        printf("设置变量回调失败: %s\n", UA_StatusCode_name(retval));
        cleanupVariableContext(context);
        return UA_NODEID_NULL;
    }

    UA_Server_setNodeContext(server, variableNodeId, context);
    printf("成功添加变量: %s\n", nodeName);
    return variableNodeId;
}

int main()
{
    // 设置信号处理
    signal(SIGINT, stopHandler);
    signal(SIGTERM, stopHandler);
    
    UA_Server *server = UA_Server_new();
    if (!server) {
        printf("创建服务器失败\n");
        return EXIT_FAILURE;
    }
    
    UA_ServerConfig_setDefault(UA_Server_getConfig(server));

    // 使用更有意义的命名空间URI
    const char *nsUri1 = "http://opcua.demo/integers";
    const char *nsUri2 = "http://opcua.demo/floats";
    const char *nsUri3 = "http://opcua.demo/strings";
    
    UA_UInt16 nsIndex1 = UA_Server_addNamespace(server, nsUri1);
    UA_UInt16 nsIndex2 = UA_Server_addNamespace(server, nsUri2);
    UA_UInt16 nsIndex3 = UA_Server_addNamespace(server, nsUri3);

    // 添加不同类型的变量
    UA_Int32 int32Value = 42;
    UA_NodeId int32NodeId = addVariable(server, nsIndex1, "Int32Variable", &UA_TYPES[UA_TYPES_INT32], &int32Value);

    UA_Float floatValue = 3.14f;
    UA_NodeId floatNodeId = addVariable(server, nsIndex2, "FloatVariable", &UA_TYPES[UA_TYPES_FLOAT], &floatValue);

    UA_String stringValue = UA_STRING_ALLOC("Hello OPC UA");
    UA_NodeId stringNodeId = addVariable(server, nsIndex3, "StringVariable", &UA_TYPES[UA_TYPES_STRING], &stringValue);

    // 检查是否所有变量都添加成功
    if (UA_NodeId_isNull(&int32NodeId) || UA_NodeId_isNull(&floatNodeId) || UA_NodeId_isNull(&stringNodeId)) {
        printf("某些变量添加失败，退出程序\n");
        UA_String_clear(&stringValue);
        UA_Server_delete(server);
        return EXIT_FAILURE;
    }

    printf("OPC UA服务器启动成功，监听端口: 4840\n");
    printf("按 Ctrl+C 优雅停止服务器\n");
    
    UA_StatusCode retVal = UA_Server_run(server, &running);
    
    printf("正在清理资源...\n");
    
    // 清理资源
    UA_String_clear(&stringValue);
    UA_Server_delete(server);
    
    printf("服务器已关闭\n");
    return retVal == UA_STATUSCODE_GOOD ? EXIT_SUCCESS : EXIT_FAILURE;
}