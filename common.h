/******************************************************************************
 * common.h
 * 
 * Common declarations, types, and macros used across the Battery Tester
 * LabWindows/CVI project
 * 
 * This header should be included by most source files in the project
 ******************************************************************************/

#ifndef COMMON_H
#define COMMON_H

//==============================================================================
// Required System Includes
//==============================================================================
#ifdef _WIN32
   #include <windows.h>
#endif

#include <ansi_c.h>
#include <cvirte.h>
#include <userint.h>
#include <utility.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <math.h>

//==============================================================================
// Project Configuration
//==============================================================================
#define PROJECT_NAME            "Battery Tester"
#define PROJECT_VERSION         "1.0.0"
#define MAX_PATH_LENGTH         260
#define MAX_ERROR_MSG_LENGTH    256
#define MAX_LOG_LINE_LENGTH     512
#define DEFAULT_THREAD_POOL_SIZE 10     // Increased for queue processing threads

//==============================================================================
// Error Code Definitions
//==============================================================================

// Success code
#define SUCCESS                 0

// Base error codes for different modules
#define ERR_BASE_SYSTEM         -1000
#define ERR_BASE_BIOLOGIC       -2000
#define ERR_BASE_PSB            -3000
#define ERR_BASE_TEST           -4000
#define ERR_BASE_UI             -5000
#define ERR_BASE_FILE           -6000
#define ERR_BASE_THREAD         -7000

// System errors (-1000 to -1999)
#define ERR_INVALID_PARAMETER   (ERR_BASE_SYSTEM - 1)
#define ERR_NULL_POINTER        (ERR_BASE_SYSTEM - 2)
#define ERR_OUT_OF_MEMORY       (ERR_BASE_SYSTEM - 3)
#define ERR_NOT_INITIALIZED     (ERR_BASE_SYSTEM - 4)
#define ERR_ALREADY_INITIALIZED (ERR_BASE_SYSTEM - 5)
#define ERR_TIMEOUT             (ERR_BASE_SYSTEM - 6)
#define ERR_OPERATION_FAILED    (ERR_BASE_SYSTEM - 7)
#define ERR_NOT_SUPPORTED       (ERR_BASE_SYSTEM - 8)
#define ERR_INVALID_STATE       (ERR_BASE_SYSTEM - 9)
#define ERR_COMM_FAILED         (ERR_BASE_SYSTEM - 10)

// Queue-specific errors
#define ERR_QUEUE_FULL          (ERR_BASE_SYSTEM - 11)
#define ERR_QUEUE_EMPTY         (ERR_BASE_SYSTEM - 12)
#define ERR_QUEUE_TIMEOUT       (ERR_BASE_SYSTEM - 13)
#define ERR_QUEUE_NOT_INIT      (ERR_BASE_SYSTEM - 14)
#define ERR_CANCELLED           (ERR_BASE_SYSTEM - 15)

// UI errors (-5000 to -5999)
#define ERR_UI                  (ERR_BASE_UI - 1)

// Thread errors (-7000 to -7999)
#define ERR_THREAD_CREATE       (ERR_BASE_THREAD - 1)
#define ERR_THREAD_POOL         (ERR_BASE_THREAD - 2)
#define ERR_THREAD_SYNC         (ERR_BASE_THREAD - 3)

//==============================================================================
// Common Type Definitions
//==============================================================================

// Boolean type for older C standards
#ifndef __cplusplus
    typedef int bool;
    #define true    1
    #define false   0
#endif

// Result type for functions that return error codes
typedef int Result;

// Device connection states
typedef enum {
    DEVICE_STATE_DISCONNECTED = 0,
    DEVICE_STATE_CONNECTING,
    DEVICE_STATE_CONNECTED,
    DEVICE_STATE_READY,
    DEVICE_STATE_RUNNING,
    DEVICE_STATE_ERROR
} DeviceState;

// Test states
typedef enum {
    TEST_STATE_IDLE = 0,
    TEST_STATE_PREPARING,
    TEST_STATE_RUNNING,
    TEST_STATE_PAUSED,
    TEST_STATE_COMPLETED,
    TEST_STATE_ABORTED,
    TEST_STATE_ERROR
} TestState;

// Generic callback function types
typedef void (*ProgressCallback)(const char *message, double progress);
typedef void (*ErrorCallback)(int errorCode, const char *errorMessage);
typedef void (*DataCallback)(double timestamp, double voltage, double current);

// Callback type definitions for worker threads
typedef int (CVICALLBACK *WorkerThreadFunc)(void *functionData);
typedef void (CVICALLBACK *DeferredUICallback)(void *callbackData);

// Time measurement structure
typedef struct {
    double startTime;
    double elapsedTime;
    double lastUpdateTime;
} TimeInfo;

// Generic device info structure
typedef struct {
    char modelName[128];
    char serialNumber[128];
    char firmwareVersion[64];
    DeviceState state;
    int lastError;
    char lastErrorMsg[MAX_ERROR_MSG_LENGTH];
} DeviceInfo;

//==============================================================================
// Global Variables (declare as extern, define in one .c file)
//==============================================================================

// Main panel handle - defined in BatteryTester.c
extern int g_mainPanelHandle;

// Global debug flag
extern int g_debugMode;

// Thread pool handle for background operations
extern CmtThreadPoolHandle g_threadPool;

// Global busy system mutex and flag
extern CmtThreadLockHandle g_busyLock;
extern int g_systemBusy;

//==============================================================================
// Utility Macros
//==============================================================================

// Safe memory allocation
#define SAFE_MALLOC(ptr, type, count) \
    do { \
        (ptr) = (type*)calloc((count), sizeof(type)); \
        if (!(ptr)) { \
            LogError("Memory allocation failed: %s, line %d", __FILE__, __LINE__); \
            return ERR_OUT_OF_MEMORY; \
        } \
    } while(0)

// Safe memory free
#define SAFE_FREE(ptr) \
    do { \
        if (ptr) { \
            free(ptr); \
            (ptr) = NULL; \
        } \
    } while(0)

// Min/Max macros
#ifndef MIN
    #define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef MAX
    #define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

// Clamp value between min and max
#define CLAMP(val, min, max) (MAX(MIN((val), (max)), (min)))

// Check if value is within range (inclusive)
#define IN_RANGE(val, min, max) (((val) >= (min)) && ((val) <= (max)))

// Convert between units
#define V_TO_MV(v) ((v) * 1000.0)
#define MV_TO_V(mv) ((mv) / 1000.0)
#define A_TO_MA(a) ((a) * 1000.0)
#define MA_TO_A(ma) ((ma) / 1000.0)

// Time conversion
#define MS_TO_S(ms) ((ms) / 1000.0)
#define S_TO_MS(s) ((s) * 1000.0)

// Array size macro
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

// String safety macros
#define SAFE_STRCPY(dest, src, size) \
    do { \
        strncpy((dest), (src), (size) - 1); \
        (dest)[(size) - 1] = '\0'; \
    } while(0)

#define SAFE_SPRINTF(buffer, size, ...) \
    snprintf((buffer), (size), __VA_ARGS__)

// Thread-safe operation macros for queue system
#define CHECK_AND_SET_BUSY(lock, flag, action_if_busy) \
    do { \
        CmtGetLock(lock); \
        if (flag) { \
            CmtReleaseLock(lock); \
            action_if_busy; \
        } else { \
            flag = 1; \
            CmtReleaseLock(lock); \
        } \
    } while(0)

#define CLEAR_BUSY(lock, flag) \
    do { \
        CmtGetLock(lock); \
        flag = 0; \
        CmtReleaseLock(lock); \
    } while(0)

//==============================================================================
// Debug and Logging Functions
//==============================================================================

// These should be implemented in a common logging module
void LogMessage(const char *format, ...);
void LogError(const char *format, ...);
void LogDebug(const char *format, ...);
void LogWarning(const char *format, ...);

// Debug print that can be compiled out
#ifdef _DEBUG
    #define DEBUG_PRINT(fmt, ...) \
        do { \
            if (g_debugMode) { \
                printf("[DEBUG] " fmt "\n", ##__VA_ARGS__); \
            } \
        } while(0)
#else
    #define DEBUG_PRINT(fmt, ...) ((void)0)
#endif

// Assert macro for debugging
#ifdef _DEBUG
    #define ASSERT(condition) \
        do { \
            if (!(condition)) { \
                LogError("Assertion failed: %s, file %s, line %d", \
                        #condition, __FILE__, __LINE__); \
                MessagePopup("Assertion Failed", \
                            "Assertion failed:\n" #condition "\n\n" \
                            "File: " __FILE__ "\n" \
                            "Line: " #__LINE__); \
                abort(); \
            } \
        } while(0)
#else
    #define ASSERT(condition) ((void)0)
#endif

//==============================================================================
// UI Update Notes
//==============================================================================
/*
 * Note: The UPDATE_UI_STRING and UPDATE_UI_NUMERIC macros have been removed
 * because PostDeferredCall in LabWindows/CVI only accepts 2 parameters.
 * 
 * To update UI from background threads:
 * 1. Use the logging module's built-in thread-safe UI update
 * 2. Create a custom structure to pass multiple parameters through PostDeferredCall
 * 3. Call PostDeferredCall with a callback function and your data structure
 * 
 * Example:
 *   typedef struct { int panel; int control; char *text; } UIData;
 *   UIData *data = malloc(sizeof(UIData));
 *   data->panel = panel; data->control = control; data->text = strdup(text);
 *   PostDeferredCall(MyUIUpdateCallback, data);
 */

//==============================================================================
// Common Utility Functions
//==============================================================================

// Error handling
const char* GetErrorString(int errorCode);          // Central error string function in utils.c
void ClearLastError(void);
void SetLastErrorMessage(int errorCode, const char *format, ...);  // Renamed to avoid Windows conflict

// String utilities
char* TrimWhitespace(char *str);
int ParseDouble(const char *str, double *value);
int ParseInt(const char *str, int *value);

// Time utilities  
double GetTimestamp(void);  // Renamed to avoid Windows GetCurrentTime macro conflict
void FormatTimeString(double seconds, char *buffer, int bufferSize);
void FormatTimestamp(time_t timestamp, char *buffer, int bufferSize);

// File utilities
int FileExists(const char *filename);
int CreateDirectoryPath(const char *path);  // Renamed to avoid Windows conflict
int GetExecutableDirectory(char *path, int pathSize);

/******************************************************************************
 * UI Helper Functions (implemented in a UI utility module)
 ******************************************************************************/
void UpdateUIString(void *panel, void *control, void *text);
void UpdateUINumeric(void *panel, void *control, void *value);
void EnablePanel(int panel, int enable);
void ShowBusyCursor(int show);

/**
 * Dim/enable all controls in a control array
 * @param panel - Panel handle containing the controls
 * @param arrayID - Control array resource ID (e.g., BATTERY_CONSTANTS_ARR)
 * @param dim - 1 to dim controls, 0 to enable them
 */
void DimControlArray(int panel, int arrayID, int dim);

/**
 * Dim/enable controls for capacity experiment
 * This function handles:
 * - Control arrays (BATTERY_CONSTANTS_ARR, MANUAL_CONTROL_ARR)
 * - Tab control locking
 * - Specific controls on the capacity tab
 * 
 * @param mainPanel - Main panel handle
 * @param tabPanel - Tab panel handle (capacity tab)
 * @param dim - 1 to dim controls, 0 to enable them
 * @param currentThresholdCtrl - Control ID for current threshold numeric
 * @param intervalCtrl - Control ID for interval numeric
 * @param return50Ctrl - Control ID for return to 50% checkbox
 */
void DimCapacityExperimentControls(int mainPanel, int tabPanel, int dim,
                                   int currentThresholdCtrl, int intervalCtrl, int return50Ctrl);

// Thread synchronization helpers
int WaitForCondition(int (*condition)(void), double timeoutSeconds);

// Queue system integration points
void InitializeQueueManagers(void);
void ShutdownQueueManagers(void);
int CheckSystemBusy(const char *operation);
void SetSystemBusy(int busy);

//==============================================================================
// Common Constants
//==============================================================================

// Default timeouts (in seconds)
#define DEFAULT_COMM_TIMEOUT    5.0
#define DEFAULT_CONNECT_TIMEOUT 10.0
#define DEFAULT_TEST_TIMEOUT    3600.0  // 1 hour

// Buffer sizes
#define SMALL_BUFFER_SIZE       64
#define MEDIUM_BUFFER_SIZE      256
#define LARGE_BUFFER_SIZE       1024
#define HUGE_BUFFER_SIZE        4096

// UI update rates
#define UI_UPDATE_RATE_FAST     0.1     // 100ms
#define UI_UPDATE_RATE_NORMAL   0.5     // 500ms
#define UI_UPDATE_RATE_SLOW     1.0     // 1s

// File extensions
#define DATA_FILE_EXT           ".csv"
#define CONFIG_FILE_EXT         ".ini"
#define LOG_FILE_EXT            ".log"

#ifndef OPT_TL_EVENT_UNICAST
#define OPT_TL_EVENT_UNICAST    0x00000001
#endif

#ifndef TSQ_INFINITE_TIMEOUT
#define TSQ_INFINITE_TIMEOUT    -1
#endif

//==============================================================================
// Platform-specific definitions
//==============================================================================

#ifdef _WIN32
    #define PATH_SEPARATOR      "\\"
    #define PATH_SEPARATOR_CHAR '\\'
    #define NEWLINE            "\r\n"
#else
    #define PATH_SEPARATOR      "/"
    #define PATH_SEPARATOR_CHAR '/'
    #define NEWLINE            "\n"
#endif

//==============================================================================
// Disable specific warnings for external headers
//==============================================================================

#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable: 4996)  // Disable deprecation warnings
#endif

//==============================================================================
// End of Header
//==============================================================================

#endif // COMMON_H