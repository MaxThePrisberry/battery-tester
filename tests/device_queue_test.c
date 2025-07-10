/******************************************************************************
 * device_queue_test.c
 * 
 * Comprehensive test suite implementation for the generic device queue system
 * with proper cancellation support and queue manager tracking
 ******************************************************************************/

#include "device_queue_test.h"
#include "logging.h"
#include "BatteryTester.h"
#include <toolbox.h>
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

// Queue manager tracking functions
static void RegisterQueueManager(DeviceQueueTestContext *ctx, DeviceQueueManager *mgr);
static void UnregisterQueueManager(DeviceQueueTestContext *ctx, DeviceQueueManager *mgr);
static void CleanupAllQueueManagers(DeviceQueueTestContext *ctx);
static DeviceQueueManager* CreateTestQueueManager(DeviceQueueTestContext *ctx,
                                                 const DeviceAdapter *adapter,
                                                 void *deviceContext,
                                                 void *connectionParams);
static void DestroyTestQueueManager(DeviceQueueTestContext *ctx, DeviceQueueManager *mgr);

/******************************************************************************
 * Queue Manager Tracking Functions
 ******************************************************************************/

static void RegisterQueueManager(DeviceQueueTestContext *ctx, DeviceQueueManager *mgr) {
    if (!ctx || !mgr) return;
    
    CmtGetLock(ctx->queueListLock);
    ListInsertItem(ctx->activeQueueManagers, &mgr, END_OF_LIST);
    CmtReleaseLock(ctx->queueListLock);
    
    LogDebug("Registered queue manager %p (total: %d)", mgr, ListNumItems(ctx->activeQueueManagers));
}

static void UnregisterQueueManager(DeviceQueueTestContext *ctx, DeviceQueueManager *mgr) {
    if (!ctx || !mgr) return;
    
    CmtGetLock(ctx->queueListLock);
    int count = ListNumItems(ctx->activeQueueManagers);
    for (int i = 1; i <= count; i++) {
        DeviceQueueManager **qmPtr = ListGetPtrToItem(ctx->activeQueueManagers, i);
        if (qmPtr && *qmPtr == mgr) {
            ListRemoveItem(ctx->activeQueueManagers, 0, i);
            LogDebug("Unregistered queue manager %p (remaining: %d)", mgr, ListNumItems(ctx->activeQueueManagers));
            break;
        }
    }
    CmtReleaseLock(ctx->queueListLock);
}

static void CleanupAllQueueManagers(DeviceQueueTestContext *ctx) {
    if (!ctx) return;
    
    LogMessage("Cleaning up all test queue managers...");
    
    CmtGetLock(ctx->queueListLock);
    int count = ListNumItems(ctx->activeQueueManagers);
    LogMessage("Found %d active queue managers to clean up", count);
    
    // Create a copy of the list to avoid modification during iteration
    DeviceQueueManager **managers = NULL;
    if (count > 0) {
        managers = calloc(count, sizeof(DeviceQueueManager*));
        if (managers) {
            for (int i = 0; i < count; i++) {
                DeviceQueueManager **qmPtr = ListGetPtrToItem(ctx->activeQueueManagers, i + 1);
                if (qmPtr) {
                    managers[i] = *qmPtr;
                }
            }
        }
    }
    
    // Clear the list
    ListClear(ctx->activeQueueManagers);
    CmtReleaseLock(ctx->queueListLock);
    
    // Now destroy each queue manager outside the lock
    if (managers) {
        for (int i = 0; i < count; i++) {
            if (managers[i]) {
                LogMessage("Destroying queue manager %d/%d", i + 1, count);
                DeviceQueue_Destroy(managers[i]);
            }
        }
        free(managers);
    }
    
    LogMessage("All test queue managers cleaned up");
}

// Wrapper for creating tracked queue managers
static DeviceQueueManager* CreateTestQueueManager(DeviceQueueTestContext *ctx,
                                                 const DeviceAdapter *adapter,
                                                 void *deviceContext,
                                                 void *connectionParams) {
    if (!ctx) return NULL;
    
    // Use test thread pool if available, otherwise fall back to global
    CmtThreadPoolHandle poolToUse = ctx->testThreadPool ? ctx->testThreadPool : g_threadPool;
    
    DeviceQueueManager *mgr = DeviceQueue_Create(adapter, deviceContext, connectionParams, poolToUse);
    if (mgr) {
        RegisterQueueManager(ctx, mgr);
    }
    return mgr;
}

// Helper to destroy and unregister a queue manager
static void DestroyTestQueueManager(DeviceQueueTestContext *ctx, DeviceQueueManager *mgr) {
    if (!ctx || !mgr) return;
    
    UnregisterQueueManager(ctx, mgr);
    DeviceQueue_Destroy(mgr);
}

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
 * Static Function Prototypes
 ******************************************************************************/

static void UpdateTestProgress(DeviceQueueTestContext *context, const char *message);

/******************************************************************************
 * Test Thread Function
 ******************************************************************************/

static int CVICALLBACK TestThreadFunction(void *functionData) {
    DeviceQueueTestContext *ctx = (DeviceQueueTestContext*)functionData;
    
    LogMessage("=== Starting Device Queue Test Suite ===");
    UpdateTestProgress(ctx, "Starting Device Queue Test Suite...");
    
    ctx->suiteStartTime = Timer();
    
    // Run each test
    for (int i = 0; i < g_numTestCases; i++) {
        // Check for cancellation before each test
        if (ctx->cancelRequested) {
            LogMessage("Test suite cancelled by user at test %d/%d", i + 1, g_numTestCases);
            UpdateTestProgress(ctx, "Test suite cancelled");
            break;
        }
        
        TestCase *test = &g_testCases[i];
        
        // Update progress
        char progressMsg[256];
        snprintf(progressMsg, sizeof(progressMsg), "Running test %d/%d: %s", 
                i + 1, g_numTestCases, test->testName);
        UpdateTestProgress(ctx, progressMsg);
        
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
    
    if (ctx->cancelRequested) {
        ctx->state = TEST_STATE_ABORTED;
        UpdateTestProgress(ctx, "Test suite cancelled");
    } else if (ctx->failedTests == 0) {
        ctx->state = TEST_STATE_COMPLETED;
        UpdateTestProgress(ctx, "All tests passed!");
    } else {
        ctx->state = TEST_STATE_ERROR;
        UpdateTestProgress(ctx, "Some tests failed");
    }
    
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
    
    // Restore button text
    SetCtrlAttribute(ctx->panelHandle, ctx->buttonControl, ATTR_LABEL_TEXT, "Test Device Queue");
    SetCtrlAttribute(ctx->panelHandle, ctx->buttonControl, ATTR_DIMMED, 0);
    
    // Clear thread ID
    g_testThreadId = 0;
    
    return 0;
}

// Helper function to update test progress (similar to PSB test)
static void UpdateTestProgress(DeviceQueueTestContext *context, const char *message) {
    if (context && context->progressCallback) {
        context->progressCallback(message);
    }
    
    if (context && context->statusStringControl > 0 && context->panelHandle > 0) {
        SetCtrlVal(context->panelHandle, context->statusStringControl, message);
        ProcessDrawEvents();
    }
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
    int result = DeviceQueueTest_Initialize(&g_testContext, panel, control);
    if (result != SUCCESS) {
        LogError("Failed to initialize test context");
        return 0;
    }
    
    // Change button to show Cancel
    SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, "Cancel");
    
    // Start test thread using the test thread pool
    int error = CmtScheduleThreadPoolFunction(g_testContext.testThreadPool, TestThreadFunction, 
                                            &g_testContext, &g_testThreadId);
    if (error != 0) {
        LogError("Failed to start test thread");
        SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, "Test Device Queue");
        g_testContext.state = TEST_STATE_ERROR;
        DeviceQueueTest_Cleanup(&g_testContext);
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
    ctx->cancelRequested = 0;
    
    // Create test-specific thread pool
    ctx->testThreadPoolSize = TEST_THREAD_POOL_SIZE;
    int error = CmtNewThreadPool(ctx->testThreadPoolSize, &ctx->testThreadPool);
    if (error < 0) {
        LogError("Failed to create test thread pool");
        return ERR_THREAD_POOL;
    }
    
    // Create queue manager tracking list
    ctx->activeQueueManagers = ListCreate(sizeof(DeviceQueueManager*));
    if (!ctx->activeQueueManagers) {
        LogError("Failed to create queue manager list");
        CmtDiscardThreadPool(ctx->testThreadPool);
        return ERR_OUT_OF_MEMORY;
    }
    
    // Create lock for queue list
    error = CmtNewLock(NULL, 0, &ctx->queueListLock);
    if (error < 0) {
        LogError("Failed to create queue list lock");
        ListDispose(ctx->activeQueueManagers);
        CmtDiscardThreadPool(ctx->testThreadPool);
        return ERR_THREAD_CREATE;
    }
    
    // Create status string control for progress updates
    // This assumes a status control exists on the panel - adjust as needed
    ctx->statusStringControl = 0; // Will be set if panel has appropriate control
    
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
        CmtDiscardLock(ctx->queueListLock);
        ListDispose(ctx->activeQueueManagers);
        CmtDiscardThreadPool(ctx->testThreadPool);
        return ERR_OUT_OF_MEMORY;
    }
    
    LogMessage("Device Queue Test initialized with dedicated thread pool");
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
        
        // Update UI to show cancelling
        UpdateTestProgress(ctx, "Cancelling tests...");
    }
}

void DeviceQueueTest_Cleanup(DeviceQueueTestContext *ctx) {
    if (!ctx) return;
    
    LogMessage("Cleaning up Device Queue Test context...");
    
    // Clean up all tracked queue managers
    CleanupAllQueueManagers(ctx);
    
    // Clean up any specific queue manager if it exists
    if (ctx->queueManager) {
        UnregisterQueueManager(ctx, ctx->queueManager);
        DeviceQueue_Destroy(ctx->queueManager);
        ctx->queueManager = NULL;
    }
    
    // Clean up mock context
    if (ctx->mockContext) {
        Mock_DestroyContext(ctx->mockContext);
        ctx->mockContext = NULL;
    }
    
    // Wait for any remaining test threads to complete
    if (ctx->testThreadPool) {
        LogMessage("Waiting for test threads to complete...");
        // Give threads time to complete naturally
        // Note: LabWindows/CVI doesn't have a way to wait for all threads in a pool
        // We rely on queue managers being destroyed first, which waits for their threads
        ProcessSystemEvents();
        Delay(0.5);
    }
    
    // Dispose of queue list
    if (ctx->activeQueueManagers) {
        ListDispose(ctx->activeQueueManagers);
        ctx->activeQueueManagers = NULL;
    }
    
    // Dispose of lock
    if (ctx->queueListLock) {
        CmtDiscardLock(ctx->queueListLock);
        ctx->queueListLock = 0;
    }
    
    // Destroy test thread pool
    if (ctx->testThreadPool) {
        LogMessage("Destroying test thread pool...");
        CmtDiscardThreadPool(ctx->testThreadPool);
        ctx->testThreadPool = 0;
    }
    
    LogMessage("Device Queue Test cleanup complete");
}

int DeviceQueueTest_IsRunning(void) {
    return (g_testThreadId != 0);
}

/******************************************************************************
 * Individual Test Implementations
 ******************************************************************************/

int Test_QueueCreation(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize) {
    if (ctx->cancelRequested) return -1;
    
    // Test creating a queue manager with valid parameters
    ctx->queueManager = CreateTestQueueManager(ctx, &g_mockAdapter, ctx->mockContext, NULL);
    
    if (!ctx->queueManager) {
        snprintf(errorMsg, errorMsgSize, "Failed to create queue manager");
        return -1;
    }
    
    // Verify queue is running
    if (!DeviceQueue_IsRunning(ctx->queueManager)) {
        snprintf(errorMsg, errorMsgSize, "Queue manager not running after creation");
        DestroyTestQueueManager(ctx, ctx->queueManager);
        ctx->queueManager = NULL;
        return -1;
    }
    
    // Get stats to verify initialization
    DeviceQueueStats stats;
    DeviceQueue_GetStats(ctx->queueManager, &stats);
    
    if (stats.totalProcessed != 0 || stats.totalErrors != 0) {
        snprintf(errorMsg, errorMsgSize, "Queue stats not initialized properly");
        DestroyTestQueueManager(ctx, ctx->queueManager);
        ctx->queueManager = NULL;
        return -1;
    }
    
    // Clean up
    DestroyTestQueueManager(ctx, ctx->queueManager);
    ctx->queueManager = NULL;
    
    return 1;
}

int Test_QueueDestruction(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize) {
    if (ctx->cancelRequested) return -1;
    
    // Create multiple queues and destroy them
    for (int i = 0; i < 5; i++) {
        if (ctx->cancelRequested) return -1;
        
        MockDeviceContext *tempContext = Mock_CreateContext();
        if (!tempContext) {
            snprintf(errorMsg, errorMsgSize, "Failed to create mock context %d", i);
            return -1;
        }
        
        DeviceQueueManager *tempQueue = CreateTestQueueManager(ctx, &g_mockAdapter, tempContext, NULL);
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
        DestroyTestQueueManager(ctx, tempQueue);
        Mock_DestroyContext(tempContext);
        
        Delay(TEST_DELAY_VERY_SHORT);
    }
    
    return 1;
}

int Test_ConnectionHandling(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize) {
    if (ctx->cancelRequested) return -1;
    
    // Test connection and disconnection scenarios
    ctx->queueManager = CreateTestQueueManager(ctx, &g_mockAdapter, ctx->mockContext, NULL);
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
    
    if (ctx->cancelRequested) goto cleanup;
    
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
    
    if (ctx->cancelRequested) goto cleanup;
    
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
    
    DestroyTestQueueManager(ctx, ctx->queueManager);
    ctx->queueManager = NULL;
    return 1;
    
cleanup:
    DestroyTestQueueManager(ctx, ctx->queueManager);
    ctx->queueManager = NULL;
    return ctx->cancelRequested ? -1 : -1;
}

int Test_BlockingCommands(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize) {
    if (ctx->cancelRequested) return -1;
    
    ctx->queueManager = CreateTestQueueManager(ctx, &g_mockAdapter, ctx->mockContext, NULL);
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
    
    if (ctx->cancelRequested) goto cleanup;
    
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
    
    DestroyTestQueueManager(ctx, ctx->queueManager);
    ctx->queueManager = NULL;
    return 1;
    
cleanup:
    DestroyTestQueueManager(ctx, ctx->queueManager);
    ctx->queueManager = NULL;
    return ctx->cancelRequested ? -1 : -1;
}

int Test_AsyncCommands(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize) {
    if (ctx->cancelRequested) return -1;
    
    ctx->queueManager = CreateTestQueueManager(ctx, &g_mockAdapter, ctx->mockContext, NULL);
    if (!ctx->queueManager) {
        snprintf(errorMsg, errorMsgSize, "Failed to create queue manager");
        return -1;
    }
    
    AsyncTracker trackers[5] = {0};
    
    // Submit multiple async commands
    for (int i = 0; i < 5; i++) {
        if (ctx->cancelRequested) goto cleanup;
        
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
    
    while (Timer() < timeout && !allCompleted && !ctx->cancelRequested) {
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
    
    if (ctx->cancelRequested) goto cleanup;
    
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
    
    DestroyTestQueueManager(ctx, ctx->queueManager);
    ctx->queueManager = NULL;
    return 1;
    
cleanup:
    DestroyTestQueueManager(ctx, ctx->queueManager);
    ctx->queueManager = NULL;
    return ctx->cancelRequested ? -1 : -1;
}

int Test_PriorityHandling(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize) {
    if (ctx->cancelRequested) return -1;
    
    ctx->queueManager = CreateTestQueueManager(ctx, &g_mockAdapter, ctx->mockContext, NULL);
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
        if (ctx->cancelRequested) goto cleanup;
        
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
        if (ctx->cancelRequested) goto cleanup;
        
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
        if (ctx->cancelRequested) goto cleanup;
        
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
    
    while (Timer() < timeout && !allCompleted && !ctx->cancelRequested) {
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
    
    if (ctx->cancelRequested) goto cleanup;
    
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
    
    DestroyTestQueueManager(ctx, ctx->queueManager);
    ctx->queueManager = NULL;
    return 1;
    
cleanup:
    Mock_SetCommandDelay(ctx->mockContext, MOCK_COMMAND_DELAY_MS);
    DestroyTestQueueManager(ctx, ctx->queueManager);
    ctx->queueManager = NULL;
    return ctx->cancelRequested ? -1 : -1;
}

int Test_CommandCancellation(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize) {
    if (ctx->cancelRequested) return -1;
    
    ctx->queueManager = CreateTestQueueManager(ctx, &g_mockAdapter, ctx->mockContext, NULL);
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
    
    if (ctx->cancelRequested) goto cleanup;
    
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
    
    if (ctx->cancelRequested) goto cleanup;
    
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
    
    if (ctx->cancelRequested) goto cleanup;
    
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
    
    DestroyTestQueueManager(ctx, ctx->queueManager);
    ctx->queueManager = NULL;
    return 1;
    
cleanup:
    Mock_SetCommandDelay(ctx->mockContext, MOCK_COMMAND_DELAY_MS);
    DestroyTestQueueManager(ctx, ctx->queueManager);
    ctx->queueManager = NULL;
    return ctx->cancelRequested ? -1 : -1;
}

int Test_Transactions(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize) {
    if (ctx->cancelRequested) return -1;
    
    ctx->queueManager = CreateTestQueueManager(ctx, &g_mockAdapter, ctx->mockContext, NULL);
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
        if (ctx->cancelRequested) goto cleanup;
        
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
    while (Timer() < timeout && !tracker.completed && !ctx->cancelRequested) {
        ProcessSystemEvents();
        Delay(0.05);
    }
    
    if (ctx->cancelRequested) goto cleanup;
    
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
    while (Timer() < timeout && !abortTracker.completed && !ctx->cancelRequested) {
        ProcessSystemEvents();
        Delay(0.05);
    }
    
    if (ctx->cancelRequested) goto cleanup;
    
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
    
    // Additional transaction tests omitted for brevity but follow same pattern...
    
    DestroyTestQueueManager(ctx, ctx->queueManager);
    ctx->queueManager = NULL;
    return 1;
    
cleanup:
    Mock_SetCommandDelay(ctx->mockContext, MOCK_COMMAND_DELAY_MS);
    DestroyTestQueueManager(ctx, ctx->queueManager);
    ctx->queueManager = NULL;
    return ctx->cancelRequested ? -1 : -1;
}

// Similar pattern for remaining tests...
// Each test should:
// 1. Check ctx->cancelRequested at start and key points
// 2. Use CreateTestQueueManager instead of DeviceQueue_Create
// 3. Use DestroyTestQueueManager for cleanup
// 4. Return -1 if cancelled

int Test_QueueOverflow(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize) {
    if (ctx->cancelRequested) return -1;
    
    ctx->queueManager = CreateTestQueueManager(ctx, &g_mockAdapter, ctx->mockContext, NULL);
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
        if (ctx->cancelRequested) goto cleanup;
        
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
    
    // Restore normal delay
    Mock_SetCommandDelay(ctx->mockContext, MOCK_COMMAND_DELAY_MS);
    
    DestroyTestQueueManager(ctx, ctx->queueManager);
    ctx->queueManager = NULL;
    return 1;
    
cleanup:
    Mock_SetCommandDelay(ctx->mockContext, MOCK_COMMAND_DELAY_MS);
    DeviceQueue_CancelAll(ctx->queueManager);
    DestroyTestQueueManager(ctx, ctx->queueManager);
    ctx->queueManager = NULL;
    return ctx->cancelRequested ? -1 : -1;
}

int Test_ErrorHandling(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize) {
    if (ctx->cancelRequested) return -1;
    
    ctx->queueManager = CreateTestQueueManager(ctx, &g_mockAdapter, ctx->mockContext, NULL);
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
        if (ctx->cancelRequested) goto cleanup;
        
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
    
    DestroyTestQueueManager(ctx, ctx->queueManager);
    ctx->queueManager = NULL;
    return 1;
    
cleanup:
    Mock_SetFailureRate(ctx->mockContext, 0);
    DestroyTestQueueManager(ctx, ctx->queueManager);
    ctx->queueManager = NULL;
    return ctx->cancelRequested ? -1 : -1;
}

int Test_Timeouts(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize) {
    if (ctx->cancelRequested) return -1;
    
    ctx->queueManager = CreateTestQueueManager(ctx, &g_mockAdapter, ctx->mockContext, NULL);
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
    
    DestroyTestQueueManager(ctx, ctx->queueManager);
    ctx->queueManager = NULL;
    return 1;
    
cleanup:
    ctx->mockContext->simulateTimeout = 0;
    DestroyTestQueueManager(ctx, ctx->queueManager);
    ctx->queueManager = NULL;
    return ctx->cancelRequested ? -1 : -1;
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
    if (ctx->cancelRequested) return -1;
    
    ctx->queueManager = CreateTestQueueManager(ctx, &g_mockAdapter, ctx->mockContext, NULL);
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
    
    // Start worker threads using test thread pool
    for (int i = 0; i < TEST_THREAD_COUNT; i++) {
        if (ctx->cancelRequested) goto cleanup;
        
        int error = CmtScheduleThreadPoolFunction(ctx->testThreadPool, MixedWorkerFunction,
                                                &workerData[i], &threads[i]);
        if (error != 0) {
            snprintf(errorMsg, errorMsgSize, "Failed to start thread %d", i);
            goto cleanup;
        }
    }
    
    // Wait for all threads to complete submission
    for (int i = 0; i < TEST_THREAD_COUNT; i++) {
        CmtWaitForThreadPoolFunctionCompletion(ctx->testThreadPool, threads[i],
                                             OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
    }
    
    if (ctx->cancelRequested) goto cleanup;
    
    // Wait for all operations to complete
    double timeout = Timer() + 10.0;
    int totalSubmitted = 0, totalCompleted = 0;
    int totalTxnCreated = 0, totalTxnCompleted = 0;
    
    while (Timer() < timeout && !ctx->cancelRequested) {
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
    
    if (ctx->cancelRequested) goto cleanup;
    
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
    
    DestroyTestQueueManager(ctx, ctx->queueManager);
    ctx->queueManager = NULL;
    return 1;
    
cleanup:
    DestroyTestQueueManager(ctx, ctx->queueManager);
    ctx->queueManager = NULL;
    return ctx->cancelRequested ? -1 : -1;
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
    if (ctx->cancelRequested) return -1;
    
    ctx->queueManager = CreateTestQueueManager(ctx, &g_mockAdapter, ctx->mockContext, NULL);
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
    
    CmtScheduleThreadPoolFunction(ctx->testThreadPool, ThreadWorkerFunction,
                                &workerData, &submitThread);
    CmtScheduleThreadPoolFunction(ctx->testThreadPool, CancellationWorkerFunction,
                                &workerData, &cancelThread1);
    CmtScheduleThreadPoolFunction(ctx->testThreadPool, CancellationWorkerFunction,
                                &workerData, &cancelThread2);
    
    // Let them run for a while
    double timeout = Timer() + 2.0;
    while (Timer() < timeout && !ctx->cancelRequested) {
        ProcessSystemEvents();
        Delay(0.1);
    }
    
    // Wait for threads to complete
    CmtWaitForThreadPoolFunctionCompletion(ctx->testThreadPool, submitThread,
                                         OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
    CmtWaitForThreadPoolFunctionCompletion(ctx->testThreadPool, cancelThread1,
                                         OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
    CmtWaitForThreadPoolFunctionCompletion(ctx->testThreadPool, cancelThread2,
                                         OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
    
    // Cancel any remaining commands
    DeviceQueue_CancelAll(ctx->queueManager);
    
    // Restore normal delay
    Mock_SetCommandDelay(ctx->mockContext, MOCK_COMMAND_DELAY_MS);
    
    DestroyTestQueueManager(ctx, ctx->queueManager);
    ctx->queueManager = NULL;
    return 1;
}

int Test_Statistics(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize) {
    if (ctx->cancelRequested) return -1;
    
    ctx->queueManager = CreateTestQueueManager(ctx, &g_mockAdapter, ctx->mockContext, NULL);
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
        if (ctx->cancelRequested) goto cleanup;
        
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
        if (ctx->cancelRequested) goto cleanup;
        
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
    
    DestroyTestQueueManager(ctx, ctx->queueManager);
    ctx->queueManager = NULL;
    return 1;
    
cleanup:
    Mock_SetFailureRate(ctx->mockContext, 0);
    DestroyTestQueueManager(ctx, ctx->queueManager);
    ctx->queueManager = NULL;
    return ctx->cancelRequested ? -1 : -1;
}

int Test_ReconnectionLogic(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize) {
    if (ctx->cancelRequested) return -1;
    
    // Set initial disconnected state
    Mock_SetConnectionState(ctx->mockContext, 0);
    ctx->mockContext->shouldFailConnection = 1;
    
    ctx->queueManager = CreateTestQueueManager(ctx, &g_mockAdapter, ctx->mockContext, NULL);
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
    double timeout = Timer() + 5.0;
    while (Timer() < timeout && !ctx->cancelRequested) {
        ProcessSystemEvents();
        Delay(0.1);
    }
    
    if (ctx->cancelRequested) goto cleanup;
    
    DeviceQueue_GetStats(ctx->queueManager, &stats);
    if (stats.reconnectAttempts < 2) {
        snprintf(errorMsg, errorMsgSize, "Not enough reconnection attempts: %d", 
                stats.reconnectAttempts);
        goto cleanup;
    }
    
    // Enable connection
    ctx->mockContext->shouldFailConnection = 0;
    Mock_SetConnectionState(ctx->mockContext, 1);
    
    // Wait for reconnection
    timeout = Timer() + 5.0;
    while (Timer() < timeout && !ctx->cancelRequested) {
        ProcessSystemEvents();
        Delay(0.1);
    }
    
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
    
    DestroyTestQueueManager(ctx, ctx->queueManager);
    ctx->queueManager = NULL;
    return 1;
    
cleanup:
    ctx->mockContext->shouldFailConnection = 0;
    DestroyTestQueueManager(ctx, ctx->queueManager);
    ctx->queueManager = NULL;
    return ctx->cancelRequested ? -1 : -1;
}

int Test_EdgeCases(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize) {
    if (ctx->cancelRequested) return -1;
    
    // Test creating queue with NULL adapter
    DeviceQueueManager *badQueue = CreateTestQueueManager(ctx, NULL, ctx->mockContext, NULL);
    if (badQueue != NULL) {
        DestroyTestQueueManager(ctx, badQueue);
        snprintf(errorMsg, errorMsgSize, "Should reject NULL adapter");
        return -1;
    }
    
    // Test creating queue with NULL context
    badQueue = CreateTestQueueManager(ctx, &g_mockAdapter, NULL, NULL);
    if (badQueue != NULL) {
        DestroyTestQueueManager(ctx, badQueue);
        snprintf(errorMsg, errorMsgSize, "Should reject NULL device context");
        return -1;
    }
    
    // Create valid queue for remaining tests
    ctx->queueManager = CreateTestQueueManager(ctx, &g_mockAdapter, ctx->mockContext, NULL);
    if (!ctx->queueManager) {
        snprintf(errorMsg, errorMsgSize, "Failed to create queue manager");
        return -1;
    }
    
    // Test null result pointer for blocking command
    int error = DeviceQueue_CommandBlocking(ctx->queueManager, MOCK_CMD_SET_VALUE, NULL,
                                          DEVICE_PRIORITY_HIGH, NULL, 1000);
    if (error != ERR_INVALID_PARAMETER) {
        snprintf(errorMsg, errorMsgSize, "Should reject NULL result pointer");
        goto cleanup;
    }
    
    DestroyTestQueueManager(ctx, ctx->queueManager);
    ctx->queueManager = NULL;
    return 1;
    
cleanup:
    DestroyTestQueueManager(ctx, ctx->queueManager);
    ctx->queueManager = NULL;
    return ctx->cancelRequested ? -1 : -1;
}