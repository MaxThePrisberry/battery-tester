/******************************************************************************
 * device_queue_test.h
 * 
 * Comprehensive test suite for the generic device queue system
 * Tests all aspects of the queue without requiring real hardware
 ******************************************************************************/

#ifndef DEVICE_QUEUE_TEST_H
#define DEVICE_QUEUE_TEST_H

#include "common.h"
#include "device_queue.h"
#include <toolbox.h>  // For List functions

/******************************************************************************
 * Test Configuration
 ******************************************************************************/

// Test timing
#define TEST_DELAY_VERY_SHORT   0.05    // 50ms
#define TEST_DELAY_SHORT        0.1     // 100ms
#define TEST_DELAY_MEDIUM       0.5     // 500ms
#define TEST_DELAY_LONG         1.0     // 1 second

// Mock device configuration
#define MOCK_DEVICE_NAME        "Mock Device"
#define MOCK_COMMAND_DELAY_MS   10      // Default command execution delay
#define MOCK_CONNECT_DELAY_MS   100     // Connection delay
#define MOCK_DEFAULT_TIMEOUT_MS 1000    // Default timeout

// Thread testing
#define TEST_THREAD_COUNT       4       // Number of concurrent threads to test
#define COMMANDS_PER_THREAD     10      // Commands each thread will submit
#define TEST_THREAD_POOL_SIZE   10      // Size of test-specific thread pool

// Queue testing limits
#define TEST_MAX_COMMANDS       100     // Maximum commands for stress testing
#define TEST_QUEUE_OVERFLOW     200     // Commands to cause overflow

/******************************************************************************
 * Mock Device Command Types
 ******************************************************************************/

typedef enum {
    MOCK_CMD_NONE = 0,
    MOCK_CMD_TEST_CONNECTION,
    MOCK_CMD_SET_VALUE,
    MOCK_CMD_GET_VALUE,
    MOCK_CMD_SLOW_OPERATION,
    MOCK_CMD_FAILING_OPERATION,
    MOCK_CMD_TYPE_COUNT
} MockCommandType;

/******************************************************************************
 * Mock Device Structures
 ******************************************************************************/

// Mock device context
typedef struct {
    // Connection state
    volatile int isConnected;
    volatile int shouldFailConnection;
    volatile int connectionFailCount;
    
    // Command execution control
    volatile int shouldFailCommands;
    volatile int commandFailRate;      // Percentage (0-100)
    volatile int commandDelay;         // Milliseconds
    
    // Statistics
    volatile int connectCount;
    volatile int disconnectCount;
    volatile int commandsExecuted;
    volatile int commandsFailed;
    
    // Thread safety
    CmtThreadLockHandle statsLock;
    
    // Test control
    volatile int simulateTimeout;
    volatile int simulateDisconnect;
    
} MockDeviceContext;

// Mock command parameters
typedef struct {
    int value;
    double delay;
    char message[256];
} MockCommandParams;

// Mock command result
typedef struct {
    int success;
    int value;
    char message[256];
} MockCommandResult;

/******************************************************************************
 * Test Context Structure
 ******************************************************************************/

typedef struct {
    // Test control
    volatile int cancelRequested;
    TestState state;
    
    // UI integration
    int panelHandle;
    int buttonControl;
    int statusStringControl;
    void (*progressCallback)(const char *message);
    
    // Test tracking
    int totalTests;
    int passedTests;
    int failedTests;
    double suiteStartTime;
    
    // Current test info
    char currentTestName[256];
    double testStartTime;
    
    // Mock device and queue
    MockDeviceContext *mockContext;
    DeviceQueueManager *queueManager;
    
    // Thread testing
    CmtThreadFunctionID workerThreads[TEST_THREAD_COUNT];
    int threadResults[TEST_THREAD_COUNT];
    
    // Queue manager tracking
    ListType activeQueueManagers;      // List of DeviceQueueManager*
    CmtThreadLockHandle queueListLock;
    
    // Test-specific thread pool
    CmtThreadPoolHandle testThreadPool;
    int testThreadPoolSize;
    
} DeviceQueueTestContext;

/******************************************************************************
 * Test Result Structure
 ******************************************************************************/

typedef struct {
    const char *testName;
    int (*testFunction)(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize);
    int result;  // 0 = not run, 1 = pass, -1 = fail
    char errorMessage[256];
    double executionTime;
} TestCase;

/******************************************************************************
 * Public Function Prototypes
 ******************************************************************************/

// Main callback for UI button
int CVICALLBACK TestDeviceQueueCallback(int panel, int control, int event,
                                       void *callbackData, int eventData1, 
                                       int eventData2);
int CVICALLBACK TestDeviceQueueWorkerThread(void *functionData);

// Test suite control functions
int DeviceQueueTest_Initialize(DeviceQueueTestContext *ctx, int panel, int buttonControl);
int DeviceQueueTest_Run(DeviceQueueTestContext *ctx);
void DeviceQueueTest_Cancel(DeviceQueueTestContext *ctx);
void DeviceQueueTest_Cleanup(DeviceQueueTestContext *ctx);
int DeviceQueueTest_IsRunning(void);

// Individual test functions (all return 1 for pass, -1 for fail)
int Test_QueueCreation(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize);
int Test_QueueDestruction(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize);
int Test_ConnectionHandling(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize);
int Test_BlockingCommands(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize);
int Test_AsyncCommands(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize);
int Test_PriorityHandling(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize);
int Test_CommandCancellation(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize);
int Test_Transactions(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize);
int Test_QueueOverflow(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize);
int Test_ErrorHandling(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize);
int Test_Timeouts(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize);
int Test_ThreadSafety(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize);
int Test_ConcurrentCancellation(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize);
int Test_Statistics(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize);
int Test_ReconnectionLogic(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize);
int Test_EdgeCases(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize);
int Test_EmptyTransaction(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize);
int Test_GetDeviceContext(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize);
int Test_ShutdownWithBlockingCommand(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize);
int Test_TransactionPriorityOrdering(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize);
int Test_ThreadPoolExhaustion(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize);
int Test_SetLogDevice(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize);
int Test_IsInTransaction(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize);
int Test_LargeTransactions(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize);
int Test_NullCallbacks(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize);
int Test_MixedCommandsAndTransactions(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize);
int Test_TransactionTimeout(DeviceQueueTestContext *ctx, char *errorMsg, int errorMsgSize);

// Mock device helper functions
MockDeviceContext* Mock_CreateContext(void);
void Mock_DestroyContext(MockDeviceContext *ctx);
void Mock_SetConnectionState(MockDeviceContext *ctx, int connected);
void Mock_SetFailureRate(MockDeviceContext *ctx, int rate);
void Mock_SetCommandDelay(MockDeviceContext *ctx, int delayMs);
void Mock_ResetStatistics(MockDeviceContext *ctx);

#endif // DEVICE_QUEUE_TEST_H