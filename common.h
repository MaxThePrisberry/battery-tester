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
// Device-specific variables
//==============================================================================
// Device enable flags - set to 1 to enable monitoring, 0 to disable
#define ENABLE_PSB         1    // Enable PSB 10000 monitoring
#define ENABLE_BIOLOGIC    1    // Enable BioLogic SP-150e monitoring
#define ENABLE_DTB         1    // Enable DTB4848 monitoring
#define ENABLE_TNY         1    // Enable Teensy monitoring
#define ENABLE_CDAQ        1    // Enable cDAQ 9178

#define PSB_COM_PORT            3       // PSB 10000 COM port
#define PSB_SLAVE_ADDRESS       1       // PSB Modbus slave address
#define PSB_BAUD_RATE           9600    // PSB baud rate

#define DTB_COM_PORT            5       // DTB 4848 COM port  
#define DTB_SLAVE_ADDRESS       1       // DTB Modbus slave address
#define DTB_BAUD_RATE           9600    // DTB baud rate

#define TNY_COM_PORT            6       // Teensy COM port

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
#define ERR_BASE_DTB            -8000
#define ERR_BASE_TNY            -9000

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
#define ERR_NOT_CONNECTED       (ERR_BASE_SYSTEM - 11)

// Queue-specific errors
#define ERR_QUEUE_FULL          (ERR_BASE_SYSTEM - 20)
#define ERR_QUEUE_EMPTY         (ERR_BASE_SYSTEM - 21)
#define ERR_QUEUE_TIMEOUT       (ERR_BASE_SYSTEM - 22)
#define ERR_QUEUE_NOT_INIT      (ERR_BASE_SYSTEM - 23)
#define ERR_CANCELLED           (ERR_BASE_SYSTEM - 24)

// UI errors (-5000 to -5999)
#define ERR_UI                  (ERR_BASE_UI - 1)

// Thread errors (-7000 to -7999)
#define ERR_THREAD_CREATE       (ERR_BASE_THREAD - 1)
#define ERR_THREAD_POOL         (ERR_BASE_THREAD - 2)
#define ERR_THREAD_SYNC         (ERR_BASE_THREAD - 3)

//==============================================================================
// Common Type Definitions
//==============================================================================

// Boolean type for old C standards
#ifndef __cplusplus
    typedef int bool;
    #define true    1
    #define false   0
#endif

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

// Min/Max macros
#ifndef MIN
    #define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef MAX
    #define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

// Clamp value between min and max
#define CLAMP(val, min, max) (MAX(MIN((val), (max)), (min)))

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

//==============================================================================
// UI Update Notes
//==============================================================================
/*
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

/******************************************************************************
 * Graph Utility Functions
 ******************************************************************************/

/**
 * Clear all plots from multiple graphs
 * @param panel - Panel handle containing the graphs
 * @param graphs - Array of graph control IDs to clear
 * @param numGraphs - Number of graphs in the array
 */
void ClearAllGraphs(int panel, const int graphs[], int numGraphs);

/**
 * Configure a graph with title and axis labels
 * @param panel - Panel handle containing the graph
 * @param graph - Graph control ID
 * @param title - Graph title text
 * @param xLabel - X-axis label text
 * @param yLabel - Y-axis label text
 * @param yMin - Minimum Y-axis value
 * @param yMax - Maximum Y-axis value
 */
void ConfigureGraph(int panel, int graph, const char *title, const char *xLabel, 
                   const char *yLabel, double yMin, double yMax);

/**
 * Plot a single data point on a graph
 * @param panel - Panel handle containing the graph
 * @param graph - Graph control ID
 * @param x - X-coordinate of the point
 * @param y - Y-coordinate of the point
 * @param style - Point style (e.g., VAL_SOLID_CIRCLE)
 * @param color - Point color (e.g., VAL_RED)
 */
void PlotDataPoint(int panel, int graph, double x, double y, int style, int color);

/******************************************************************************
 * File Writing Utilities
 ******************************************************************************/

/**
 * Write an INI file section header
 * @param file - Open file handle
 * @param sectionName - Name of the section
 * @return SUCCESS or error code
 */
int WriteINISection(FILE *file, const char *sectionName);

/**
 * Write a key-value pair to an INI file
 * @param file - Open file handle
 * @param key - Key name
 * @param format - Printf-style format string for the value
 * @param ... - Variable arguments for the format string
 * @return SUCCESS or error code
 */
int WriteINIValue(FILE *file, const char *key, const char *format, ...);

/**
 * Write a double value to an INI file with specified precision
 * @param file - Open file handle
 * @param key - Key name
 * @param value - Double value to write
 * @param precision - Number of decimal places
 * @return SUCCESS or error code
 */
int WriteINIDouble(FILE *file, const char *key, double value, int precision);

//==============================================================================
// Common Utility Functions
//==============================================================================

/**
 * Get human-readable error string for an error code
 * @param errorCode - Error code from any module in the system
 * @return Pointer to static error string (do not free)
 */
const char* GetErrorString(int errorCode);

/**
 * Trim leading and trailing whitespace from a string in-place
 * @param str - String to trim (modified in-place)
 * @return Pointer to the trimmed string (same as input or advanced past leading spaces)
 */
char* TrimWhitespace(char *str);

/**
 * Duplicate a string (portable version of strdup)
 * @param s - String to duplicate
 * @return Newly allocated copy of the string, or NULL on failure (caller must free)
 */
char* my_strdup(const char* s);

/**
 * Thread-safe tokenizer (portable version of strtok_r)
 * @param s - String to tokenize (NULL to continue previous tokenization)
 * @param delim - Delimiter characters
 * @param saveptr - Pointer to maintain tokenization state between calls
 * @return Next token or NULL if no more tokens
 */
char *my_strtok_r(char *s, const char *delim, char **saveptr);

/**
 * Get current timestamp using high-resolution timer
 * @return Current time in seconds since an arbitrary reference point
 */
double GetTimestamp(void);

/**
 * Format elapsed time as HH:MM:SS string
 * @param seconds - Elapsed time in seconds
 * @param buffer - Output buffer for formatted string
 * @param bufferSize - Size of output buffer
 */
void FormatTimeString(double seconds, char *buffer, int bufferSize);

/**
 * Format a time_t timestamp as a human-readable string
 * @param timestamp - Unix timestamp to format
 * @param buffer - Output buffer for formatted string
 * @param bufferSize - Size of output buffer
 */
void FormatTimestamp(time_t timestamp, char *buffer, int bufferSize);

/**
 * Check if a file exists
 * @param filename - Path to the file to check
 * @return 1 if file exists, 0 if not
 */
int FileExists(const char *filename);

/**
 * Create a directory path (including parent directories if needed)
 * @param path - Directory path to create
 * @return SUCCESS or error code
 */
int CreateDirectoryPath(const char *path);

/**
 * Get the directory containing the executable
 * @param path - Buffer to receive the directory path
 * @param pathSize - Size of the path buffer
 * @return SUCCESS or error code
 */
int GetExecutableDirectory(char *path, int pathSize);

/**
 * Create a timestamped directory
 * @param baseDir - Base directory path
 * @param prefix - Optional prefix for the timestamp (can be NULL)
 * @param resultPath - Buffer to receive the created directory path
 * @param resultPathSize - Size of resultPath buffer
 * @return SUCCESS or error code
 */
int CreateTimestampedDirectory(const char *baseDir, const char *prefix, 
                              char *resultPath, int resultPathSize);

/******************************************************************************
 * UI Helper Functions (implemented in utils.c)
 ******************************************************************************/

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
 * @param controls - array of tab-specific controls
 * @param numControls - number of tab-specific controls
 */
void DimCapacityExperimentControls(int mainPanel, int tabPanel, int dim, int *controls, int numControls);

//==============================================================================
// Common Constants
//==============================================================================

// Buffer sizes
#define SMALL_BUFFER_SIZE       64
#define MEDIUM_BUFFER_SIZE      256
#define LARGE_BUFFER_SIZE       1024
#define HUGE_BUFFER_SIZE        4096

#ifndef TSQ_INFINITE_TIMEOUT
#define TSQ_INFINITE_TIMEOUT    -1
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
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