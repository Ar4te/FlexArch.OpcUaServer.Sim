#include "../includes/open62541.h"
#include <pthread.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <stdarg.h>

// 包含配置文件（如果存在）
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

// ==================== 常量定义 ====================
#define MAX_VARIABLES 100
#define MAX_OBJECTS 50
#define MAX_METHODS 20
#define MAX_EVENTS 10
#define SERVER_PORT 4840
#define SIMULATION_INTERVAL_MS 1000
#define LOG_BUFFER_SIZE 1024

// ==================== 枚举类型 ====================
typedef enum
{
    SIMULATION_NONE,
    SIMULATION_SINE_WAVE,
    SIMULATION_RANDOM,
    SIMULATION_COUNTER,
    SIMULATION_SQUARE_WAVE
} SimulationType;

typedef enum
{
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_ERROR
} LogLevel;

// ==================== 结构体定义 ====================
typedef struct
{
    void *value;
    const UA_DataType *type;
    pthread_mutex_t mutex;
    SimulationType simulation;
    double simulationParam1; // 频率或范围
    double simulationParam2; // 振幅或最小值
    double simulationParam3; // 偏移或最大值
    time_t lastUpdate;
    UA_Boolean hasAlarm;
    double alarmThreshold;
    UA_Boolean alarmState;
} VariableContext;

typedef struct
{
    UA_NodeId nodeId;
    char name[64];
    UA_NodeId parentNodeId;
    VariableContext *variables[10];
    int variableCount;
} ObjectContext;

typedef struct
{
    UA_NodeId nodeId;
    char name[64];
    UA_NodeId parentNodeId;
    UA_StatusCode (*callback)(UA_Server *server, const UA_NodeId *objectId,
                              const UA_Variant *input, UA_Variant *output);
} MethodContext;

typedef struct
{
    UA_NodeId eventTypeId;
    char name[64];
    UA_Boolean isActive;
    time_t lastTriggered;
} EventContext;

typedef struct
{
    UA_Server *server;
    pthread_t simulationThread;
    pthread_t diagnosticsThread;
    UA_Boolean running;

    // 统计信息
    UA_UInt64 totalRequests;
    UA_UInt64 totalErrors;
    UA_UInt32 connectedClients;
    time_t startTime;

    // 配置
    LogLevel logLevel;
    UA_Boolean enableSecurity;
    UA_Boolean enableDiagnostics;

    // 存储上下文
    VariableContext *variables[MAX_VARIABLES];
    ObjectContext *objects[MAX_OBJECTS];
    MethodContext *methods[MAX_METHODS];
    EventContext *events[MAX_EVENTS];
    int variableCount;
    int objectCount;
    int methodCount;
    int eventCount;
} ServerContext;

// ==================== 全局变量 ====================
ServerContext g_serverContext = {0};

// ==================== 日志系统 ====================
void logMessage(LogLevel level, const char *format, ...)
{
    if (level < g_serverContext.logLevel)
        return;

    time_t now = time(NULL);
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", localtime(&now));

    const char *levelStr[] = {"DEBUG", "INFO", "WARNING", "ERROR"};

    va_list args;
    va_start(args, format);

    printf("[%s] [%s] ", timeStr, levelStr[level]);
    vprintf(format, args);
    printf("\n");

    va_end(args);
}

// ==================== 信号处理 ====================
static void stopHandler(int sig)
{
    logMessage(LOG_LEVEL_INFO, "收到停止信号，正在关闭服务器...");
    g_serverContext.running = false;
}

// ==================== 数据模拟函数 ====================
void updateSimulatedValue(VariableContext *context)
{
    if (context->simulation == SIMULATION_NONE)
        return;

    time_t now = time(NULL);
    double timeDiff = difftime(now, context->lastUpdate);

    switch (context->simulation)
    {
    case SIMULATION_SINE_WAVE:
    {
        if (context->type == &UA_TYPES[UA_TYPES_FLOAT])
        {
            double frequency = context->simulationParam1;
            double amplitude = context->simulationParam2;
            double offset = context->simulationParam3;
            *(UA_Float *)context->value = (UA_Float)(amplitude * sin(2 * M_PI * frequency * now / 60.0) + offset);
        }
        else if (context->type == &UA_TYPES[UA_TYPES_DOUBLE])
        {
            double frequency = context->simulationParam1;
            double amplitude = context->simulationParam2;
            double offset = context->simulationParam3;
            *(UA_Double *)context->value = amplitude * sin(2 * M_PI * frequency * now / 60.0) + offset;
        }
        break;
    }
    case SIMULATION_RANDOM:
    {
        if (context->type == &UA_TYPES[UA_TYPES_INT32])
        {
            int min = (int)context->simulationParam2;
            int max = (int)context->simulationParam3;
            *(UA_Int32 *)context->value = min + rand() % (max - min + 1);
        }
        else if (context->type == &UA_TYPES[UA_TYPES_FLOAT])
        {
            float min = (float)context->simulationParam2;
            float max = (float)context->simulationParam3;
            *(UA_Float *)context->value = min + (float)rand() / RAND_MAX * (max - min);
        }
        break;
    }
    case SIMULATION_COUNTER:
    {
        if (context->type == &UA_TYPES[UA_TYPES_INT32])
        {
            *(UA_Int32 *)context->value += (UA_Int32)context->simulationParam1;
        }
        else if (context->type == &UA_TYPES[UA_TYPES_UINT32])
        {
            *(UA_UInt32 *)context->value += (UA_UInt32)context->simulationParam1;
        }
        break;
    }
    case SIMULATION_SQUARE_WAVE:
    {
        if (context->type == &UA_TYPES[UA_TYPES_BOOLEAN])
        {
            double period = context->simulationParam1;
            *(UA_Boolean *)context->value = (UA_Boolean)((now % (int)period) < (period / 2));
        }
        break;
    }
    }

    context->lastUpdate = now;

    // 检查报警条件
    if (context->hasAlarm && context->type == &UA_TYPES[UA_TYPES_FLOAT])
    {
        UA_Boolean newAlarmState = (*(UA_Float *)context->value > context->alarmThreshold);
        if (newAlarmState != context->alarmState)
        {
            context->alarmState = newAlarmState;
            logMessage(LOG_LEVEL_WARNING, "报警状态变更: %s", newAlarmState ? "激活" : "解除");
        }
    }
}

// ==================== 数据模拟线程 ====================
void *simulationThread(void *arg)
{
    logMessage(LOG_LEVEL_INFO, "数据模拟线程已启动");

    while (g_serverContext.running)
    {
        for (int i = 0; i < g_serverContext.variableCount; i++)
        {
            if (g_serverContext.variables[i])
            {
                pthread_mutex_lock(&g_serverContext.variables[i]->mutex);
                updateSimulatedValue(g_serverContext.variables[i]);
                pthread_mutex_unlock(&g_serverContext.variables[i]->mutex);
            }
        }

        usleep(SIMULATION_INTERVAL_MS * 1000);
    }

    logMessage(LOG_LEVEL_INFO, "数据模拟线程已结束");
    return NULL;
}

// ==================== 诊断线程 ====================
void *diagnosticsThread(void *arg)
{
    logMessage(LOG_LEVEL_INFO, "诊断线程已启动");

    while (g_serverContext.running)
    {
        time_t uptime = time(NULL) - g_serverContext.startTime;

        if (g_serverContext.enableDiagnostics)
        {
            logMessage(LOG_LEVEL_INFO, "服务器运行时间: %ld秒, 总请求数: %llu, 错误数: %llu, 连接客户端: %u",
                       uptime,
                       (unsigned long long)g_serverContext.totalRequests,
                       (unsigned long long)g_serverContext.totalErrors,
                       g_serverContext.connectedClients);
        }

        sleep(30); // 每30秒输出一次诊断信息
    }

    logMessage(LOG_LEVEL_INFO, "诊断线程已结束");
    return NULL;
}

// ==================== 回调函数 ====================
static void onReadCallBack(UA_Server *server,
                           const UA_NodeId *sessionId,
                           void *sessionContext,
                           const UA_NodeId *nodeId,
                           void *nodeContext,
                           const UA_NumericRange *range,
                           UA_DataValue *value)
{
    VariableContext *context = (VariableContext *)nodeContext;
    if (!context)
        return;

    g_serverContext.totalRequests++;

    pthread_mutex_lock(&context->mutex);

    // 为不同类型进行深拷贝以确保线程安全
    if (context->type == &UA_TYPES[UA_TYPES_INT32])
    {
        UA_Int32 *copyValue = UA_malloc(sizeof(UA_Int32));
        *copyValue = *(UA_Int32 *)context->value;
        UA_Variant_setScalar(&value->value, copyValue, &UA_TYPES[UA_TYPES_INT32]);
    }
    else if (context->type == &UA_TYPES[UA_TYPES_UINT32])
    {
        UA_UInt32 *copyValue = UA_malloc(sizeof(UA_UInt32));
        *copyValue = *(UA_UInt32 *)context->value;
        UA_Variant_setScalar(&value->value, copyValue, &UA_TYPES[UA_TYPES_UINT32]);
    }
    else if (context->type == &UA_TYPES[UA_TYPES_FLOAT])
    {
        UA_Float *copyValue = UA_malloc(sizeof(UA_Float));
        *copyValue = *(UA_Float *)context->value;
        UA_Variant_setScalar(&value->value, copyValue, &UA_TYPES[UA_TYPES_FLOAT]);
    }
    else if (context->type == &UA_TYPES[UA_TYPES_DOUBLE])
    {
        UA_Double *copyValue = UA_malloc(sizeof(UA_Double));
        *copyValue = *(UA_Double *)context->value;
        UA_Variant_setScalar(&value->value, copyValue, &UA_TYPES[UA_TYPES_DOUBLE]);
    }
    else if (context->type == &UA_TYPES[UA_TYPES_BOOLEAN])
    {
        UA_Boolean *copyValue = UA_malloc(sizeof(UA_Boolean));
        *copyValue = *(UA_Boolean *)context->value;
        UA_Variant_setScalar(&value->value, copyValue, &UA_TYPES[UA_TYPES_BOOLEAN]);
    }
    else if (context->type == &UA_TYPES[UA_TYPES_STRING])
    {
        UA_String *copyValue = UA_malloc(sizeof(UA_String));
        UA_String_copy((UA_String *)context->value, copyValue);
        UA_Variant_setScalar(&value->value, copyValue, &UA_TYPES[UA_TYPES_STRING]);
    }
    else if (context->type == &UA_TYPES[UA_TYPES_DATETIME])
    {
        UA_DateTime *copyValue = UA_malloc(sizeof(UA_DateTime));
        *copyValue = *(UA_DateTime *)context->value;
        UA_Variant_setScalar(&value->value, copyValue, &UA_TYPES[UA_TYPES_DATETIME]);
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
        g_serverContext.totalErrors++;
        return UA_STATUSCODE_BADTYPEMISMATCH;
    }

    logMessage(LOG_LEVEL_DEBUG, "触发值写入操作2: %s", value->data);
    if (expectedType == &UA_TYPES[UA_TYPES_INT32])
    {
        *(UA_Int32 *)targetValue = *(UA_Int32 *)value->data;
        logMessage(LOG_LEVEL_DEBUG, "写入Int32值: %d", *(UA_Int32 *)value->data);
    }
    else if (expectedType == &UA_TYPES[UA_TYPES_UINT32])
    {
        *(UA_UInt32 *)targetValue = *(UA_UInt32 *)value->data;
        logMessage(LOG_LEVEL_DEBUG, "写入UInt32值: %u", *(UA_UInt32 *)value->data);
    }
    else if (expectedType == &UA_TYPES[UA_TYPES_FLOAT])
    {
        *(UA_Float *)targetValue = *(UA_Float *)value->data;
        logMessage(LOG_LEVEL_DEBUG, "写入Float值: %f", *(UA_Float *)value->data);
    }
    else if (expectedType == &UA_TYPES[UA_TYPES_DOUBLE])
    {
        *(UA_Double *)targetValue = *(UA_Double *)value->data;
        logMessage(LOG_LEVEL_DEBUG, "写入Double值: %f", *(UA_Double *)value->data);
    }
    else if (expectedType == &UA_TYPES[UA_TYPES_BOOLEAN])
    {
        *(UA_Boolean *)targetValue = *(UA_Boolean *)value->data;
        logMessage(LOG_LEVEL_DEBUG, "写入Boolean值: %s", *(UA_Boolean *)value->data ? "true" : "false");
    }
    else if (expectedType == &UA_TYPES[UA_TYPES_STRING])
    {
        UA_String *src = (UA_String *)value->data;
        UA_String *dst = (UA_String *)targetValue;
        UA_String_clear(dst);
        UA_StatusCode status = UA_String_copy(src, dst);
        if (status == UA_STATUSCODE_GOOD)
        {
            logMessage(LOG_LEVEL_DEBUG, "写入String值: %.*s", (int)dst->length, dst->data);
        }
        else
        {
            logMessage(LOG_LEVEL_ERROR, "复制String值失败");
            g_serverContext.totalErrors++;
            return status;
        }
    }
    else if (expectedType == &UA_TYPES[UA_TYPES_DATETIME])
    {
        *(UA_DateTime *)targetValue = *(UA_DateTime *)value->data;
        logMessage(LOG_LEVEL_DEBUG, "写入DateTime值");
    }
    else
    {
        logMessage(LOG_LEVEL_ERROR, "不支持的类型: %s", value->type->typeName);
        g_serverContext.totalErrors++;
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

    logMessage(LOG_LEVEL_DEBUG, "触发值写入操作1: %s", value->data);
    VariableContext *context = (VariableContext *)nodeContext;
    if (!context)
    {
        g_serverContext.totalErrors++;
        return UA_STATUSCODE_BADINTERNALERROR;
    }

    g_serverContext.totalRequests++;

    pthread_mutex_lock(&context->mutex);
    UA_StatusCode status = writeVariableValue(value, context->value, context->type);
    pthread_mutex_unlock(&context->mutex);

    return status;
}

// ==================== 方法回调函数 ====================
static UA_StatusCode helloMethodCallback(UA_Server *server,
                                         const UA_NodeId *sessionId,
                                         void *sessionContext,
                                         const UA_NodeId *methodId,
                                         void *methodContext,
                                         const UA_NodeId *objectId,
                                         void *objectContext,
                                         size_t inputSize,
                                         const UA_Variant *input,
                                         size_t outputSize,
                                         UA_Variant *output)
{

    logMessage(LOG_LEVEL_INFO, "Hello方法被调用");

    if (inputSize > 0 && input[0].type == &UA_TYPES[UA_TYPES_STRING])
    {
        UA_String *inputStr = (UA_String *)input[0].data;
        logMessage(LOG_LEVEL_INFO, "收到输入: %.*s", (int)inputStr->length, inputStr->data);
    }

    if (outputSize > 0)
    {
        UA_String outputStr = UA_STRING_ALLOC("Hello from OPC UA Server!");
        UA_Variant_setScalar(&output[0], &outputStr, &UA_TYPES[UA_TYPES_STRING]);
    }

    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode calculateMethodCallback(UA_Server *server,
                                             const UA_NodeId *sessionId,
                                             void *sessionContext,
                                             const UA_NodeId *methodId,
                                             void *methodContext,
                                             const UA_NodeId *objectId,
                                             void *objectContext,
                                             size_t inputSize,
                                             const UA_Variant *input,
                                             size_t outputSize,
                                             UA_Variant *output)
{

    logMessage(LOG_LEVEL_INFO, "Calculate方法被调用");

    if (inputSize >= 2 &&
        input[0].type == &UA_TYPES[UA_TYPES_DOUBLE] &&
        input[1].type == &UA_TYPES[UA_TYPES_DOUBLE])
    {

        UA_Double a = *(UA_Double *)input[0].data;
        UA_Double b = *(UA_Double *)input[1].data;
        UA_Double result = a + b;

        logMessage(LOG_LEVEL_INFO, "计算: %f + %f = %f", a, b, result);

        if (outputSize > 0)
        {
            UA_Double *outputValue = UA_malloc(sizeof(UA_Double));
            *outputValue = result;
            UA_Variant_setScalar(&output[0], outputValue, &UA_TYPES[UA_TYPES_DOUBLE]);
        }
    }
    ///
    return UA_STATUSCODE_GOOD;
}

// ==================== 事件处理 ====================
static void triggerCustomEvent(UA_Server *server, UA_NodeId eventTypeId, const char *message)
{
    UA_NodeId eventNodeId = UA_NODEID_NUMERIC(1, 0);
    // UA_EventNotification notification = UA_EventNotification_default;
    // UA_EventNotification_init(&notification);

    // 设置事件字段
    UA_Variant eventId;
    UA_Variant_init(&eventId);
    UA_ByteString eventIdValue = UA_BYTESTRING_ALLOC("CustomEvent");
    UA_Variant_setScalar(&eventId, &eventIdValue, &UA_TYPES[UA_TYPES_BYTESTRING]);

    UA_Variant eventType;
    UA_Variant_init(&eventType);
    UA_Variant_setScalar(&eventType, &eventTypeId, &UA_TYPES[UA_TYPES_NODEID]);

    UA_Variant sourceNode;
    UA_Variant_init(&sourceNode);
    UA_NodeId serverNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER);
    UA_Variant_setScalar(&sourceNode, &serverNodeId, &UA_TYPES[UA_TYPES_NODEID]);

    UA_Variant sourceName;
    UA_Variant_init(&sourceName);
    UA_String sourceNameValue = UA_STRING_ALLOC("OPC UA Demo Server");
    UA_Variant_setScalar(&sourceName, &sourceNameValue, &UA_TYPES[UA_TYPES_STRING]);

    UA_Variant time;
    UA_Variant_init(&time);
    UA_DateTime currentTime = UA_DateTime_now();
    UA_Variant_setScalar(&time, &currentTime, &UA_TYPES[UA_TYPES_DATETIME]);

    UA_Variant messageVar;
    UA_Variant_init(&messageVar);
    UA_String messageValue = UA_STRING_ALLOC(message);
    UA_Variant_setScalar(&messageVar, &messageValue, &UA_TYPES[UA_TYPES_STRING]);

    logMessage(LOG_LEVEL_INFO, "触发事件: %s", message);

    // 清理资源
    UA_ByteString_clear(&eventIdValue);
    UA_String_clear(&sourceNameValue);
    UA_String_clear(&messageValue);
}

// ==================== 资源管理 ====================
static void cleanupVariableContext(VariableContext *context)
{
    if (!context)
        return;

    pthread_mutex_destroy(&context->mutex);

    if (context->type == &UA_TYPES[UA_TYPES_STRING])
    {
        UA_String_clear((UA_String *)context->value);
    }

    UA_free(context->value);
    UA_free(context);
}

// ==================== 节点创建函数 ====================
static UA_NodeId addVariable(UA_Server *server,
                             UA_UInt16 nsIndex,
                             const char *nodeName,
                             const UA_DataType *type,
                             void *value,
                             SimulationType simulation,
                             double param1, double param2, double param3)
{

    if (g_serverContext.variableCount >= MAX_VARIABLES)
    {
        logMessage(LOG_LEVEL_ERROR, "变量数量已达到最大值");
        return UA_NODEID_NULL;
    }

    UA_VariableAttributes attr = UA_VariableAttributes_default;
    UA_Variant_setScalar(&attr.value, value, type);
    attr.displayName = UA_LOCALIZEDTEXT("zh-CN", nodeName);
    attr.description = UA_LOCALIZEDTEXT("zh-CN", nodeName);
    attr.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
    attr.userAccessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;

    UA_NodeId variableNodeId = UA_NODEID_STRING(nsIndex, nodeName);

    VariableContext *context = (VariableContext *)UA_malloc(sizeof(VariableContext));

    // 根据类型分配并复制值
    if (type == &UA_TYPES[UA_TYPES_INT32])
    {
        context->value = UA_malloc(sizeof(UA_Int32));
        *(UA_Int32 *)context->value = *(UA_Int32 *)value;
    }
    else if (type == &UA_TYPES[UA_TYPES_UINT32])
    {
        context->value = UA_malloc(sizeof(UA_UInt32));
        *(UA_UInt32 *)context->value = *(UA_UInt32 *)value;
    }
    else if (type == &UA_TYPES[UA_TYPES_FLOAT])
    {
        context->value = UA_malloc(sizeof(UA_Float));
        *(UA_Float *)context->value = *(UA_Float *)value;
    }
    else if (type == &UA_TYPES[UA_TYPES_DOUBLE])
    {
        context->value = UA_malloc(sizeof(UA_Double));
        *(UA_Double *)context->value = *(UA_Double *)value;
    }
    else if (type == &UA_TYPES[UA_TYPES_BOOLEAN])
    {
        context->value = UA_malloc(sizeof(UA_Boolean));
        *(UA_Boolean *)context->value = *(UA_Boolean *)value;
    }
    else if (type == &UA_TYPES[UA_TYPES_STRING])
    {
        context->value = UA_malloc(sizeof(UA_String));
        UA_String_copy((UA_String *)value, (UA_String *)context->value);
    }
    else if (type == &UA_TYPES[UA_TYPES_DATETIME])
    {
        context->value = UA_malloc(sizeof(UA_DateTime));
        *(UA_DateTime *)context->value = *(UA_DateTime *)value;
    }

    context->type = type;
    context->simulation = simulation;
    context->simulationParam1 = param1;
    context->simulationParam2 = param2;
    context->simulationParam3 = param3;
    context->lastUpdate = time(NULL);
    context->hasAlarm = false;
    context->alarmThreshold = 0.0;
    context->alarmState = false;

    if (pthread_mutex_init(&context->mutex, NULL) != 0)
    {
        logMessage(LOG_LEVEL_ERROR, "互斥锁初始化失败");
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

    if (retval != UA_STATUSCODE_GOOD)
    {
        logMessage(LOG_LEVEL_ERROR, "添加变量节点失败: %s", UA_StatusCode_name(retval));
        cleanupVariableContext(context);
        return UA_NODEID_NULL;
    }

    UA_ValueCallback callback;
    callback.onRead = onReadCallBack;
    callback.onWrite = onWriteCallback;

    retval = UA_Server_setVariableNode_valueCallback(server, variableNodeId, callback);
    if (retval != UA_STATUSCODE_GOOD)
    {
        logMessage(LOG_LEVEL_ERROR, "设置变量回调失败: %s", UA_StatusCode_name(retval));
        cleanupVariableContext(context);
        return UA_NODEID_NULL;
    }

    UA_Server_setNodeContext(server, variableNodeId, context);

    // 保存到全局上下文
    g_serverContext.variables[g_serverContext.variableCount++] = context;

    logMessage(LOG_LEVEL_INFO, "成功添加变量: %s (模拟类型: %d)", nodeName, simulation);
    return variableNodeId;
}

static UA_NodeId addObject(UA_Server *server, UA_UInt16 nsIndex, const char *objectName)
{
    if (g_serverContext.objectCount >= MAX_OBJECTS)
    {
        logMessage(LOG_LEVEL_ERROR, "对象数量已达到最大值");
        return UA_NODEID_NULL;
    }

    UA_ObjectAttributes attr = UA_ObjectAttributes_default;
    attr.displayName = UA_LOCALIZEDTEXT("zh-CN", objectName);
    attr.description = UA_LOCALIZEDTEXT("zh-CN", objectName);

    UA_NodeId objectNodeId = UA_NODEID_STRING(nsIndex, objectName);

    UA_StatusCode retval = UA_Server_addObjectNode(server,
                                                   objectNodeId,
                                                   UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
                                                   UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
                                                   UA_QUALIFIEDNAME(nsIndex, objectName),
                                                   UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE),
                                                   attr, NULL, &objectNodeId);

    if (retval != UA_STATUSCODE_GOOD)
    {
        logMessage(LOG_LEVEL_ERROR, "添加对象节点失败: %s", UA_StatusCode_name(retval));
        return UA_NODEID_NULL;
    }

    ObjectContext *context = (ObjectContext *)UA_malloc(sizeof(ObjectContext));
    context->nodeId = objectNodeId;
    strncpy(context->name, objectName, sizeof(context->name) - 1);
    context->name[sizeof(context->name) - 1] = '\0';
    context->parentNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    context->variableCount = 0;

    g_serverContext.objects[g_serverContext.objectCount++] = context;

    logMessage(LOG_LEVEL_INFO, "成功添加对象: %s", objectName);
    return objectNodeId;
}

static UA_NodeId addMethod(UA_Server *server, UA_UInt16 nsIndex, UA_NodeId parentNodeId,
                           const char *methodName, UA_MethodCallback callback,
                           size_t inputArgumentsSize, const UA_Argument *inputArguments,
                           size_t outputArgumentsSize, const UA_Argument *outputArguments)
{

    if (g_serverContext.methodCount >= MAX_METHODS)
    {
        logMessage(LOG_LEVEL_ERROR, "方法数量已达到最大值");
        return UA_NODEID_NULL;
    }

    UA_MethodAttributes attr = UA_MethodAttributes_default;
    attr.displayName = UA_LOCALIZEDTEXT("zh-CN", methodName);
    attr.description = UA_LOCALIZEDTEXT("zh-CN", methodName);
    attr.executable = true;
    attr.userExecutable = true;

    UA_NodeId methodNodeId = UA_NODEID_STRING(nsIndex, methodName);

    UA_StatusCode retval = UA_Server_addMethodNode(server,
                                                   methodNodeId,
                                                   parentNodeId,
                                                   UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
                                                   UA_QUALIFIEDNAME(nsIndex, methodName),
                                                   attr, callback, NULL,
                                                   inputArgumentsSize, inputArguments,
                                                   outputArgumentsSize, outputArguments,
                                                   &methodNodeId);

    if (retval != UA_STATUSCODE_GOOD)
    {
        logMessage(LOG_LEVEL_ERROR, "添加方法节点失败: %s", UA_StatusCode_name(retval));
        return UA_NODEID_NULL;
    }

    MethodContext *context = (MethodContext *)UA_malloc(sizeof(MethodContext));
    context->nodeId = methodNodeId;
    strncpy(context->name, methodName, sizeof(context->name) - 1);
    context->name[sizeof(context->name) - 1] = '\0';
    context->parentNodeId = parentNodeId;

    g_serverContext.methods[g_serverContext.methodCount++] = context;

    logMessage(LOG_LEVEL_INFO, "成功添加方法: %s", methodName);
    return methodNodeId;
}

// ==================== 服务器初始化 ====================
static UA_StatusCode initializeServer()
{
    // 初始化全局上下文
    memset(&g_serverContext, 0, sizeof(ServerContext));
    g_serverContext.running = true;
    g_serverContext.logLevel = LOG_LEVEL_INFO;
    g_serverContext.enableDiagnostics = true;
    g_serverContext.startTime = time(NULL);

    // 创建服务器
    g_serverContext.server = UA_Server_new();
    if (!g_serverContext.server)
    {
        logMessage(LOG_LEVEL_ERROR, "创建服务器失败");
        return UA_STATUSCODE_BADINTERNALERROR;
    }

    UA_ServerConfig_setDefault(UA_Server_getConfig(g_serverContext.server));

    // 添加命名空间
    const char *nsUriBasic = "http://opcua.demo/basic";
    const char *nsUriSimulation = "http://opcua.demo/simulation";
    const char *nsUriObjects = "http://opcua.demo/objects";
    const char *nsUriMethods = "http://opcua.demo/methods";

    UA_UInt16 nsBasic = UA_Server_addNamespace(g_serverContext.server, nsUriBasic);
    UA_UInt16 nsSimulation = UA_Server_addNamespace(g_serverContext.server, nsUriSimulation);
    UA_UInt16 nsObjects = UA_Server_addNamespace(g_serverContext.server, nsUriObjects);
    UA_UInt16 nsMethods = UA_Server_addNamespace(g_serverContext.server, nsUriMethods);

    // 添加基本变量
    UA_Int32 int32Value = 42;
    addVariable(g_serverContext.server, nsBasic, "Int32Variable", &UA_TYPES[UA_TYPES_INT32],
                &int32Value, SIMULATION_NONE, 0, 0, 0);

    UA_UInt32 uint32Value = 123;
    addVariable(g_serverContext.server, nsBasic, "UInt32Variable", &UA_TYPES[UA_TYPES_UINT32],
                &uint32Value, SIMULATION_COUNTER, 1, 0, 0);

    UA_Float floatValue = 3.14f;
    addVariable(g_serverContext.server, nsBasic, "FloatVariable", &UA_TYPES[UA_TYPES_FLOAT],
                &floatValue, SIMULATION_NONE, 0, 0, 0);

    UA_Double doubleValue = 2.71828;
    addVariable(g_serverContext.server, nsBasic, "DoubleVariable", &UA_TYPES[UA_TYPES_DOUBLE],
                &doubleValue, SIMULATION_NONE, 0, 0, 0);

    UA_Boolean boolValue = true;
    addVariable(g_serverContext.server, nsBasic, "BooleanVariable", &UA_TYPES[UA_TYPES_BOOLEAN],
                &boolValue, SIMULATION_SQUARE_WAVE, 10, 0, 0);

    UA_String stringValue = UA_STRING_ALLOC("Hello OPC UA World!");
    addVariable(g_serverContext.server, nsBasic, "StringVariable", &UA_TYPES[UA_TYPES_STRING],
                &stringValue, SIMULATION_NONE, 0, 0, 0);

    UA_DateTime datetimeValue = UA_DateTime_now();
    addVariable(g_serverContext.server, nsBasic, "DateTimeVariable", &UA_TYPES[UA_TYPES_DATETIME],
                &datetimeValue, SIMULATION_NONE, 0, 0, 0);

    // 添加模拟变量
    UA_Float sineWave = 0.0f;
    addVariable(g_serverContext.server, nsSimulation, "SineWave", &UA_TYPES[UA_TYPES_FLOAT],
                &sineWave, SIMULATION_SINE_WAVE, 0.1, 10.0, 0.0);

    UA_Int32 randomInt = 0;
    addVariable(g_serverContext.server, nsSimulation, "RandomInteger", &UA_TYPES[UA_TYPES_INT32],
                &randomInt, SIMULATION_RANDOM, 0, 0, 100);

    UA_Float randomFloat = 0.0f;
    addVariable(g_serverContext.server, nsSimulation, "RandomFloat", &UA_TYPES[UA_TYPES_FLOAT],
                &randomFloat, SIMULATION_RANDOM, 0, 0.0, 1.0);

    UA_Int32 counter = 0;
    addVariable(g_serverContext.server, nsSimulation, "Counter", &UA_TYPES[UA_TYPES_INT32],
                &counter, SIMULATION_COUNTER, 1, 0, 0);

    // 添加对象
    UA_NodeId motorObjectId = addObject(g_serverContext.server, nsObjects, "Motor");
    UA_NodeId temperatureObjectId = addObject(g_serverContext.server, nsObjects, "Temperature");

    // 添加方法
    UA_Argument inputArgument;
    UA_Argument_init(&inputArgument);
    inputArgument.description = UA_LOCALIZEDTEXT("zh-CN", "输入字符串");
    inputArgument.name = UA_STRING("input");
    inputArgument.dataType = UA_TYPES[UA_TYPES_STRING].typeId;
    inputArgument.valueRank = UA_VALUERANK_SCALAR;

    UA_Argument outputArgument;
    UA_Argument_init(&outputArgument);
    outputArgument.description = UA_LOCALIZEDTEXT("zh-CN", "输出字符串");
    outputArgument.name = UA_STRING("output");
    outputArgument.dataType = UA_TYPES[UA_TYPES_STRING].typeId;
    outputArgument.valueRank = UA_VALUERANK_SCALAR;

    addMethod(g_serverContext.server, nsMethods, UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
              "HelloMethod", helloMethodCallback, 1, &inputArgument, 1, &outputArgument);

    // 计算方法
    UA_Argument calcInputArgs[2];
    UA_Argument_init(&calcInputArgs[0]);
    calcInputArgs[0].description = UA_LOCALIZEDTEXT("zh-CN", "第一个数");
    calcInputArgs[0].name = UA_STRING("a");
    calcInputArgs[0].dataType = UA_TYPES[UA_TYPES_DOUBLE].typeId;
    calcInputArgs[0].valueRank = UA_VALUERANK_SCALAR;

    UA_Argument_init(&calcInputArgs[1]);
    calcInputArgs[1].description = UA_LOCALIZEDTEXT("zh-CN", "第二个数");
    calcInputArgs[1].name = UA_STRING("b");
    calcInputArgs[1].dataType = UA_TYPES[UA_TYPES_DOUBLE].typeId;
    calcInputArgs[1].valueRank = UA_VALUERANK_SCALAR;

    UA_Argument calcOutputArg;
    UA_Argument_init(&calcOutputArg);
    calcOutputArg.description = UA_LOCALIZEDTEXT("zh-CN", "计算结果");
    calcOutputArg.name = UA_STRING("result");
    calcOutputArg.dataType = UA_TYPES[UA_TYPES_DOUBLE].typeId;
    calcOutputArg.valueRank = UA_VALUERANK_SCALAR;

    addMethod(g_serverContext.server, nsMethods, UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
              "CalculateMethod", calculateMethodCallback, 2, calcInputArgs, 1, &calcOutputArg);

    // 清理字符串
    UA_String_clear(&stringValue);

    logMessage(LOG_LEVEL_INFO, "服务器初始化完成");
    return UA_STATUSCODE_GOOD;
}

// ==================== 服务器清理 ====================
static void cleanupServer()
{
    logMessage(LOG_LEVEL_INFO, "正在清理服务器资源...");

    // 停止线程
    g_serverContext.running = false;

    if (g_serverContext.simulationThread)
    {
        pthread_join(g_serverContext.simulationThread, NULL);
    }

    if (g_serverContext.diagnosticsThread)
    {
        pthread_join(g_serverContext.diagnosticsThread, NULL);
    }

    // 清理变量上下文
    for (int i = 0; i < g_serverContext.variableCount; i++)
    {
        if (g_serverContext.variables[i])
        {
            cleanupVariableContext(g_serverContext.variables[i]);
        }
    }

    // 清理对象上下文
    for (int i = 0; i < g_serverContext.objectCount; i++)
    {
        if (g_serverContext.objects[i])
        {
            UA_free(g_serverContext.objects[i]);
        }
    }

    // 清理方法上下文
    for (int i = 0; i < g_serverContext.methodCount; i++)
    {
        if (g_serverContext.methods[i])
        {
            UA_free(g_serverContext.methods[i]);
        }
    }

    // 清理服务器
    if (g_serverContext.server)
    {
        UA_Server_delete(g_serverContext.server);
    }

    logMessage(LOG_LEVEL_INFO, "服务器资源清理完成");
}

// ==================== 主函数 ====================
int main(int argc, char *argv[])
{
    // 设置信号处理
    signal(SIGINT, stopHandler);
    signal(SIGTERM, stopHandler);

    // 初始化随机数生成器
    srand(time(NULL));

    logMessage(LOG_LEVEL_INFO, "====================================");
    logMessage(LOG_LEVEL_INFO, "    OPC UA 完整功能服务器模拟器");
    logMessage(LOG_LEVEL_INFO, "====================================");

    // 解析命令行参数
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--debug") == 0)
        {
            g_serverContext.logLevel = LOG_LEVEL_DEBUG;
        }
        else if (strcmp(argv[i], "--no-diagnostics") == 0)
        {
            g_serverContext.enableDiagnostics = false;
        }
        else if (strcmp(argv[i], "--help") == 0)
        {
            printf("用法: %s [选项]\n", argv[0]);
            printf("选项:\n");
            printf("  --debug           启用调试日志\n");
            printf("  --no-diagnostics  禁用诊断信息\n");
            printf("  --version         显示版本信息\n");
            printf("  --help            显示帮助信息\n");
            printf("\n");
            printf("示例:\n");
            printf("  %s                普通模式运行\n", argv[0]);
            printf("  %s --debug        调试模式运行\n", argv[0]);
            printf("  %s --no-diagnostics  禁用诊断信息运行\n", argv[0]);
            printf("\n");
            printf("连接信息:\n");
            printf("  端口: %d\n", SERVER_PORT);
            printf("  URL: opc.tcp://localhost:%d\n", SERVER_PORT);
            return 0;
        }
        else if (strcmp(argv[i], "--version") == 0)
        {
#ifdef HAVE_CONFIG_H
            printf("%s\n", getFullVersionString());
            printf("项目: %s\n", getProjectName());
            printf("描述: %s\n", getProjectDescription());
            printf("版本: %s\n", getVersionString());
            printf("构建类型: %s\n", CMAKE_BUILD_TYPE);
            printf("编译器: %s\n", CMAKE_C_COMPILER);
#else
            printf("OPC UA服务器模拟器 v1.0.0\n");
            printf("构建时间: %s %s\n", __DATE__, __TIME__);
#endif
            return 0;
        }
        else
        {
            printf("未知选项: %s\n", argv[i]);
            printf("使用 %s --help 获取帮助信息\n", argv[0]);
            return 1;
        }
    }

    // 初始化服务器
    UA_StatusCode retval = initializeServer();
    if (retval != UA_STATUSCODE_GOOD)
    {
        logMessage(LOG_LEVEL_ERROR, "服务器初始化失败");
        return EXIT_FAILURE;
    }

    // 启动模拟线程
    if (pthread_create(&g_serverContext.simulationThread, NULL, simulationThread, NULL) != 0)
    {
        logMessage(LOG_LEVEL_ERROR, "创建数据模拟线程失败");
        cleanupServer();
        return EXIT_FAILURE;
    }

    // 启动诊断线程
    if (g_serverContext.enableDiagnostics)
    {
        if (pthread_create(&g_serverContext.diagnosticsThread, NULL, diagnosticsThread, NULL) != 0)
        {
            logMessage(LOG_LEVEL_WARNING, "创建诊断线程失败");
        }
    }

    logMessage(LOG_LEVEL_INFO, "OPC UA服务器启动成功");
    logMessage(LOG_LEVEL_INFO, "监听端口: %d", SERVER_PORT);
    logMessage(LOG_LEVEL_INFO, "连接URL: opc.tcp://localhost:%d", SERVER_PORT);
    logMessage(LOG_LEVEL_INFO, "功能特性:");
    logMessage(LOG_LEVEL_INFO, "  - 多种数据类型支持 (Int32, UInt32, Float, Double, Boolean, String, DateTime)");
    logMessage(LOG_LEVEL_INFO, "  - 数据模拟 (正弦波, 随机数, 计数器, 方波)");
    logMessage(LOG_LEVEL_INFO, "  - 方法调用 (HelloMethod, CalculateMethod)");
    logMessage(LOG_LEVEL_INFO, "  - 对象节点组织");
    logMessage(LOG_LEVEL_INFO, "  - 事件通知");
    logMessage(LOG_LEVEL_INFO, "  - 实时诊断");
    logMessage(LOG_LEVEL_INFO, "按 Ctrl+C 优雅停止服务器");
    logMessage(LOG_LEVEL_INFO, "====================================");

    // 运行服务器
    retval = UA_Server_run(g_serverContext.server, &g_serverContext.running);

    // 清理资源
    cleanupServer();

    logMessage(LOG_LEVEL_INFO, "服务器已完全关闭");
    logMessage(LOG_LEVEL_INFO, "感谢使用 OPC UA 服务器模拟器！");

    return retval == UA_STATUSCODE_GOOD ? EXIT_SUCCESS : EXIT_FAILURE;
}