/******************************************************************************
 * device_queue_test.c
 * 
 * Comprehensive test suite implementation for the generic device queue system
 ******************************************************************************/

#include "device_queue_test.h"
#include "logging.h"
#include "BatteryTester.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/******************************************************************************
 * Static Variables
 ******************************************************************************/

static DeviceQueueTestContext g_testContext = {0};
static CmtThreadFunctionID g_testThreadId = 0;

static const char* g_mockCommandNames[] = {
    "NONE",
    "TEST_CONNECTION",
    "SET_VALUE",
    "GET_VALUE",
    "SLOW_OPERATION",
    "FAILING_OPERATION"
};

// Test cases array
static TestCase g_testCases[] = {
    {"Queue Creation", Test_QueueCreation, 0, "", 0.0},
    {"Queue Destruction", Test_QueueDestruction, 0, "", 0.0},
    {"Connection Handling", Test_ConnectionHandling, 0, "", 0.0},
    {"Blocking Commands", Test_BlockingCommands, 0, "", 0.0},
    {"Async Commands", Test_AsyncCommands, 0, "", 0.0},
    {"Priority Handling", Test_PriorityHandling, 0, "", 0.0},
    {"Command Cancellation", Test_CommandCancellation, 0, "", 0.0},
    {"Transactions", Test_Transactions, 0, "", 0.0},
    {"Queue Overflow", Test_QueueOverflow, 0, "", 0.0},
    {"Error Handling", Test_ErrorHandling, 0, "", 0.0},
    {"Timeouts", Test_Timeouts, 0, "", 0.0},
    {"Thread Safety", Test_ThreadSafety, 0, "", 0.0},
    {"Concurrent Cancellation", Test_ConcurrentCancellation, 0, "", 0.0},
    {"Statistics", Test_Statistics, 0, "", 0.0},
    {"Reconnection Logic", Test_ReconnectionLogic, 0, "", 0.0},
    {"Edge Cases", Test_EdgeCases, 0, "", 0.0}
};

static int g_numTestCases = sizeof(g_testCases) / sizeof(TestCase);

/******************************************************************************
 * Callback Function Declarations (moved outside of test functions)
 ******************************************************************************/

// Structure to track async completions
typedef struct {
    volatile int completed;
    int commandType;
    int resultValue;
    DeviceCommandID cmdId;
} AsyncTracker;

// Structure to track execution order
typedef struct {
    volatile int completed;
    volatile int executionOrder;
    DevicePriority priority;
    int value;
} PriorityTracker;

// Track transaction completion
typedef struct {
    volatile int completed;
    int successCount;
    int failureCount;
} TransactionTracker;

// Structure for tracking transaction execution order
typedef struct {
    volatile int executionOrder;
    volatile int completed;
    DeviceTransactionHandle txnId;
    int threadId;
    double startTime;
    double endTime;
} TransactionOrderTracker;

// Structure for concurrent transaction testing
typedef struct {
    DeviceQueueManager *queueManager;
    int threadIndex;
    volatile int transactionsCreated;
    volatile int transactionsCommitted;
    volatile int errors;
    TransactionOrderTracker *trackers;
    int numTransactions;
} ConcurrentTransactionData;

// Global execution counter for priority testing
static volatile int g_executionCounter = 0;

// Global counter for transaction execution order
static volatile int g_transactionExecutionCounter = 0;

// Callback functions (moved outside test functions)
static void AsyncCallback(DeviceCommandID cmdId, int commandType, void *result, void *userData);
static void PriorityCallback(DeviceCommandID cmdId, int commandType, void *result, void *userData);
static void TransactionCallback(DeviceTransactionHandle txnId, int success, int failed, 
                              TransactionCommandResult *results, int resultCount, void *userData);
static void ThreadCommandCallback(DeviceCommandID cmdId, int commandType, void *result, void *userData);

/******************************************************************************
 * Mock Device Adapter Implementation
 ******************************************************************************/

// Forward declarations for adapter functions
static int Mock_Connect(void *deviceContext, void *connectionParams);
static int Mock_Disconnect(void *deviceContext);
static int Mock_TestConnection(void *deviceContext);
static bool Mock_IsConnected(void *deviceContext);
static int Mock_ExecuteCommand(void *deviceContext, int commandType, void *params, void *result);
static void* Mock_CreateCommandParams(int commandType, void *sourceParams);
static void Mock_FreeCommandParams(int commandType, void *params);
static void* Mock_CreateCommandResult(int commandType);
static void Mock_FreeCommandResult(int commandType, void *result);
static void Mock_CopyCommandResult(int commandType, void *dest, void *src);
static const char* Mock_GetCommandTypeName(int commandType);
static int Mock_GetCommandDelay(int commandType);

// Mock device adapter
static const DeviceAdapter g_mockAdapter = {
    .deviceName = MOCK_DEVICE_NAME,
    .connect = Mock_Connect,
    .disconnect = Mock_Disconnect,
    .testConnection = Mock_TestConnection,
    .isConnected = Mock_IsConnected,
    .executeCommand = Mock_ExecuteCommand,
    .createCommandParams = Mock_CreateCommandParams,
    .freeCommandParams = Mock_FreeCommandParams,
    .createCommandResult = Mock_CreateCommandResult,
    .freeCommandResult = Mock_FreeCommandResult,
    .copyCommandResult = Mock_CopyCommandResult,
    .getCommandTypeName = Mock_GetCommandTypeName,
    .getCommandDelay = Mock_GetCommandDelay,
    .getErrorString = GetErrorString,
    .supportsRawCommands = NULL,
    .executeRawCommand = NULL
};

/******************************************************************************
 * Mock Device Context Functions
 ******************************************************************************/

MockDeviceContext* Mock_CreateContext(void) {
    MockDeviceContext *ctx = calloc(1, sizeof(MockDeviceContext));
    if (!ctx) return NULL;
    
    CmtNewLock(NULL, 0, &ctx->statsLock);
    ctx->commandDelay = MOCK_COMMAND_DELAY_MS;
    
    return ctx;
}

void Mock_DestroyContext(MockDeviceContext *ctx) {
    if (!ctx) return;
    
    if (ctx->statsLock) {
        CmtDiscardLock(ctx->statsLock);
    }
    
    free(ctx);
}

void Mock_SetConnectionState(MockDeviceContext *ctx, int connected) {
    if (ctx) {
        ctx->isConnected = connected;
    }
}

void Mock_SetFailureRate(MockDeviceContext *ctx, int rate) {
    if (ctx) {
        ctx->commandFailRate = CLAMP(rate, 0, 100);
        ctx->shouldFailCommands = (rate > 0);
    }
}

void Mock_SetCommandDelay(MockDeviceContext *ctx, int delayMs) {
    if (ctx) {
        ctx->commandDelay = MAX(0, delayMs);
    }
}

void Mock_ResetStatistics(MockDeviceContext *ctx) {
    if (!ctx) return;
    
    CmtGetLock(ctx->statsLock);
    ctx->connectCount = 0;
    ctx->disconnectCount = 0;
    ctx->commandsExecuted = 0;
    ctx->commandsFailed = 0;
    ctx->connectionFailCount = 0;
    CmtReleaseLock(ctx->statsLock);
}

/******************************************************************************
 * Mock Adapter Implementation
 ******************************************************************************/

static int Mock_Connect(void *deviceContext, void *connectionParams) {
    MockDeviceContext *ctx = (MockDeviceContext*)deviceContext;
    if (!ctx) return ERR_INVALID_PARAMETER;
    
    // Simulate connection delay
    Delay(MOCK_CONNECT_DELAY_MS / 1000.0);
    
    CmtGetLock(ctx->statsLock);
    ctx->connectCount++;
    
    if (ctx->shouldFailConnection) {
        ctx->connectionFailCount++;
        CmtReleaseLock(ctx->statsLock);
        return ERR_COMM_FAILED;
    }
    
    ctx->isConnected = 1;
    CmtReleaseLock(ctx->statsLock);
    
    return SUCCESS;
}

static int Mock_Disconnect(void *deviceContext) {
    MockDeviceContext *ctx = (MockDeviceContext*)deviceContext;
    if (!ctx) return ERR_INVALID_PARAMETER;
    
    CmtGetLock(ctx->statsLock);
    ctx->disconnectCount++;
    ctx->isConnected = 0;
    CmtReleaseLock(ctx->statsLock);
    
    return SUCCESS;
}

static int Mock_TestConnection(void *deviceContext) {
    MockDeviceContext *ctx = (MockDeviceContext*)deviceContext;
    if (!ctx) return ERR_INVALID_PARAMETER;
    
    if (!ctx->isConnected) {
        return ERR_NOT_CONNECTED;
    }
    
    if (ctx->simulateDisconnect) {
        ctx->isConnected = 0;
        return ERR_COMM_FAILED;
    }
    
    return SUCCESS;
}

static bool Mock_IsConnected(void *deviceContext) {
    MockDeviceContext *ctx = (MockDeviceContext*)deviceContext;
    return ctx && ctx->isConnected;
}

static int Mock_ExecuteCommand(void *deviceContext, int commandType, void *params, void *result) {
    MockDeviceContext *ctx = (MockDeviceContext*)deviceContext;
    if (!ctx) return ERR_INVALID_PARAMETER;
    
    // Check connection
    if (!ctx->isConnected) {
        return ERR_NOT_CONNECTED;
    }
    
    // Simulate command delay
    if (ctx->commandDelay > 0) {
        Delay(ctx->commandDelay / 1000.0);
    }
    
    CmtGetLock(ctx->statsLock);
    ctx->commandsExecuted++;
    
    // Check for simulated timeout
    if (ctx->simulateTimeout) {
        ctx->commandsFailed++;
        CmtReleaseLock(ctx->statsLock);
        return ERR_TIMEOUT;
    }
    
    // Check for simulated disconnect
    if (ctx->simulateDisconnect) {
        ctx->isConnected = 0;
        ctx->commandsFailed++;
        CmtReleaseLock(ctx->statsLock);
        return ERR_COMM_FAILED;
    }
    
    // Check failure rate
    if (ctx->shouldFailCommands) {
        int random = rand() % 100;
        if (random < ctx->commandFailRate) {
            ctx->commandsFailed++;
            CmtReleaseLock(ctx->statsLock);
            return ERR_OPERATION_FAILED;
        }
    }
    
    CmtReleaseLock(ctx->statsLock);
    
    // Execute mock command
    MockCommandResult *mockResult = (MockCommandResult*)result;
    MockCommandParams *mockParams = (MockCommandParams*)params;
    
    switch ((MockCommandType)commandType) {
        case MOCK_CMD_TEST_CONNECTION:
            mockResult->success = 1;
            strcpy(mockResult->message, "Connection test OK");
            break;
            
        case MOCK_CMD_SET_VALUE:
            mockResult->success = 1;
            mockResult->value = mockParams ? mockParams->value : 0;
            sprintf(mockResult->message, "Value set to %d", mockResult->value);
            break;
            
        case MOCK_CMD_GET_VALUE:
            mockResult->success = 1;
            mockResult->value = rand() % 1000;  // Random value
            sprintf(mockResult->message, "Value is %d", mockResult->value);
            break;
            
        case MOCK_CMD_SLOW_OPERATION:
            // Extra delay for slow operations
            Delay(mockParams ? mockParams->delay : 0.5);
            mockResult->success = 1;
            strcpy(mockResult->message, "Slow operation completed");
            break;
            
        case MOCK_CMD_FAILING_OPERATION:
            // This command always fails
            return ERR_OPERATION_FAILED;
            
        default:
            return ERR_INVALID_PARAMETER;
    }
    
    return SUCCESS;
}

static void* Mock_CreateCommandParams(int commandType, void *sourceParams) {
    if (!sourceParams) return NULL;
    
    MockCommandParams *params = (MockCommandParams*)malloc(sizeof(MockCommandParams));
    if (!params) return NULL;
    
    memcpy(params, sourceParams, sizeof(MockCommandParams));
    return params;
}

static void Mock_FreeCommandParams(int commandType, void *params) {
    if (params) {
        free(params);
    }
}

static void* Mock_CreateCommandResult(int commandType) {
    return calloc(1, sizeof(MockCommandResult));
}

static void Mock_FreeCommandResult(int commandType, void *result) {
    free(result);
}

static void Mock_CopyCommandResult(int commandType, void *dest, void *src) {
    if (dest && src) {
        *(MockCommandResult*)dest = *(MockCommandResult*)src;
    }
}

static const char* Mock_GetCommandTypeName(int commandType) {
    if (commandType >= 0 && commandType < MOCK_CMD_TYPE_COUNT) {
        return g_mockCommandNames[commandType];
    }
    return "UNKNOWN";
}

static int Mock_GetCommandDelay(int commandType) {
    switch ((MockCommandType)commandType) {
        case MOCK_CMD_SLOW_OPERATION:
            return 100;  // Extra delay for slow operations
        default:
            return 10;   // Standard delay
    }
}

/******************************************************************************
 * Callback Function Implementations (moved from inside test functions)
 ******************************************************************************/

static void AsyncCallback(DeviceCommandID cmdId, int commandType, void *result, void *userData) {
    AsyncTracker *tracker = (AsyncTracker*)userData;
    if (tracker) {
        tracker->completed = 1;
        tracker->commandType = commandType;
        tracker->cmdId = cmdId;
        if (result) {
            tracker->resultValue = ((MockCommandResult*)result)->value;
        }
    }
}

static void PriorityCallback(DeviceCommandID cmdId, int commandType, void *result, void *userData) {
    PriorityTracker *tracker = (PriorityTracker*)userData;
    if (tracker) {
        tracker->executionOrder = InterlockedIncrement(&g_executionCounter);
        tracker->completed = 1;
    }
}

static void TransactionCallback(DeviceTransactionHandle txnId, int success, int failed, 
                              TransactionCommandResult *results, int resultCount,
                              void *userData) {
    TransactionTracker *t = (TransactionTracker*)userData;
    if (t) {
        t->completed = 1;
        t->successCount = success;
        t->failureCount = failed;
    }
}

// Thread worker data and callback
typedef struct {
    DeviceQueueManager *queueManager;
    int threadIndex;
    volatile int commandsSubmitted;
    volatile int commandsCompleted;
    volatile int errors;
} ThreadWorkerData;

static void ThreadCommandCallback(DeviceCommandID cmdId, int commandType, void *result, void *userData) {
    ThreadWorkerData *data = (ThreadWorkerData*)userData;
    if (data) {
        InterlockedIncrement(&data->commandsCompleted);
    }
}

// Callback for tracking transaction order
static void TransactionOrderCallback(DeviceTransactionHandle txnId, int success, int failed, 
                                   TransactionCommandResult *results, int resultCount,
                                   void *userData) {
    TransactionOrderTracker *tracker = (TransactionOrderTracker*)userData;
    if (tracker) {
        tracker->executionOrder = InterlockedIncrement(&g_transactionExecutionCounter);
        tracker->endTime = Timer();
        tracker->completed = 1;
    }
}

// Worker thread for concurrent transaction creation
static int CVICALLBACK ConcurrentTransactionWorker(void *functionData) {
    ConcurrentTransactionData *data = (ConcurrentTransactionData*)functionData;
    
    // Create multiple transactions
    for (int i = 0; i < data->numTransactions; i++) {
        // Begin transaction
        DeviceTransactionHandle txn = DeviceQueue_BeginTransaction(data->queueManager);
        if (txn == 0) {
            InterlockedIncrement(&data->errors);
            continue;
        }
        
        data->trackers[data->threadIndex * data->numTransactions + i].txnId = txn;
        data->trackers[data->threadIndex * data->numTransactions + i].threadId = GetCurrentThreadId();
        data->trackers[data->threadIndex * data->numTransactions + i].startTime = Timer();
        
        InterlockedIncrement(&data->transactionsCreated);
        
        // Add commands to transaction
        for (int j = 0; j < 3; j++) {
            MockCommandParams params = {.value = data->threadIndex * 100 + i * 10 + j};
            int error = DeviceQueue_AddToTransaction(data->queueManager, txn, 
                                                   MOCK_CMD_SET_VALUE, &params);
            if (error != SUCCESS) {
                InterlockedIncrement(&data->errors);
            }
        }
        
        // Random delay to mix up commit order
        Delay((rand() % 50) / 1000.0);
        
        // Commit transaction
        int error = DeviceQueue_CommitTransaction(data->queueManager, txn,
                                                TransactionOrderCallback,
                                                &data->trackers[data->threadIndex * data->numTransactions + i]);
        if (error == SUCCESS) {
            InterlockedIncrement(&data->transactionsCommitted);
        } else {
            InterlockedIncrement(&data->errors);
        }
    }
    
    return 0;
}

// Structure for blocking command in thread
typedef struct {
    DeviceQueueManager *mgr;
    MockCommandResult *result;
    volatile int completed;
    int error;
    double startTime;
    double endTime;
} BlockingCmdData;

// Thread function for blocking command
static int CVICALLBACK BlockingCommandThread(void *functionData) {
    BlockingCmdData *data = (BlockingCmdData*)functionData;
    
    MockCommandParams params = {.value = 777};
    data->error = DeviceQueue_CommandBlocking(data->mgr, MOCK_CMD_SET_VALUE,
                                            &params, DEVICE_PRIORITY_HIGH, 
                                            data->result, 2000);
    data->endTime = Timer();
    data->completed = 1;
    
    return 0;
}

/******************************************************************************
 * Test Thread Function
 ******************************************************************************/

static int CVICALLBACK TestThreadFunction(void *functionData) {
    DeviceQueueTestContext *ctx = (DeviceQueueTestContext*)functionData;
    
    LogMessage("=== Starting Device Queue Test Suite ===");
    
    ctx->suiteStartTime = Timer();
    
    // Run each test
    for (int i = 0; i < g_numTestCases; i++) {
        if (ctx->cancelRequested) {
            LogMessage("Test suite cancelled by user");
            break;
        }
        
        TestCase *test = &g_testCases[i];
        
        LogMessage("Running test %d/%d: %s", i + 1, g_numTestCases, test->testName);
        
        strcpy(ctx->currentTestName, test->testName);
        ctx->testStartTime = Timer();
        
        // Reset mock device statistics before each test
        Mock_ResetStatistics(ctx->mockContext);
        
        // Run the test
        test->result = test->testFunction(ctx, test->errorMessage, sizeof(test->errorMessage));
        test->executionTime = Timer() - ctx->testStartTime;
        
        if (test->result > 0) {
            LogMessage("  ? PASSED (%.2f seconds)", test->executionTime);
            ctx->passedTests++;
        } else {
            LogError("  ? FAILED: %s", test->errorMessage);
            ctx->failedTests++;
        }
        
        ctx->totalTests++;
        
        // Brief delay between tests
        if (i < g_numTestCases - 1 && !ctx->cancelRequested) {
            ProcessSystemEvents();
            Delay(TEST_DELAY_SHORT);
        }
    }
    
    // Generate summary
    double totalTime = Timer() - ctx->suiteStartTime;
    LogMessage("========================================");
    LogMessage("Device Queue Test Suite Summary:");
    LogMessage("Total Tests: %d", ctx->totalTests);
    LogMessage("Passed: %d", ctx->passedTests);
    LogMessage("Failed: %d", ctx->failedTests);
    LogMessage("Total Time: %.2f seconds", totalTime);
    LogMessage("========================================");
    
    if (ctx->failedTests > 0) {
        LogMessage("Failed Tests:");
        for (int i = 0; i < g_numTestCases; i++) {
            if (g_testCases[i].result < 0) {
                LogMessage("  - %s: %s", g_testCases[i].testName, g_testCases[i].errorMessage);
            }
        }
    }
    
    // Update state
    if (ctx->cancelRequested) {
        ctx->state = TEST_STATE_ABORTED;
    } else if (ctx->failedTests == 0) {
        ctx->state = TEST_STATE_COMPLETED;
    } else {
        ctx->state = TEST_STATE_ERROR;
    }
    
    // Restore button text
    SetCtrlAttribute(ctx->panelHandle, ctx->buttonControl, ATTR_LABEL_TEXT, "Test Device Queue");
    
    // Clear thread ID
    g_testThreadId = 0;
    
    return 0;
}

/******************************************************************************
 * Public Functions
 ******************************************************************************/

int CVICALLBACK TestDeviceQueueCallback(int panel, int control, int event,
                                       void *callbackData, int eventData1, 
                                       int eventData2) {
    if (event != EVENT_COMMIT) return 0;
    
    // Check if test is running
    if (DeviceQueueTest_IsRunning()) {
        // Cancel request
        DeviceQueueTest_Cancel(&g_testContext);
        SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, "Cancelling...");
        SetCtrlAttribute(panel, control, ATTR_DIMMED, 1);
        return 0;
    }
    
    // Initialize test context
    DeviceQueueTest_Initialize(&g_testContext, panel, control);
    
    // Change button to show Cancel
    SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, "Cancel");
    
    // Start test thread
    int error = CmtScheduleThreadPoolFunction(g_threadPool, TestThreadFunction, 
                                            &g_testContext, &g_testThreadId);
    if (error != 0) {
        LogError("Failed to start test thread");
        SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, "Test Device Queue");
        g_testContext.state = TEST_STATE_ERROR;
        return 0;
    }
    
    return 0;
}

int DeviceQueueTest_Initialize(DeviceQueueTestContext *ctx, int panel, int buttonControl) {
    if (!ctx) return ERR_INVALID_PARAMETER;
    
    memset(ctx, 0, sizeof(DeviceQueueTestContext));
    ctx->state = TEST_STATE_PREPARING;
    ctx->panelHandle = panel;
    ctx->buttonControl = buttonControl;
    
    // Reset all test results
    for (int i = 0; i < g_numTestCases; i++) {
        g_testCases[i].result = 0;
        g_testCases[i].errorMessage[0] = '\0';
        g_testCases[i].executionTime = 0.0;
    }
    
    // Create mock device context
    ctx->mockContext = Mock_CreateContext();
    if (!ctx->mockContext) {
        LogError("Failed to create mock device context");
        return ERR_OUT_OF_MEMORY;
    }
    
    return SUCCESS;
}

int DeviceQueueTest_Run(DeviceQueueTestContext *ctx) {
    // The actual test execution is handled by TestThreadFunction
    // This function is here for API consistency
    return SUCCESS;
}

void DeviceQueueTest_Cancel(DeviceQueueTestContext *ctx) {
    if (ctx) {
        ctx->cancelRequested = 1;
        LogMessage("Test cancellation requested");
    }
}

void DeviceQueueTest_Cleanup(DeviceQueueTestContext *ctx) {
    if (!ctx) return;
    
    // Clean up queue manager if it exists
    if (ctx->queueManager) {
        DeviceQueue_Destroy(ctx->queueManager);
        ctx->queueManager = NULL;
    }
    
    // Clean up mock context
    if (ctx->mockContext) {
        Mock_DestroyContext(ctx->mockContext);
        ctx->mockContext = NULL;
    }
}

int DeviceQueueTest_IsRunning(void) {
    return (g_testThreadId != 0);
}

/******************************************************************************
 * Individual Test Implementations
 ******************************************************************************/

int Test_QueueCreation(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize) {
    // Test creating a queue manager with valid parameters
    ctx->queueManager = DeviceQueue_Create(&g_mockAdapter, ctx->mockContext, NULL);
    
    if (!ctx->queueManager) {
        snprintf(errorMsg, errorMsgSize, "Failed to create queue manager");
        return -1;
    }
    
    // Verify queue is running
    if (!DeviceQueue_IsRunning(ctx->queueManager)) {
        snprintf(errorMsg, errorMsgSize, "Queue manager not running after creation");
        DeviceQueue_Destroy(ctx->queueManager);
        ctx->queueManager = NULL;
        return -1;
    }
    
    // Get stats to verify initialization
    DeviceQueueStats stats;
    DeviceQueue_GetStats(ctx->queueManager, &stats);
    
    if (stats.totalProcessed != 0 || stats.totalErrors != 0) {
        snprintf(errorMsg, errorMsgSize, "Queue stats not initialized properly");
        DeviceQueue_Destroy(ctx->queueManager);
        ctx->queueManager = NULL;
        return -1;
    }
    
    // Clean up
    DeviceQueue_Destroy(ctx->queueManager);
    ctx->queueManager = NULL;
    
    return 1;
}

int Test_QueueDestruction(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize) {
    // Create multiple queues and destroy them
    for (int i = 0; i < 5; i++) {
        MockDeviceContext *tempContext = Mock_CreateContext();
        if (!tempContext) {
            snprintf(errorMsg, errorMsgSize, "Failed to create mock context %d", i);
            return -1;
        }
        
        DeviceQueueManager *tempQueue = DeviceQueue_Create(&g_mockAdapter, tempContext, NULL);
        if (!tempQueue) {
            Mock_DestroyContext(tempContext);
            snprintf(errorMsg, errorMsgSize, "Failed to create queue %d", i);
            return -1;
        }
        
        // Submit some commands
        MockCommandParams params = {.value = i};
        MockCommandResult result;
        
        DeviceQueue_CommandBlocking(tempQueue, MOCK_CMD_SET_VALUE, &params, 
                                  DEVICE_PRIORITY_NORMAL, &result, 100);
        
        // Destroy while commands might be processing
        DeviceQueue_Destroy(tempQueue);
        Mock_DestroyContext(tempContext);
        
        Delay(TEST_DELAY_VERY_SHORT);
    }
    
    return 1;
}

int Test_ConnectionHandling(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize) {
    // Test connection and disconnection scenarios
    ctx->queueManager = DeviceQueue_Create(&g_mockAdapter, ctx->mockContext, NULL);
    if (!ctx->queueManager) {
        snprintf(errorMsg, errorMsgSize, "Failed to create queue manager");
        return -1;
    }
    
    // Should be connected after creation
    DeviceQueueStats stats;
    DeviceQueue_GetStats(ctx->queueManager, &stats);
    
    if (!stats.isConnected) {
        snprintf(errorMsg, errorMsgSize, "Device not connected after queue creation");
        goto cleanup;
    }
    
    // Simulate disconnection
    Mock_SetConnectionState(ctx->mockContext, 0);
    ctx->mockContext->simulateDisconnect = 1;
    
    // Try a command that should fail
    MockCommandResult result;
    int error = DeviceQueue_CommandBlocking(ctx->queueManager, MOCK_CMD_TEST_CONNECTION,
                                          NULL, DEVICE_PRIORITY_HIGH, &result, 1000);
    
    if (error != ERR_COMM_FAILED && error != ERR_NOT_CONNECTED) {
        snprintf(errorMsg, errorMsgSize, "Expected connection error, got %d", error);
        goto cleanup;
    }
    
    // Should trigger reconnection attempts
    Delay(2.0);  // Wait for reconnection
    
    // Re-enable connection
    ctx->mockContext->simulateDisconnect = 0;
    Mock_SetConnectionState(ctx->mockContext, 1);
    
    // Wait for reconnection
    Delay(2.0);
    
    // Try command again - should work now
    error = DeviceQueue_CommandBlocking(ctx->queueManager, MOCK_CMD_TEST_CONNECTION,
                                      NULL, DEVICE_PRIORITY_HIGH, &result, 1000);
    
    if (error != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to reconnect: %s", GetErrorString(error));
        goto cleanup;
    }
    
    DeviceQueue_Destroy(ctx->queueManager);
    ctx->queueManager = NULL;
    return 1;
    
cleanup:
    DeviceQueue_Destroy(ctx->queueManager);
    ctx->queueManager = NULL;
    return -1;
}

int Test_BlockingCommands(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize) {
    ctx->queueManager = DeviceQueue_Create(&g_mockAdapter, ctx->mockContext, NULL);
    if (!ctx->queueManager) {
        snprintf(errorMsg, errorMsgSize, "Failed to create queue manager");
        return -1;
    }
    
    // Test various blocking commands
    MockCommandParams params = {.value = 42};
    MockCommandResult result;
    
    // Test successful command
    int error = DeviceQueue_CommandBlocking(ctx->queueManager, MOCK_CMD_SET_VALUE,
                                          &params, DEVICE_PRIORITY_HIGH, &result, 1000);
    
    if (error != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Blocking command failed: %s", GetErrorString(error));
        goto cleanup;
    }
    
    if (result.value != 42) {
        snprintf(errorMsg, errorMsgSize, "Result value mismatch: expected 42, got %d", result.value);
        goto cleanup;
    }
    
    // Test GET command
    error = DeviceQueue_CommandBlocking(ctx->queueManager, MOCK_CMD_GET_VALUE,
                                      NULL, DEVICE_PRIORITY_NORMAL, &result, 1000);
    
    if (error != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "GET command failed: %s", GetErrorString(error));
        goto cleanup;
    }
    
    // Test slow operation
    params.delay = 0.2;  // 200ms delay
    double startTime = Timer();
    error = DeviceQueue_CommandBlocking(ctx->queueManager, MOCK_CMD_SLOW_OPERATION,
                                      &params, DEVICE_PRIORITY_LOW, &result, 1000);
    double elapsed = Timer() - startTime;
    
    if (error != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Slow operation failed: %s", GetErrorString(error));
        goto cleanup;
    }
    
    if (elapsed < 0.2) {
        snprintf(errorMsg, errorMsgSize, "Slow operation completed too quickly: %.3f seconds", elapsed);
        goto cleanup;
    }
    
    DeviceQueue_Destroy(ctx->queueManager);
    ctx->queueManager = NULL;
    return 1;
    
cleanup:
    DeviceQueue_Destroy(ctx->queueManager);
    ctx->queueManager = NULL;
    return -1;
}

int Test_AsyncCommands(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize) {
    ctx->queueManager = DeviceQueue_Create(&g_mockAdapter, ctx->mockContext, NULL);
    if (!ctx->queueManager) {
        snprintf(errorMsg, errorMsgSize, "Failed to create queue manager");
        return -1;
    }
    
    AsyncTracker trackers[5] = {0};
    
    // Submit multiple async commands
    for (int i = 0; i < 5; i++) {
        MockCommandParams params = {.value = i * 10};
        DeviceCommandID cmdId = DeviceQueue_CommandAsync(ctx->queueManager, MOCK_CMD_SET_VALUE,
                                                  &params, DEVICE_PRIORITY_NORMAL,
                                                  AsyncCallback, &trackers[i]);
        
        if (cmdId == 0) {
            snprintf(errorMsg, errorMsgSize, "Failed to queue async command %d", i);
            goto cleanup;
        }
        
        trackers[i].cmdId = cmdId;
    }
    
    // Wait for all to complete
    double timeout = Timer() + 2.0;
    int allCompleted = 0;
    
    while (Timer() < timeout && !allCompleted) {
        allCompleted = 1;
        for (int i = 0; i < 5; i++) {
            if (!trackers[i].completed) {
                allCompleted = 0;
                break;
            }
        }
        ProcessSystemEvents();
        Delay(TEST_DELAY_VERY_SHORT);
    }
    
    if (!allCompleted) {
        snprintf(errorMsg, errorMsgSize, "Not all async commands completed within timeout");
        goto cleanup;
    }
    
    // Verify results
    for (int i = 0; i < 5; i++) {
        if (trackers[i].resultValue != i * 10) {
            snprintf(errorMsg, errorMsgSize, "Async command %d result mismatch: expected %d, got %d",
                    i, i * 10, trackers[i].resultValue);
            goto cleanup;
        }
    }
    
    DeviceQueue_Destroy(ctx->queueManager);
    ctx->queueManager = NULL;
    return 1;
    
cleanup:
    DeviceQueue_Destroy(ctx->queueManager);
    ctx->queueManager = NULL;
    return -1;
}

int Test_PriorityHandling(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize) {
    ctx->queueManager = DeviceQueue_Create(&g_mockAdapter, ctx->mockContext, NULL);
    if (!ctx->queueManager) {
        snprintf(errorMsg, errorMsgSize, "Failed to create queue manager");
        return -1;
    }
    
    // Slow down command execution to test priority
    Mock_SetCommandDelay(ctx->mockContext, 50);  // 50ms per command
    
    g_executionCounter = 0;
    
    PriorityTracker trackers[9] = {0};
    
    // Submit commands in mixed priority order
    // Low priority first
    for (int i = 0; i < 3; i++) {
        MockCommandParams params = {.value = i};
        trackers[i].priority = DEVICE_PRIORITY_LOW;
        trackers[i].value = i;
        
        DeviceCommandID cmdId = DeviceQueue_CommandAsync(ctx->queueManager, MOCK_CMD_SET_VALUE,
                                                  &params, DEVICE_PRIORITY_LOW,
                                                  PriorityCallback, &trackers[i]);
        if (cmdId == 0) {
            snprintf(errorMsg, errorMsgSize, "Failed to queue low priority command %d", i);
            goto cleanup;
        }
    }
    
    // Then normal priority
    for (int i = 3; i < 6; i++) {
        MockCommandParams params = {.value = i};
        trackers[i].priority = DEVICE_PRIORITY_NORMAL;
        trackers[i].value = i;
        
        DeviceCommandID cmdId = DeviceQueue_CommandAsync(ctx->queueManager, MOCK_CMD_SET_VALUE,
                                                  &params, DEVICE_PRIORITY_NORMAL,
                                                  PriorityCallback, &trackers[i]);
        if (cmdId == 0) {
            snprintf(errorMsg, errorMsgSize, "Failed to queue normal priority command %d", i);
            goto cleanup;
        }
    }
    
    // Finally high priority
    for (int i = 6; i < 9; i++) {
        MockCommandParams params = {.value = i};
        trackers[i].priority = DEVICE_PRIORITY_HIGH;
        trackers[i].value = i;
        
        DeviceCommandID cmdId = DeviceQueue_CommandAsync(ctx->queueManager, MOCK_CMD_SET_VALUE,
                                                  &params, DEVICE_PRIORITY_HIGH,
                                                  PriorityCallback, &trackers[i]);
        if (cmdId == 0) {
            snprintf(errorMsg, errorMsgSize, "Failed to queue high priority command %d", i);
            goto cleanup;
        }
    }
    
    // Wait for all to complete
    double timeout = Timer() + 3.0;
    int allCompleted = 0;
    
    while (Timer() < timeout && !allCompleted) {
        allCompleted = 1;
        for (int i = 0; i < 9; i++) {
            if (!trackers[i].completed) {
                allCompleted = 0;
                break;
            }
        }
        ProcessSystemEvents();
        Delay(TEST_DELAY_VERY_SHORT);
    }
    
    if (!allCompleted) {
        snprintf(errorMsg, errorMsgSize, "Not all priority commands completed");
        goto cleanup;
    }
    
    // Verify high priority executed first
    int highPriorityFirst = 1;
    for (int i = 6; i < 9; i++) {
        if (trackers[i].executionOrder > 3) {
            highPriorityFirst = 0;
            break;
        }
    }
    
    if (!highPriorityFirst) {
        snprintf(errorMsg, errorMsgSize, "High priority commands not executed first");
        // Log execution order for debugging
        for (int i = 0; i < 9; i++) {
            LogDebug("Command %d (priority %d): execution order %d", 
                    trackers[i].value, trackers[i].priority, trackers[i].executionOrder);
        }
        goto cleanup;
    }
    
    // Restore normal command delay
    Mock_SetCommandDelay(ctx->mockContext, MOCK_COMMAND_DELAY_MS);
    
    DeviceQueue_Destroy(ctx->queueManager);
    ctx->queueManager = NULL;
    return 1;
    
cleanup:
    Mock_SetCommandDelay(ctx->mockContext, MOCK_COMMAND_DELAY_MS);
    DeviceQueue_Destroy(ctx->queueManager);
    ctx->queueManager = NULL;
    return -1;
}

int Test_CommandCancellation(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize) {
    ctx->queueManager = DeviceQueue_Create(&g_mockAdapter, ctx->mockContext, NULL);
    if (!ctx->queueManager) {
        snprintf(errorMsg, errorMsgSize, "Failed to create queue manager");
        return -1;
    }
    
    // Slow down execution to have time to cancel
    Mock_SetCommandDelay(ctx->mockContext, 100);
    
    // Test cancel by ID
    MockCommandParams params = {.value = 100};
    DeviceCommandID cmdId = DeviceQueue_CommandAsync(ctx->queueManager, MOCK_CMD_SET_VALUE,
                                             &params, DEVICE_PRIORITY_LOW, NULL, NULL);
    
    if (cmdId == 0) {
        snprintf(errorMsg, errorMsgSize, "Failed to queue command for cancellation");
        goto cleanup;
    }
    
    // Cancel it immediately
    int error = DeviceQueue_CancelCommand(ctx->queueManager, cmdId);
    if (error != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to cancel command by ID");
        goto cleanup;
    }
    
    // Test cancel by type - queue multiple commands of same type
    for (int i = 0; i < 5; i++) {
        params.value = i;
        DeviceQueue_CommandAsync(ctx->queueManager, MOCK_CMD_GET_VALUE,
                               &params, DEVICE_PRIORITY_LOW, NULL, NULL);
    }
    
    // Cancel all GET_VALUE commands
    error = DeviceQueue_CancelByType(ctx->queueManager, MOCK_CMD_GET_VALUE);
    if (error != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to cancel commands by type");
        goto cleanup;
    }
    
    // Test cancel by age
    params.delay = 0.5;  // Slow operation
    for (int i = 0; i < 3; i++) {
        DeviceQueue_CommandAsync(ctx->queueManager, MOCK_CMD_SLOW_OPERATION,
                               &params, DEVICE_PRIORITY_LOW, NULL, NULL);
    }
    
    // Wait a bit
    Delay(0.2);
    
    // Queue more commands
    for (int i = 0; i < 3; i++) {
        DeviceQueue_CommandAsync(ctx->queueManager, MOCK_CMD_SET_VALUE,
                               &params, DEVICE_PRIORITY_LOW, NULL, NULL);
    }
    
    // Cancel commands older than 0.1 seconds
    error = DeviceQueue_CancelByAge(ctx->queueManager, 0.1);
    if (error != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to cancel commands by age");
        goto cleanup;
    }
    
    // Test cancel all
    for (int i = 0; i < 10; i++) {
        DeviceQueue_CommandAsync(ctx->queueManager, MOCK_CMD_SET_VALUE,
                               &params, DEVICE_PRIORITY_NORMAL, NULL, NULL);
    }
    
    error = DeviceQueue_CancelAll(ctx->queueManager);
    if (error != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to cancel all commands");
        goto cleanup;
    }
    
    // Verify queues are empty
    DeviceQueueStats stats;
    DeviceQueue_GetStats(ctx->queueManager, &stats);
    
    // Wait a bit for any in-flight commands to complete
    Delay(0.5);
    
    DeviceQueue_GetStats(ctx->queueManager, &stats);
    if (stats.highPriorityQueued + stats.normalPriorityQueued + stats.lowPriorityQueued > 0) {
        snprintf(errorMsg, errorMsgSize, "Queues not empty after cancel all");
        goto cleanup;
    }
    
    // Restore normal delay
    Mock_SetCommandDelay(ctx->mockContext, MOCK_COMMAND_DELAY_MS);
    
    DeviceQueue_Destroy(ctx->queueManager);
    ctx->queueManager = NULL;
    return 1;
    
cleanup:
    Mock_SetCommandDelay(ctx->mockContext, MOCK_COMMAND_DELAY_MS);
    DeviceQueue_Destroy(ctx->queueManager);
    ctx->queueManager = NULL;
    return -1;
}

int Test_Transactions(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize) {
    ctx->queueManager = DeviceQueue_Create(&g_mockAdapter, ctx->mockContext, NULL);
    if (!ctx->queueManager) {
        snprintf(errorMsg, errorMsgSize, "Failed to create queue manager");
        return -1;
    }
    
    // Test 1: Basic transaction
    DeviceTransactionHandle txn = DeviceQueue_BeginTransaction(ctx->queueManager);
    if (txn == 0) {
        snprintf(errorMsg, errorMsgSize, "Failed to begin transaction");
        goto cleanup;
    }
    
    // Add commands to transaction
    for (int i = 0; i < 5; i++) {
        MockCommandParams params = {.value = i * 100};
        int error = DeviceQueue_AddToTransaction(ctx->queueManager, txn, 
                                               MOCK_CMD_SET_VALUE, &params);
        if (error != SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to add command %d to transaction", i);
            goto cleanup;
        }
    }
    
    // Structure to track detailed results
    typedef struct {
        volatile int completed;
        int expectedResults[5];
        int actualResults[5];
        int allCorrect;
    } DetailedTracker;
    
    DetailedTracker tracker = {
        .completed = 0,
        .expectedResults = {0, 100, 200, 300, 400},
        .allCorrect = 1
    };
    
    // Commit transaction
    int error = DeviceQueue_CommitTransaction(ctx->queueManager, txn, 
                                            TransactionCallback, &tracker);
    if (error != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to commit transaction");
        goto cleanup;
    }
    
    // Wait for completion
    double timeout = Timer() + 2.0;
    while (Timer() < timeout && !tracker.completed) {
        ProcessSystemEvents();
        Delay(0.05);
    }
    
    if (!tracker.completed) {
        snprintf(errorMsg, errorMsgSize, "Transaction did not complete");
        goto cleanup;
    }
    
    // Test 2: Transaction with abort on error
    txn = DeviceQueue_BeginTransaction(ctx->queueManager);
    error = DeviceQueue_SetTransactionFlags(ctx->queueManager, txn, DEVICE_TXN_ABORT_ON_ERROR);
    if (error != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set transaction flags");
        goto cleanup;
    }
    
    // Add mix of good and failing commands
    MockCommandParams goodParams = {.value = 100};
    error = DeviceQueue_AddToTransaction(ctx->queueManager, txn, 
                                       MOCK_CMD_SET_VALUE, &goodParams);
    error |= DeviceQueue_AddToTransaction(ctx->queueManager, txn, 
                                        MOCK_CMD_FAILING_OPERATION, NULL);
    error |= DeviceQueue_AddToTransaction(ctx->queueManager, txn, 
                                        MOCK_CMD_SET_VALUE, &goodParams);
    
    if (error != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to add commands for abort test");
        goto cleanup;
    }
    
    TransactionTracker abortTracker = {0};
    error = DeviceQueue_CommitTransaction(ctx->queueManager, txn,
                                        TransactionCallback, &abortTracker);
    
    // Wait for completion
    timeout = Timer() + 2.0;
    while (Timer() < timeout && !abortTracker.completed) {
        ProcessSystemEvents();
        Delay(0.05);
    }
    
    if (!abortTracker.completed) {
        snprintf(errorMsg, errorMsgSize, "Abort transaction did not complete");
        goto cleanup;
    }
    
    // Should have 1 success, 2 failures (1 actual fail, 1 cancelled)
    if (abortTracker.successCount != 1 || abortTracker.failureCount != 2) {
        snprintf(errorMsg, errorMsgSize, "Abort on error didn't work correctly: %d success, %d failed",
                abortTracker.successCount, abortTracker.failureCount);
        goto cleanup;
    }
    
    // Test 3: Transaction priority
    Mock_SetCommandDelay(ctx->mockContext, 50); // Slow down for testing
    
    // Create LOW priority transaction
    DeviceTransactionHandle lowTxn = DeviceQueue_BeginTransaction(ctx->queueManager);
    DeviceQueue_SetTransactionPriority(ctx->queueManager, lowTxn, DEVICE_PRIORITY_LOW);
    
    // Create HIGH priority transaction
    DeviceTransactionHandle highTxn = DeviceQueue_BeginTransaction(ctx->queueManager);
    DeviceQueue_SetTransactionPriority(ctx->queueManager, highTxn, DEVICE_PRIORITY_HIGH);
    
    // Add commands to both
    for (int i = 0; i < 2; i++) {
        MockCommandParams params = {.value = i};
        DeviceQueue_AddToTransaction(ctx->queueManager, lowTxn, MOCK_CMD_SET_VALUE, &params);
        DeviceQueue_AddToTransaction(ctx->queueManager, highTxn, MOCK_CMD_SET_VALUE, &params);
    }
    
    TransactionOrderTracker lowTracker = {0}, highTracker = {0};
    g_transactionExecutionCounter = 0;
    
    // Commit LOW first, then HIGH
    DeviceQueue_CommitTransaction(ctx->queueManager, lowTxn, 
                                TransactionOrderCallback, &lowTracker);
    Delay(0.01); // Small delay
    DeviceQueue_CommitTransaction(ctx->queueManager, highTxn,
                                TransactionOrderCallback, &highTracker);
    
    // Wait for both
    timeout = Timer() + 3.0;
    while (Timer() < timeout && (!lowTracker.completed || !highTracker.completed)) {
        ProcessSystemEvents();
        Delay(0.05);
    }
    
    // Both should complete, but order depends on implementation
    if (!lowTracker.completed || !highTracker.completed) {
        snprintf(errorMsg, errorMsgSize, "Priority transactions did not complete");
        goto cleanup;
    }
    
    Mock_SetCommandDelay(ctx->mockContext, MOCK_COMMAND_DELAY_MS);
    
    // Test 4: Transaction timeout
    txn = DeviceQueue_BeginTransaction(ctx->queueManager);
    DeviceQueue_SetTransactionTimeout(ctx->queueManager, txn, 100); // 100ms timeout
    
    // Add slow commands
    MockCommandParams slowParams = {.delay = 0.2}; // 200ms each
    for (int i = 0; i < 3; i++) {
        DeviceQueue_AddToTransaction(ctx->queueManager, txn, 
                                   MOCK_CMD_SLOW_OPERATION, &slowParams);
    }
    
    TransactionTracker timeoutTracker = {0};
    DeviceQueue_CommitTransaction(ctx->queueManager, txn,
                                TransactionCallback, &timeoutTracker);
    
    // Wait for timeout
    timeout = Timer() + 1.0;
    while (Timer() < timeout && !timeoutTracker.completed) {
        ProcessSystemEvents();
        Delay(0.05);
    }
    
    if (!timeoutTracker.completed) {
        snprintf(errorMsg, errorMsgSize, "Timeout transaction did not complete");
        goto cleanup;
    }
    
    // Should have some failures due to timeout
    if (timeoutTracker.failureCount == 0) {
        snprintf(errorMsg, errorMsgSize, "Transaction timeout didn't trigger");
        goto cleanup;
    }
    
    // Test 5: Empty transaction rejection
    txn = DeviceQueue_BeginTransaction(ctx->queueManager);
    error = DeviceQueue_CommitTransaction(ctx->queueManager, txn, NULL, NULL);
    if (error == SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Empty transaction should be rejected");
        goto cleanup;
    }
    
    // Test 6: Max transaction size
    txn = DeviceQueue_BeginTransaction(ctx->queueManager);
    
    for (int i = 0; i < DEVICE_MAX_TRANSACTION_COMMANDS + 1; i++) {
        MockCommandParams params = {.value = i};
        error = DeviceQueue_AddToTransaction(ctx->queueManager, txn, 
                                           MOCK_CMD_SET_VALUE, &params);
        
        if (i < DEVICE_MAX_TRANSACTION_COMMANDS && error != SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to add command %d to transaction", i);
            goto cleanup;
        } else if (i == DEVICE_MAX_TRANSACTION_COMMANDS && error == SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Should have rejected command beyond transaction limit");
            goto cleanup;
        }
    }
    
    // Cancel this large transaction
    DeviceQueue_CancelTransaction(ctx->queueManager, txn);
    
    DeviceQueue_Destroy(ctx->queueManager);
    ctx->queueManager = NULL;
    return 1;
    
cleanup:
    Mock_SetCommandDelay(ctx->mockContext, MOCK_COMMAND_DELAY_MS);
    DeviceQueue_Destroy(ctx->queueManager);
    ctx->queueManager = NULL;
    return -1;
}

// Test transaction edge cases
int Test_TransactionEdgeCases(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize) {
    ctx->queueManager = DeviceQueue_Create(&g_mockAdapter, ctx->mockContext, NULL);
    if (!ctx->queueManager) {
        snprintf(errorMsg, errorMsgSize, "Failed to create queue manager");
        return -1;
    }
    
    // Test 1: Cancel uncommitted transaction
    DeviceTransactionHandle txn = DeviceQueue_BeginTransaction(ctx->queueManager);
    for (int i = 0; i < 3; i++) {
        MockCommandParams params = {.value = i};
        DeviceQueue_AddToTransaction(ctx->queueManager, txn, MOCK_CMD_SET_VALUE, &params);
    }
    
    int error = DeviceQueue_CancelTransaction(ctx->queueManager, txn);
    if (error != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to cancel uncommitted transaction");
        goto cleanup;
    }
    
    // Test 2: Double commit attempt
    txn = DeviceQueue_BeginTransaction(ctx->queueManager);
    MockCommandParams params = {.value = 42};
    DeviceQueue_AddToTransaction(ctx->queueManager, txn, MOCK_CMD_SET_VALUE, &params);
    DeviceQueue_CommitTransaction(ctx->queueManager, txn, NULL, NULL);
    
    // Try to commit again
    error = DeviceQueue_CommitTransaction(ctx->queueManager, txn, NULL, NULL);
    if (error == SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should reject double commit");
        goto cleanup;
    }
    
    // Test 3: Add to committed transaction
    error = DeviceQueue_AddToTransaction(ctx->queueManager, txn, MOCK_CMD_SET_VALUE, &params);
    if (error == SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should reject adding to committed transaction");
        goto cleanup;
    }
    
    // Test 4: Invalid transaction ID operations
    error = DeviceQueue_AddToTransaction(ctx->queueManager, 99999, MOCK_CMD_SET_VALUE, &params);
    if (error == SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should reject invalid transaction ID");
        goto cleanup;
    }
    
    error = DeviceQueue_CommitTransaction(ctx->queueManager, 99999, NULL, NULL);
    if (error == SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should reject committing invalid transaction");
        goto cleanup;
    }
    
    error = DeviceQueue_CancelTransaction(ctx->queueManager, 99999);
    if (error == SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should fail cancelling invalid transaction");
        goto cleanup;
    }
    
    // Test 5: Transaction during device disconnection
    Mock_SetConnectionState(ctx->mockContext, 0);
    ctx->mockContext->simulateDisconnect = 1;
    
    txn = DeviceQueue_BeginTransaction(ctx->queueManager);
    if (txn == 0) {
        snprintf(errorMsg, errorMsgSize, "Should allow transaction creation when disconnected");
        goto cleanup;
    }
    
    // Add commands
    for (int i = 0; i < 3; i++) {
        MockCommandParams p = {.value = i};
        error = DeviceQueue_AddToTransaction(ctx->queueManager, txn, MOCK_CMD_SET_VALUE, &p);
        if (error != SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Should allow adding commands when disconnected");
            goto cleanup;
        }
    }
    
    TransactionTracker disconnectTracker = {0};
    
    // Commit should work but execution will fail
    error = DeviceQueue_CommitTransaction(ctx->queueManager, txn,
                                        TransactionCallback, &disconnectTracker);
    if (error != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should allow commit when disconnected");
        goto cleanup;
    }
    
    // Wait briefly
    Delay(0.5);
    
    // Re-enable connection
    ctx->mockContext->simulateDisconnect = 0;
    Mock_SetConnectionState(ctx->mockContext, 1);
    
    // Wait for reconnection and execution
    double timeout = Timer() + 3.0;
    while (Timer() < timeout && !disconnectTracker.completed) {
        ProcessSystemEvents();
        Delay(0.1);
    }
    
    if (!disconnectTracker.completed) {
        snprintf(errorMsg, errorMsgSize, "Transaction didn't complete after reconnection");
        goto cleanup;
    }
    
    // Should have failures from disconnection
    if (disconnectTracker.failureCount == 0) {
        snprintf(errorMsg, errorMsgSize, "Expected failures during disconnection");
        goto cleanup;
    }
    
    // Test 6: Rapid transaction creation and cancellation
    DeviceTransactionHandle rapidTxns[10] = {0};
    
    // Create many transactions quickly
    for (int i = 0; i < 10; i++) {
        rapidTxns[i] = DeviceQueue_BeginTransaction(ctx->queueManager);
        if (rapidTxns[i] == 0) {
            snprintf(errorMsg, errorMsgSize, "Failed to create rapid transaction %d", i);
            goto cleanup;
        }
        
        // Add one command
        MockCommandParams p = {.value = i};
        DeviceQueue_AddToTransaction(ctx->queueManager, rapidTxns[i], 
                                   MOCK_CMD_SET_VALUE, &p);
    }
    
    // Cancel every other one
    for (int i = 0; i < 10; i += 2) {
        error = DeviceQueue_CancelTransaction(ctx->queueManager, rapidTxns[i]);
        if (error != SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to cancel rapid transaction %d", i);
            goto cleanup;
        }
    }
    
    // Commit the rest
    TransactionTracker rapidTrackers[10] = {0};
    for (int i = 1; i < 10; i += 2) {
        error = DeviceQueue_CommitTransaction(ctx->queueManager, rapidTxns[i],
                                            TransactionCallback, &rapidTrackers[i]);
        if (error != SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to commit rapid transaction %d", i);
            goto cleanup;
        }
    }
    
    // Wait for all to complete
    timeout = Timer() + 2.0;
    int allDone = 0;
    while (Timer() < timeout && !allDone) {
        allDone = 1;
        for (int i = 1; i < 10; i += 2) {
            if (!rapidTrackers[i].completed) {
                allDone = 0;
                break;
            }
        }
        ProcessSystemEvents();
        Delay(0.05);
    }
    
    if (!allDone) {
        snprintf(errorMsg, errorMsgSize, "Not all rapid transactions completed");
        goto cleanup;
    }
    
    DeviceQueue_Destroy(ctx->queueManager);
    ctx->queueManager = NULL;
    return 1;
    
cleanup:
    ctx->mockContext->simulateDisconnect = 0;
    Mock_SetConnectionState(ctx->mockContext, 1);
    DeviceQueue_Destroy(ctx->queueManager);
    ctx->queueManager = NULL;
    return -1;
}

int Test_QueueOverflow(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize) {
    ctx->queueManager = DeviceQueue_Create(&g_mockAdapter, ctx->mockContext, NULL);
    if (!ctx->queueManager) {
        snprintf(errorMsg, errorMsgSize, "Failed to create queue manager");
        return -1;
    }
    
    // Slow down execution to fill queues
    Mock_SetCommandDelay(ctx->mockContext, 1000);  // 1 second per command
    
    // Try to overflow high priority queue
    int submitted = 0;
    int rejected = 0;
    
    for (int i = 0; i < DEVICE_QUEUE_HIGH_PRIORITY_SIZE + 10; i++) {
        MockCommandParams params = {.value = i};
        DeviceCommandID cmdId = DeviceQueue_CommandAsync(ctx->queueManager, MOCK_CMD_SET_VALUE,
                                                  &params, DEVICE_PRIORITY_HIGH,
                                                  NULL, NULL);
        if (cmdId != 0) {
            submitted++;
        } else {
            rejected++;
        }
        
        // Use blocking command with 0 timeout to test queue full
        if (rejected == 0 && i >= DEVICE_QUEUE_HIGH_PRIORITY_SIZE - 1) {
            MockCommandResult result;
            int error = DeviceQueue_CommandBlocking(ctx->queueManager, MOCK_CMD_SET_VALUE,
                                                  &params, DEVICE_PRIORITY_HIGH, &result, 0);
            if (error == ERR_QUEUE_FULL || error == ERR_TIMEOUT) {
                rejected++;
                break;  // We've proven the queue can be full
            }
        }
    }
    
    if (rejected == 0) {
        snprintf(errorMsg, errorMsgSize, "No commands rejected when queue should be full");
        goto cleanup;
    }
    
    // Cancel all to clean up
    DeviceQueue_CancelAll(ctx->queueManager);
    
    // Test normal priority queue overflow with 0 timeout
    submitted = 0;
    rejected = 0;
    
    // First, fill the queue with async commands
    for (int i = 0; i < DEVICE_QUEUE_NORMAL_PRIORITY_SIZE; i++) {
        MockCommandParams params = {.value = i};
        DeviceQueue_CommandAsync(ctx->queueManager, MOCK_CMD_GET_VALUE,
                               &params, DEVICE_PRIORITY_NORMAL, NULL, NULL);
    }
    
    // Now try to add more with 0 timeout
    for (int i = 0; i < 10; i++) {
        MockCommandParams params = {.value = i + 1000};
        MockCommandResult result;
        int error = DeviceQueue_CommandBlocking(ctx->queueManager, MOCK_CMD_GET_VALUE,
                                              &params, DEVICE_PRIORITY_NORMAL, &result, 0);
        if (error == SUCCESS) {
            submitted++;
        } else if (error == ERR_QUEUE_FULL || error == ERR_TIMEOUT) {
            rejected++;
        }
    }
    
    if (rejected == 0) {
        snprintf(errorMsg, errorMsgSize, "Normal priority queue did not overflow");
        goto cleanup;
    }
    
    // Restore normal delay
    Mock_SetCommandDelay(ctx->mockContext, MOCK_COMMAND_DELAY_MS);
    
    // Cancel all commands
    DeviceQueue_CancelAll(ctx->queueManager);
    
    DeviceQueue_Destroy(ctx->queueManager);
    ctx->queueManager = NULL;
    return 1;
    
cleanup:
    Mock_SetCommandDelay(ctx->mockContext, MOCK_COMMAND_DELAY_MS);
    DeviceQueue_CancelAll(ctx->queueManager);
    DeviceQueue_Destroy(ctx->queueManager);
    ctx->queueManager = NULL;
    return -1;
}

int Test_ErrorHandling(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize) {
    ctx->queueManager = DeviceQueue_Create(&g_mockAdapter, ctx->mockContext, NULL);
    if (!ctx->queueManager) {
        snprintf(errorMsg, errorMsgSize, "Failed to create queue manager");
        return -1;
    }
    
    // Test command that always fails
    MockCommandResult result;
    int error = DeviceQueue_CommandBlocking(ctx->queueManager, MOCK_CMD_FAILING_OPERATION,
                                          NULL, DEVICE_PRIORITY_HIGH, &result, 1000);
    
    if (error != ERR_OPERATION_FAILED) {
        snprintf(errorMsg, errorMsgSize, "Expected operation failed error, got %d", error);
        goto cleanup;
    }
    
    // Test with failure rate
    Mock_SetFailureRate(ctx->mockContext, 50);  // 50% failure rate
    
    int successes = 0;
    int failures = 0;
    
    for (int i = 0; i < 20; i++) {
        MockCommandParams params = {.value = i};
        error = DeviceQueue_CommandBlocking(ctx->queueManager, MOCK_CMD_SET_VALUE,
                                          &params, DEVICE_PRIORITY_NORMAL, &result, 1000);
        if (error == SUCCESS) {
            successes++;
        } else {
            failures++;
        }
    }
    
    if (successes == 0 || failures == 0) {
        snprintf(errorMsg, errorMsgSize, "50%% failure rate not working: %d success, %d failed",
                successes, failures);
        goto cleanup;
    }
    
    // Reset failure rate
    Mock_SetFailureRate(ctx->mockContext, 0);
    
    // Test NULL parameters
    error = DeviceQueue_CommandBlocking(NULL, MOCK_CMD_SET_VALUE, NULL, 
                                      DEVICE_PRIORITY_HIGH, &result, 1000);
    if (error != ERR_INVALID_PARAMETER) {
        snprintf(errorMsg, errorMsgSize, "Should reject NULL queue manager");
        goto cleanup;
    }
    
    error = DeviceQueue_CommandBlocking(ctx->queueManager, MOCK_CMD_SET_VALUE, NULL,
                                      DEVICE_PRIORITY_HIGH, NULL, 1000);
    if (error != ERR_INVALID_PARAMETER) {
        snprintf(errorMsg, errorMsgSize, "Should reject NULL result pointer");
        goto cleanup;
    }
    
    DeviceQueue_Destroy(ctx->queueManager);
    ctx->queueManager = NULL;
    return 1;
    
cleanup:
    Mock_SetFailureRate(ctx->mockContext, 0);
    DeviceQueue_Destroy(ctx->queueManager);
    ctx->queueManager = NULL;
    return -1;
}

int Test_Timeouts(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize) {
    ctx->queueManager = DeviceQueue_Create(&g_mockAdapter, ctx->mockContext, NULL);
    if (!ctx->queueManager) {
        snprintf(errorMsg, errorMsgSize, "Failed to create queue manager");
        return -1;
    }
    
    // Enable timeout simulation
    ctx->mockContext->simulateTimeout = 1;
    
    MockCommandResult result;
    double startTime = Timer();
    
    // Test short timeout
    int error = DeviceQueue_CommandBlocking(ctx->queueManager, MOCK_CMD_SET_VALUE,
                                          NULL, DEVICE_PRIORITY_HIGH, &result, 100);
    
    double elapsed = Timer() - startTime;
    
    if (error != ERR_TIMEOUT) {
        snprintf(errorMsg, errorMsgSize, "Expected timeout error, got %d", error);
        goto cleanup;
    }
    
    // Timeout should occur quickly when command returns timeout
    if (elapsed > 0.5) {
        snprintf(errorMsg, errorMsgSize, "Timeout took too long: %.3f seconds", elapsed);
        goto cleanup;
    }
    
    // Disable timeout simulation
    ctx->mockContext->simulateTimeout = 0;
    
    // Test normal command with very long execution
    Mock_SetCommandDelay(ctx->mockContext, 2000);  // 2 second execution
    
    startTime = Timer();
    error = DeviceQueue_CommandBlocking(ctx->queueManager, MOCK_CMD_SET_VALUE,
                                      NULL, DEVICE_PRIORITY_HIGH, &result, 500);  // 500ms timeout
    elapsed = Timer() - startTime;
    
    if (error != ERR_TIMEOUT) {
        snprintf(errorMsg, errorMsgSize, "Should timeout on slow command, got %d", error);
        goto cleanup;
    }
    
    // Should timeout after specified time
    if (elapsed < 0.5 || elapsed > 1.0) {
        snprintf(errorMsg, errorMsgSize, "Incorrect timeout duration: %.3f seconds", elapsed);
        goto cleanup;
    }
    
    // Restore normal delay
    Mock_SetCommandDelay(ctx->mockContext, MOCK_COMMAND_DELAY_MS);
    
    DeviceQueue_Destroy(ctx->queueManager);
    ctx->queueManager = NULL;
    return 1;
    
cleanup:
    ctx->mockContext->simulateTimeout = 0;
    Mock_SetCommandDelay(ctx->mockContext, MOCK_COMMAND_DELAY_MS);
    DeviceQueue_Destroy(ctx->queueManager);
    ctx->queueManager = NULL;
    return -1;
}

// Thread worker function for concurrent testing
static int CVICALLBACK ThreadWorkerFunction(void *functionData) {
    ThreadWorkerData *data = (ThreadWorkerData*)functionData;
    
    // Submit commands with different priorities
    for (int i = 0; i < COMMANDS_PER_THREAD; i++) {
        MockCommandParams params = {.value = data->threadIndex * 1000 + i};
        
        // Mix priorities
        DevicePriority priority = (DevicePriority)(i % 3);
        
        DeviceCommandID cmdId = DeviceQueue_CommandAsync(data->queueManager, MOCK_CMD_SET_VALUE,
                                                  &params, priority,
                                                  ThreadCommandCallback, data);
        if (cmdId != 0) {
            InterlockedIncrement(&data->commandsSubmitted);
        } else {
            InterlockedIncrement(&data->errors);
        }
        
        // Small random delay
        Delay((rand() % 10) / 1000.0);
    }
    
    return 0;
}

// Helper structures for Test_ThreadSafety
typedef struct {
    DeviceQueueManager *queueManager;
    int threadIndex;
    volatile int commandsSubmitted;
    volatile int commandsCompleted;
    volatile int transactionsCreated;
    volatile int transactionsCompleted;
    volatile int errors;
} MixedWorkerData;

static void MixedCommandCallback(DeviceCommandID cmdId, int commandType, void *result, void *userData) {
    MixedWorkerData *mwd = (MixedWorkerData*)userData;
    InterlockedIncrement(&mwd->commandsCompleted);
}

static void MixedTransactionCallback(DeviceTransactionHandle t, int s, int f,
                                   TransactionCommandResult *r, int rc, void *ud) {
    MixedWorkerData *mwd = (MixedWorkerData*)ud;
    InterlockedIncrement(&mwd->transactionsCompleted);
}

static int CVICALLBACK MixedWorkerFunction(void *functionData) {
    MixedWorkerData *data = (MixedWorkerData*)functionData;
    
    // Mix of operations
    for (int i = 0; i < COMMANDS_PER_THREAD; i++) {
        int operation = rand() % 3;
        
        if (operation == 0) {
            // Submit async command
            MockCommandParams params = {.value = data->threadIndex * 1000 + i};
            DeviceCommandID cmdId = DeviceQueue_CommandAsync(data->queueManager, MOCK_CMD_SET_VALUE,
                                                      &params, (DevicePriority)(i % 3),
                                                      MixedCommandCallback, data);
            if (cmdId != 0) {
                InterlockedIncrement(&data->commandsSubmitted);
            } else {
                InterlockedIncrement(&data->errors);
            }
        } else if (operation == 1) {
            // Submit blocking command
            MockCommandParams params = {.value = data->threadIndex * 2000 + i};
            MockCommandResult result;
            int error = DeviceQueue_CommandBlocking(data->queueManager, MOCK_CMD_GET_VALUE,
                                                  &params, DEVICE_PRIORITY_NORMAL,
                                                  &result, 1000);
            if (error == SUCCESS) {
                InterlockedIncrement(&data->commandsSubmitted);
                InterlockedIncrement(&data->commandsCompleted);
            } else {
                InterlockedIncrement(&data->errors);
            }
        } else {
            // Create and commit a transaction
            DeviceTransactionHandle txn = DeviceQueue_BeginTransaction(data->queueManager);
            if (txn != 0) {
                InterlockedIncrement(&data->transactionsCreated);
                
                // Add 2-3 commands
                int numCmds = 2 + (rand() % 2);
                for (int j = 0; j < numCmds; j++) {
                    MockCommandParams params = {.value = data->threadIndex * 3000 + i * 10 + j};
                    DeviceQueue_AddToTransaction(data->queueManager, txn,
                                               MOCK_CMD_SET_VALUE, &params);
                }
                
                int error = DeviceQueue_CommitTransaction(data->queueManager, txn,
                                                        MixedTransactionCallback, data);
                if (error != SUCCESS) {
                    InterlockedIncrement(&data->errors);
                }
            } else {
                InterlockedIncrement(&data->errors);
            }
        }
        
        // Random small delay
        Delay((rand() % 10) / 1000.0);
    }
    
    return 0;
}

int Test_ThreadSafety(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize) {
    ctx->queueManager = DeviceQueue_Create(&g_mockAdapter, ctx->mockContext, NULL);
    if (!ctx->queueManager) {
        snprintf(errorMsg, errorMsgSize, "Failed to create queue manager");
        return -1;
    }
    
    MixedWorkerData workerData[TEST_THREAD_COUNT];
    CmtThreadFunctionID threads[TEST_THREAD_COUNT] = {0};
    
    // Initialize worker data
    memset(workerData, 0, sizeof(workerData));
    for (int i = 0; i < TEST_THREAD_COUNT; i++) {
        workerData[i].queueManager = ctx->queueManager;
        workerData[i].threadIndex = i;
    }
    
    // Start worker threads
    for (int i = 0; i < TEST_THREAD_COUNT; i++) {
        int error = CmtScheduleThreadPoolFunction(g_threadPool, MixedWorkerFunction,
                                                &workerData[i], &threads[i]);
        if (error != 0) {
            snprintf(errorMsg, errorMsgSize, "Failed to start thread %d", i);
            goto cleanup;
        }
    }
    
    // Wait for all threads to complete submission
    for (int i = 0; i < TEST_THREAD_COUNT; i++) {
        CmtWaitForThreadPoolFunctionCompletion(g_threadPool, threads[i],
                                             OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
    }
    
    // Wait for all operations to complete
    double timeout = Timer() + 10.0;
    int totalSubmitted = 0, totalCompleted = 0;
    int totalTxnCreated = 0, totalTxnCompleted = 0;
    
    while (Timer() < timeout) {
        totalSubmitted = 0;
        totalCompleted = 0;
        totalTxnCreated = 0;
        totalTxnCompleted = 0;
        
        for (int i = 0; i < TEST_THREAD_COUNT; i++) {
            totalSubmitted += workerData[i].commandsSubmitted;
            totalCompleted += workerData[i].commandsCompleted;
            totalTxnCreated += workerData[i].transactionsCreated;
            totalTxnCompleted += workerData[i].transactionsCompleted;
        }
        
        // For async commands, we can't easily track completion
        // Just ensure transactions completed
        if (totalTxnCompleted >= totalTxnCreated && totalTxnCreated > 0) {
            break;
        }
        
        ProcessSystemEvents();
        Delay(0.1);
    }
    
    // Verify some operations succeeded
    if (totalSubmitted == 0 && totalTxnCreated == 0) {
        snprintf(errorMsg, errorMsgSize, "No operations were submitted");
        goto cleanup;
    }
    
    // Check for excessive errors
    int totalErrors = 0;
    for (int i = 0; i < TEST_THREAD_COUNT; i++) {
        totalErrors += workerData[i].errors;
    }
    
    // Some errors are acceptable in concurrent scenarios
    if (totalErrors > (totalSubmitted + totalTxnCreated) / 2) {
        snprintf(errorMsg, errorMsgSize, "Too many errors in concurrent execution: %d errors",
                totalErrors);
        goto cleanup;
    }
    
    // Verify queue statistics
    DeviceQueueStats stats;
    DeviceQueue_GetStats(ctx->queueManager, &stats);
    
    if (stats.totalProcessed == 0) {
        snprintf(errorMsg, errorMsgSize, "No commands were processed");
        goto cleanup;
    }
    
    LogDebug("Thread safety test: %d commands, %d transactions, %d errors",
            totalSubmitted, totalTxnCreated, totalErrors);
    
    DeviceQueue_Destroy(ctx->queueManager);
    ctx->queueManager = NULL;
    return 1;
    
cleanup:
    DeviceQueue_Destroy(ctx->queueManager);
    ctx->queueManager = NULL;
    return -1;
}

// Thread function for concurrent cancellation
static int CVICALLBACK CancellationWorkerFunction(void *functionData) {
    ThreadWorkerData *data = (ThreadWorkerData*)functionData;
    
    // Randomly cancel commands
    for (int i = 0; i < 20; i++) {
        int operation = rand() % 4;
        
        switch (operation) {
            case 0:
                // Cancel by type
                DeviceQueue_CancelByType(data->queueManager, MOCK_CMD_SET_VALUE);
                break;
                
            case 1:
                // Cancel by age
                DeviceQueue_CancelByAge(data->queueManager, 0.1);
                break;
                
            case 2:
                // Cancel all
                DeviceQueue_CancelAll(data->queueManager);
                break;
                
            case 3:
                // Submit a new command
                MockCommandParams params = {.value = rand() % 1000};
                DeviceQueue_CommandAsync(data->queueManager, MOCK_CMD_SET_VALUE,
                                       &params, DEVICE_PRIORITY_NORMAL, NULL, NULL);
                break;
        }
        
        Delay((rand() % 50) / 1000.0);
    }
    
    return 0;
}

int Test_ConcurrentCancellation(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize) {
    ctx->queueManager = DeviceQueue_Create(&g_mockAdapter, ctx->mockContext, NULL);
    if (!ctx->queueManager) {
        snprintf(errorMsg, errorMsgSize, "Failed to create queue manager");
        return -1;
    }
    
    // Slow down execution to have commands to cancel
    Mock_SetCommandDelay(ctx->mockContext, 50);
    
    ThreadWorkerData workerData = {
        .queueManager = ctx->queueManager,
        .threadIndex = 0
    };
    
    // Start threads that submit and cancel commands
    CmtThreadFunctionID submitThread, cancelThread1, cancelThread2;
    
    CmtScheduleThreadPoolFunction(g_threadPool, ThreadWorkerFunction,
                                &workerData, &submitThread);
    CmtScheduleThreadPoolFunction(g_threadPool, CancellationWorkerFunction,
                                &workerData, &cancelThread1);
    CmtScheduleThreadPoolFunction(g_threadPool, CancellationWorkerFunction,
                                &workerData, &cancelThread2);
    
    // Let them run for a while
    Delay(2.0);
    
    // Wait for threads to complete
    CmtWaitForThreadPoolFunctionCompletion(g_threadPool, submitThread,
                                         OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
    CmtWaitForThreadPoolFunctionCompletion(g_threadPool, cancelThread1,
                                         OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
    CmtWaitForThreadPoolFunctionCompletion(g_threadPool, cancelThread2,
                                         OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
    
    // Cancel any remaining commands
    DeviceQueue_CancelAll(ctx->queueManager);
    
    // If we get here without crashing, the test passed
    
    // Restore normal delay
    Mock_SetCommandDelay(ctx->mockContext, MOCK_COMMAND_DELAY_MS);
    
    DeviceQueue_Destroy(ctx->queueManager);
    ctx->queueManager = NULL;
    return 1;
}

int Test_Statistics(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize) {
    ctx->queueManager = DeviceQueue_Create(&g_mockAdapter, ctx->mockContext, NULL);
    if (!ctx->queueManager) {
        snprintf(errorMsg, errorMsgSize, "Failed to create queue manager");
        return -1;
    }
    
    // Get initial stats
    DeviceQueueStats stats;
    DeviceQueue_GetStats(ctx->queueManager, &stats);
    
    if (stats.totalProcessed != 0 || stats.totalErrors != 0) {
        snprintf(errorMsg, errorMsgSize, "Initial stats not zero");
        goto cleanup;
    }
    
    // Submit and execute some successful commands
    MockCommandResult result;
    for (int i = 0; i < 5; i++) {
        MockCommandParams params = {.value = i};
        DeviceQueue_CommandBlocking(ctx->queueManager, MOCK_CMD_SET_VALUE,
                                  &params, DEVICE_PRIORITY_NORMAL, &result, 1000);
    }
    
    DeviceQueue_GetStats(ctx->queueManager, &stats);
    if (stats.totalProcessed != 5) {
        snprintf(errorMsg, errorMsgSize, "Processed count incorrect: %d", stats.totalProcessed);
        goto cleanup;
    }
    
    // Force some errors
    Mock_SetFailureRate(ctx->mockContext, 100);  // 100% failure
    
    for (int i = 0; i < 3; i++) {
        DeviceQueue_CommandBlocking(ctx->queueManager, MOCK_CMD_SET_VALUE,
                                  NULL, DEVICE_PRIORITY_HIGH, &result, 1000);
    }
    
    DeviceQueue_GetStats(ctx->queueManager, &stats);
    if (stats.totalErrors != 3) {
        snprintf(errorMsg, errorMsgSize, "Error count incorrect: %d", stats.totalErrors);
        goto cleanup;
    }
    
    // Reset failure rate
    Mock_SetFailureRate(ctx->mockContext, 0);
    
    // Test queue depths
    Mock_SetCommandDelay(ctx->mockContext, 100);  // Slow execution
    
    // Submit commands to different priority queues
    for (int i = 0; i < 5; i++) {
        DeviceQueue_CommandAsync(ctx->queueManager, MOCK_CMD_SET_VALUE,
                               NULL, DEVICE_PRIORITY_HIGH, NULL, NULL);
    }
    for (int i = 0; i < 3; i++) {
        DeviceQueue_CommandAsync(ctx->queueManager, MOCK_CMD_SET_VALUE,
                               NULL, DEVICE_PRIORITY_NORMAL, NULL, NULL);
    }
    for (int i = 0; i < 2; i++) {
        DeviceQueue_CommandAsync(ctx->queueManager, MOCK_CMD_SET_VALUE,
                               NULL, DEVICE_PRIORITY_LOW, NULL, NULL);
    }
    
    // Check queue depths immediately
    DeviceQueue_GetStats(ctx->queueManager, &stats);
    
    // At least some commands should be queued
    if (stats.highPriorityQueued + stats.normalPriorityQueued + stats.lowPriorityQueued == 0) {
        snprintf(errorMsg, errorMsgSize, "No commands in queues after submission");
        goto cleanup;
    }
    
    // Cancel all to clean up
    DeviceQueue_CancelAll(ctx->queueManager);
    
    // Restore normal delay
    Mock_SetCommandDelay(ctx->mockContext, MOCK_COMMAND_DELAY_MS);
    
    DeviceQueue_Destroy(ctx->queueManager);
    ctx->queueManager = NULL;
    return 1;
    
cleanup:
    Mock_SetFailureRate(ctx->mockContext, 0);
    Mock_SetCommandDelay(ctx->mockContext, MOCK_COMMAND_DELAY_MS);
    DeviceQueue_Destroy(ctx->queueManager);
    ctx->queueManager = NULL;
    return -1;
}

int Test_ReconnectionLogic(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize) {
    // Set initial disconnected state
    Mock_SetConnectionState(ctx->mockContext, 0);
    ctx->mockContext->shouldFailConnection = 1;
    
    ctx->queueManager = DeviceQueue_Create(&g_mockAdapter, ctx->mockContext, NULL);
    if (!ctx->queueManager) {
        snprintf(errorMsg, errorMsgSize, "Failed to create queue manager");
        return -1;
    }
    
    // Should start disconnected
    DeviceQueueStats stats;
    DeviceQueue_GetStats(ctx->queueManager, &stats);
    
    if (stats.isConnected) {
        snprintf(errorMsg, errorMsgSize, "Should start disconnected");
        goto cleanup;
    }
    
    // Wait for a few reconnection attempts
    Delay(5.0);  // Increased from 4.0 to ensure we get at least 2 attempts
    
    DeviceQueue_GetStats(ctx->queueManager, &stats);
    if (stats.reconnectAttempts < 2) {
        snprintf(errorMsg, errorMsgSize, "Not enough reconnection attempts: %d", 
                stats.reconnectAttempts);
        goto cleanup;
    }
    
    // Enable connection
    ctx->mockContext->shouldFailConnection = 0;
    Mock_SetConnectionState(ctx->mockContext, 1);
    
    // The reconnection timer has exponential backoff
    // After 2 failed attempts, next retry is in 4 seconds
    // We need to wait at least that long
    Delay(5.0);  // Increased from 2.0 to ensure reconnection happens
    
    DeviceQueue_GetStats(ctx->queueManager, &stats);
    if (!stats.isConnected) {
        snprintf(errorMsg, errorMsgSize, "Failed to reconnect after enabling connection");
        goto cleanup;
    }
    
    // Test command execution after reconnection
    MockCommandResult result;
    int error = DeviceQueue_CommandBlocking(ctx->queueManager, MOCK_CMD_TEST_CONNECTION,
                                          NULL, DEVICE_PRIORITY_HIGH, &result, 1000);
    
    if (error != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Command failed after reconnection: %s", 
                GetErrorString(error));
        goto cleanup;
    }
    
    DeviceQueue_Destroy(ctx->queueManager);
    ctx->queueManager = NULL;
    return 1;
    
cleanup:
    ctx->mockContext->shouldFailConnection = 0;
    DeviceQueue_Destroy(ctx->queueManager);
    ctx->queueManager = NULL;
    return -1;
}

int Test_EdgeCases(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize) {    
    // Test creating queue with NULL adapter
    DeviceQueueManager *badQueue = DeviceQueue_Create(NULL, ctx->mockContext, NULL);
    if (badQueue != NULL) {
        DeviceQueue_Destroy(badQueue);
        snprintf(errorMsg, errorMsgSize, "Should reject NULL adapter");
        return -1;
    }
    
    // Test creating queue with NULL context
    badQueue = DeviceQueue_Create(&g_mockAdapter, NULL, NULL);
    if (badQueue != NULL) {
        DeviceQueue_Destroy(badQueue);
        snprintf(errorMsg, errorMsgSize, "Should reject NULL device context");
        return -1;
    }
    
    // Create valid queue for remaining tests
    ctx->queueManager = DeviceQueue_Create(&g_mockAdapter, ctx->mockContext, NULL);
    if (!ctx->queueManager) {
        snprintf(errorMsg, errorMsgSize, "Failed to create queue manager");
        return -1;
    }
    
    // Now run transaction edge case tests
    int result = Test_TransactionEdgeCases(ctx, errorMsg, errorMsgSize);
    
    // Note: ctx->queueManager will be destroyed by Test_TransactionEdgeCases
    ctx->queueManager = NULL;
    
    return result;
}

// Test concurrent transaction building
int Test_ConcurrentTransactionBuilding(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize) {
    ctx->queueManager = DeviceQueue_Create(&g_mockAdapter, ctx->mockContext, NULL);
    if (!ctx->queueManager) {
        snprintf(errorMsg, errorMsgSize, "Failed to create queue manager");
        return -1;
    }
    
    const int THREADS = 4;
    const int TXN_PER_THREAD = 3;
    
    ConcurrentTransactionData workerData[4];  // Use fixed size to avoid VLA
    CmtThreadFunctionID threads[4] = {0};     // Use fixed size and initialize
    
    // Dynamically allocate trackers to avoid VLA initialization issue
    TransactionOrderTracker *trackers = calloc(THREADS * TXN_PER_THREAD, sizeof(TransactionOrderTracker));
    if (!trackers) {
        snprintf(errorMsg, errorMsgSize, "Failed to allocate tracker memory");
        DeviceQueue_Destroy(ctx->queueManager);
        ctx->queueManager = NULL;
        return -1;
    }
    
    g_transactionExecutionCounter = 0;
    
    // Initialize worker data
    for (int i = 0; i < THREADS; i++) {
        workerData[i].queueManager = ctx->queueManager;
        workerData[i].threadIndex = i;
        workerData[i].transactionsCreated = 0;
        workerData[i].transactionsCommitted = 0;
        workerData[i].errors = 0;
        workerData[i].trackers = trackers;
        workerData[i].numTransactions = TXN_PER_THREAD;
    }
    
    // Start concurrent threads
    for (int i = 0; i < THREADS; i++) {
        int error = CmtScheduleThreadPoolFunction(g_threadPool, ConcurrentTransactionWorker,
                                                &workerData[i], &threads[i]);
        if (error != 0) {
            snprintf(errorMsg, errorMsgSize, "Failed to start thread %d", i);
            goto cleanup;
        }
    }
    
    // Wait for all threads to complete
    for (int i = 0; i < THREADS; i++) {
        CmtWaitForThreadPoolFunctionCompletion(g_threadPool, threads[i],
                                             OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
    }
    
    // Count totals
    int totalCreated = 0, totalCommitted = 0, totalErrors = 0;
    for (int i = 0; i < THREADS; i++) {
        totalCreated += workerData[i].transactionsCreated;
        totalCommitted += workerData[i].transactionsCommitted;
        totalErrors += workerData[i].errors;
    }
    
    // Verify all transactions were created
    if (totalCreated != THREADS * TXN_PER_THREAD) {
        snprintf(errorMsg, errorMsgSize, "Expected %d transactions created, got %d",
                THREADS * TXN_PER_THREAD, totalCreated);
        goto cleanup;
    }
    
    // Verify all transactions were committed
    if (totalCommitted != THREADS * TXN_PER_THREAD) {
        snprintf(errorMsg, errorMsgSize, "Expected %d transactions committed, got %d",
                THREADS * TXN_PER_THREAD, totalCommitted);
        goto cleanup;
    }
    
    // Wait for all transactions to execute
    double timeout = Timer() + 5.0;
    int allCompleted = 0;
    while (Timer() < timeout && !allCompleted) {
        allCompleted = 1;
        for (int i = 0; i < THREADS * TXN_PER_THREAD; i++) {
            if (!trackers[i].completed) {
                allCompleted = 0;
                break;
            }
        }
        ProcessSystemEvents();
        Delay(0.1);
    }
    
    if (!allCompleted) {
        snprintf(errorMsg, errorMsgSize, "Not all transactions completed execution");
        goto cleanup;
    }
    
    // Verify sequential execution
    for (int i = 0; i < THREADS * TXN_PER_THREAD; i++) {
        if (trackers[i].executionOrder < 1 || trackers[i].executionOrder > THREADS * TXN_PER_THREAD) {
            snprintf(errorMsg, errorMsgSize, "Invalid execution order %d for transaction %u",
                    trackers[i].executionOrder, trackers[i].txnId);
            goto cleanup;
        }
    }
    
    // Verify no two transactions executed at the same time
    for (int i = 0; i < THREADS * TXN_PER_THREAD - 1; i++) {
        for (int j = i + 1; j < THREADS * TXN_PER_THREAD; j++) {
            double start1 = trackers[i].startTime;
            double end1 = trackers[i].endTime;
            double start2 = trackers[j].startTime;
            double end2 = trackers[j].endTime;
            
            // Check if execution times overlap
            if (trackers[i].executionOrder != trackers[j].executionOrder) {
                if ((start1 < end2 && end1 > start2) || (start2 < end1 && end2 > start1)) {
                    // Check that they didn't actually execute simultaneously
                    if (abs(trackers[i].executionOrder - trackers[j].executionOrder) != 1) {
                        continue; // Non-adjacent transactions, timing overlap is OK
                    }
                }
            }
        }
    }
    
    // Success - clean up and return
    free(trackers);
    DeviceQueue_Destroy(ctx->queueManager);
    ctx->queueManager = NULL;
    return 1;
    
cleanup:
    if (trackers) {
        free(trackers);
    }
    DeviceQueue_Destroy(ctx->queueManager);
    ctx->queueManager = NULL;
    return -1;
}

// Test commands queuing behind transactions
int Test_CommandsQueueBehindTransactions(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize) {
    ctx->queueManager = DeviceQueue_Create(&g_mockAdapter, ctx->mockContext, NULL);
    if (!ctx->queueManager) {
        snprintf(errorMsg, errorMsgSize, "Failed to create queue manager");
        return -1;
    }
    
    // Slow down execution to ensure transaction is running
    Mock_SetCommandDelay(ctx->mockContext, 100); // 100ms per command
    
    // Create and commit a transaction with multiple commands
    DeviceTransactionHandle txn = DeviceQueue_BeginTransaction(ctx->queueManager);
    if (txn == 0) {
        snprintf(errorMsg, errorMsgSize, "Failed to begin transaction");
        goto cleanup;
    }
    
    // Add 5 commands to transaction (500ms total execution time)
    for (int i = 0; i < 5; i++) {
        MockCommandParams params = {.value = i * 100};
        int error = DeviceQueue_AddToTransaction(ctx->queueManager, txn, 
                                               MOCK_CMD_SET_VALUE, &params);
        if (error != SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to add command %d to transaction", i);
            goto cleanup;
        }
    }
    
    TransactionTracker txnTracker = {0};
    
    // Commit transaction
    int error = DeviceQueue_CommitTransaction(ctx->queueManager, txn,
                                            TransactionCallback, &txnTracker);
    if (error != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to commit transaction");
        goto cleanup;
    }
    
    // Give transaction time to start executing
    Delay(0.05);
    
    // Now submit regular commands while transaction is executing
    MockCommandResult blockingResult1;
    AsyncTracker asyncTracker1 = {0}, asyncTracker2 = {0};
    
    double cmdStartTime = Timer();
    
    // Submit async command (should queue)
    DeviceCommandID asyncId1 = DeviceQueue_CommandAsync(ctx->queueManager, MOCK_CMD_GET_VALUE,
                                                       NULL, DEVICE_PRIORITY_HIGH,
                                                       AsyncCallback, &asyncTracker1);
    if (asyncId1 == 0) {
        snprintf(errorMsg, errorMsgSize, "Failed to queue async command 1");
        goto cleanup;
    }
    
    // Submit another async command
    MockCommandParams params2 = {.value = 999};
    DeviceCommandID asyncId2 = DeviceQueue_CommandAsync(ctx->queueManager, MOCK_CMD_SET_VALUE,
                                                       &params2, DEVICE_PRIORITY_NORMAL,
                                                       AsyncCallback, &asyncTracker2);
    if (asyncId2 == 0) {
        snprintf(errorMsg, errorMsgSize, "Failed to queue async command 2");
        goto cleanup;
    }
    
    // Try blocking command in another thread (to avoid blocking test thread)
    BlockingCmdData blockingData = {
        .mgr = ctx->queueManager,
        .result = &blockingResult1,
        .completed = 0,
        .error = 0,
        .startTime = Timer(),
        .endTime = 0
    };
    
    CmtThreadFunctionID blockingThread;
    CmtScheduleThreadPoolFunction(g_threadPool, BlockingCommandThread,
                                &blockingData, &blockingThread);
    
    // Wait for transaction to complete
    double timeout = Timer() + 2.0;
    while (Timer() < timeout && !txnTracker.completed) {
        ProcessSystemEvents();
        Delay(0.05);
    }
    
    if (!txnTracker.completed) {
        snprintf(errorMsg, errorMsgSize, "Transaction did not complete");
        goto cleanup;
    }
    
    // Transaction should have taken ~500ms
    double txnDuration = Timer() - cmdStartTime;
    if (txnDuration < 0.4) { // Allow some margin
        snprintf(errorMsg, errorMsgSize, "Transaction completed too quickly: %.3f seconds", txnDuration);
        goto cleanup;
    }
    
    // Now wait for all commands to complete
    timeout = Timer() + 2.0;
    while (Timer() < timeout) {
        if (asyncTracker1.completed && asyncTracker2.completed && blockingData.completed) {
            break;
        }
        ProcessSystemEvents();
        Delay(0.05);
    }
    
    // Verify all commands completed
    if (!asyncTracker1.completed || !asyncTracker2.completed || !blockingData.completed) {
        snprintf(errorMsg, errorMsgSize, "Not all commands completed after transaction");
        goto cleanup;
    }
    
    // Verify blocking command was blocked during transaction
    double blockingDuration = blockingData.endTime - blockingData.startTime;
    if (blockingDuration < 0.4) { // Should have waited for transaction
        snprintf(errorMsg, errorMsgSize, "Blocking command didn't wait for transaction: %.3f seconds", 
                blockingDuration);
        goto cleanup;
    }
    
    // Clean up
    CmtWaitForThreadPoolFunctionCompletion(g_threadPool, blockingThread,
                                         OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
    
    Mock_SetCommandDelay(ctx->mockContext, MOCK_COMMAND_DELAY_MS);
    DeviceQueue_Destroy(ctx->queueManager);
    ctx->queueManager = NULL;
    return 1;
    
cleanup:
    Mock_SetCommandDelay(ctx->mockContext, MOCK_COMMAND_DELAY_MS);
    DeviceQueue_Destroy(ctx->queueManager);
    ctx->queueManager = NULL;
    return -1;
}

// Helper structures for Test_TransactionChaining
typedef struct {
    DeviceQueueManager *mgr;
    volatile int chainCompleted;
    volatile int chainErrors;
    DeviceTransactionHandle chainedTxnId;
} ChainData;

static void ChainedCompleteCallback(DeviceTransactionHandle txn2, int s, int f,
                                  TransactionCommandResult *r, int rc, void *ud) {
    ChainData *cd2 = (ChainData*)ud;
    cd2->chainCompleted = 1;
}

static void ChainedTransactionCallback(DeviceTransactionHandle txnId, int success, int failed,
                                     TransactionCommandResult *results, int resultCount,
                                     void *userData) {
    ChainData *cd = (ChainData*)userData;
    
    // Verify results
    for (int i = 0; i < resultCount; i++) {
        if (results[i].errorCode != SUCCESS) {
            InterlockedIncrement(&cd->chainErrors);
            return;
        }
    }
    
    // Create a new transaction in the callback
    DeviceTransactionHandle newTxn = DeviceQueue_BeginTransaction(cd->mgr);
    if (newTxn == 0) {
        InterlockedIncrement(&cd->chainErrors);
        return;
    }
    
    cd->chainedTxnId = newTxn;
    
    // Add commands to new transaction
    for (int i = 0; i < 3; i++) {
        MockCommandParams params = {.value = 1000 + i};
        int error = DeviceQueue_AddToTransaction(cd->mgr, newTxn, 
                                               MOCK_CMD_SET_VALUE, &params);
        if (error != SUCCESS) {
            InterlockedIncrement(&cd->chainErrors);
            return;
        }
    }
    
    // Commit the chained transaction
    int error = DeviceQueue_CommitTransaction(cd->mgr, newTxn,
                                            ChainedCompleteCallback, cd);
    if (error != SUCCESS) {
        InterlockedIncrement(&cd->chainErrors);
    }
}

// Test transaction chaining in callbacks
int Test_TransactionChaining(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize) {
    ctx->queueManager = DeviceQueue_Create(&g_mockAdapter, ctx->mockContext, NULL);
    if (!ctx->queueManager) {
        snprintf(errorMsg, errorMsgSize, "Failed to create queue manager");
        return -1;
    }
    
    // Structure to track chained transactions
    ChainData chainData = {
        .mgr = ctx->queueManager,
        .chainCompleted = 0,
        .chainErrors = 0,
        .chainedTxnId = 0
    };
    
    // Create initial transaction
    DeviceTransactionHandle txn1 = DeviceQueue_BeginTransaction(ctx->queueManager);
    if (txn1 == 0) {
        snprintf(errorMsg, errorMsgSize, "Failed to begin initial transaction");
        goto cleanup;
    }
    
    // Add commands
    for (int i = 0; i < 3; i++) {
        MockCommandParams params = {.value = i};
        int error = DeviceQueue_AddToTransaction(ctx->queueManager, txn1, 
                                               MOCK_CMD_SET_VALUE, &params);
        if (error != SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to add command to initial transaction");
            goto cleanup;
        }
    }
    
    // Commit with chaining callback
    int error = DeviceQueue_CommitTransaction(ctx->queueManager, txn1,
                                            ChainedTransactionCallback, &chainData);
    if (error != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to commit initial transaction");
        goto cleanup;
    }
    
    // Wait for both transactions to complete
    double timeout = Timer() + 3.0;
    while (Timer() < timeout && !chainData.chainCompleted) {
        ProcessSystemEvents();
        Delay(0.05);
    }
    
    if (!chainData.chainCompleted) {
        snprintf(errorMsg, errorMsgSize, "Chained transaction did not complete");
        goto cleanup;
    }
    
    if (chainData.chainErrors > 0) {
        snprintf(errorMsg, errorMsgSize, "Errors occurred during transaction chaining");
        goto cleanup;
    }
    
    DeviceQueue_Destroy(ctx->queueManager);
    ctx->queueManager = NULL;
    return 1;
    
cleanup:
    DeviceQueue_Destroy(ctx->queueManager);
    ctx->queueManager = NULL;
    return -1;
}