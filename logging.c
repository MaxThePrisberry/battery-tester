/******************************************************************************
 * logging.c
 * 
 * Log Operations Module
 * Handles logging throughout the application to both UI and file
 ******************************************************************************/

#include "common.h"
#include "logging.h"
#include "BatteryTester.h"
#include <stdarg.h>
#include <stdint.h> // For intptr_t
#include <errno.h>  // For errno

#ifdef _WIN32
    #ifndef _CVI_
        #include <direct.h>  // For getcwd
    #endif
#else
    #include <unistd.h>  // For getcwd
#endif

/******************************************************************************
 * Module Constants
 ******************************************************************************/
#define TAB_WIDTH               4       // Number of spaces to represent a tab
#define MAX_LOG_LINE_LEN        2048    // Max length for a single formatted log line
#define MAX_LINES_PER_CALL      10      // Max number of lines to handle in one LogMessage call
#define LOG_FILE_NAME           "BatteryTester.log"
#define MAX_LOG_FILE_SIZE       (10 * 1024 * 1024)  // 10MB max log file size

/******************************************************************************
 * Module Variables
 ******************************************************************************/
static FILE *g_logFile = NULL;
static int g_logToFile = 1;
static int g_logToUI = 1;
static CmtTSQHandle g_logQueue = 0;
static int g_loggingInitialized = 0;
static char g_actualLogPath[MAX_PATH_LENGTH] = {0};  // Store the actual log file path

// Log level names for display
static const char* g_logLevelNames[] = {
    "INFO",
    "WARN",
    "ERROR",
    "DEBUG"
};

// Device names for display
static const char* g_deviceNames[] = {
    "",     // LOG_DEVICE_NONE - no prefix
    "PSB",  // LOG_DEVICE_PSB
    "BIO"   // LOG_DEVICE_BIO
};

typedef enum {
    LOG_LEVEL_INFO = 0,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_DEBUG
} LogLevel;

// Structure for deferred UI update
typedef struct {
    int panel;
    int control;
    char *text;
} UIUpdateData;

/******************************************************************************
 * Internal Function Prototypes
 ******************************************************************************/
static void LogMessageInternalEx(LogDevice device, LogLevel level, const char *format, va_list args);
static void WriteToLogFile(const char *timestamp, const char *deviceStr, const char *levelStr, const char *message);
static void WriteToUI(const char *message);
static char* ProcessTabs(const char *input);
static int InitializeLogging(void);
static void CleanupLogging(void);
static void CVICALLBACK DeferredTextBoxUpdate(void *callbackData);

/******************************************************************************
 * String Utility Functions (made static)
 ******************************************************************************/
static char* my_strdup(const char* s) {
    if (s == NULL) {
        return NULL;
    }
    
    size_t len = strlen(s) + 1;
    char* new_s = (char*)malloc(len);
    if (new_s == NULL) {
        return NULL;
    }
    
    memcpy(new_s, s, len);
    return new_s;
}

static char *my_strtok_r(char *s, const char *delim, char **saveptr) {
    char *token;

    if (s == NULL) {
        s = *saveptr;
    }

    // Skip leading delimiters
    s += strspn(s, delim);

    if (*s == '\0') {
        *saveptr = s;
        return NULL;
    }

    token = s;

    // Find the end of the token
    s = strpbrk(token, delim);

    if (s == NULL) {
        *saveptr = token + strlen(token);
    } else {
        *s = '\0';
        *saveptr = s + 1;
    }

    return token;
}

/******************************************************************************
 * Deferred UI Update Callback
 ******************************************************************************/
static void CVICALLBACK DeferredTextBoxUpdate(void *callbackData) {
    UIUpdateData *data = (UIUpdateData*)callbackData;
    
    if (data && data->panel > 0 && data->control > 0 && data->text) {
        InsertTextBoxLine(data->panel, data->control, -1, data->text);
        
        // Auto-scroll to the end
        int totalLines;
        GetNumTextBoxLines(data->panel, data->control, &totalLines);
        if (totalLines > 0) {
            SetCtrlAttribute(data->panel, data->control, 
                           ATTR_FIRST_VISIBLE_LINE, totalLines);
        }
        
        // Clean up
        free(data->text);
        free(data);
    }
}

/******************************************************************************
 * Initialization and Cleanup
 ******************************************************************************/

static int InitializeLogging(void) {
    if (g_loggingInitialized) {
        return SUCCESS;
    }
    
    // Create thread-safe queue for log messages
    if (CmtNewTSQ(100, sizeof(char*), 0, &g_logQueue) < 0) {
        return ERR_THREAD_SYNC;
    }
    
    // Mark as initialized early so we can use logging functions
    g_loggingInitialized = 1;
    
    // Open log file
    if (g_logToFile) {
        char logPath[MAX_PATH_LENGTH];
        
        // Try to get executable directory
        int dirResult = GetExecutableDirectory(logPath, sizeof(logPath));
        if (dirResult != SUCCESS) {
            // Fall back to current directory
            #ifdef _CVI_
                GetDir(logPath);
            #else
                getcwd(logPath, sizeof(logPath));
            #endif
        }
        
        // Ensure path ends with separator
        size_t len = strlen(logPath);
        if (len > 0 && logPath[len-1] != PATH_SEPARATOR_CHAR) {
            strcat(logPath, PATH_SEPARATOR);
        }
        strcat(logPath, LOG_FILE_NAME);
        
        // Store the attempted log path
        SAFE_STRCPY(g_actualLogPath, logPath, sizeof(g_actualLogPath));
        
        // Debug: Print log file path
        #ifdef _DEBUG
        printf("Log file path: %s\n", logPath);
        #endif
        
        // Check if log file is too large
        if (FileExists(logPath)) {
            FILE *checkFile = fopen(logPath, "r");
            if (checkFile) {
                fseek(checkFile, 0, SEEK_END);
                long fileSize = ftell(checkFile);
                fclose(checkFile);
                
                // If too large, rename old log
                if (fileSize > MAX_LOG_FILE_SIZE) {
                    char backupPath[MAX_PATH_LENGTH];
                    SAFE_SPRINTF(backupPath, sizeof(backupPath), "%s.old", logPath);
                    remove(backupPath);
                    rename(logPath, backupPath);
                }
            }
        }
        
        // Try to open/create the log file
        g_logFile = fopen(logPath, "w");
        if (!g_logFile) {
            // Try creating in current directory as fallback
            g_logFile = fopen(LOG_FILE_NAME, "a");
            
            if (!g_logFile) {
                // Last resort - try temp directory
                #ifdef _WIN32
                    char tempPath[MAX_PATH_LENGTH];
                    GetTempPath(sizeof(tempPath), tempPath);
                    strcat(tempPath, LOG_FILE_NAME);
                    g_logFile = fopen(tempPath, "a");
                    if (g_logFile) {
                        SAFE_STRCPY(g_actualLogPath, tempPath, sizeof(g_actualLogPath));
                    }
                #endif
            } else {
                // Update actual path to current directory
                SAFE_STRCPY(g_actualLogPath, LOG_FILE_NAME, sizeof(g_actualLogPath));
            }
        }
        
        if (g_logFile) {
            // Write header
            time_t now = time(NULL);
            char timeStr[64];
            FormatTimestamp(now, timeStr, sizeof(timeStr));
            fprintf(g_logFile, "\n=== Battery Tester Log Started: %s ===\n", timeStr);
            fprintf(g_logFile, "Log file location: %s\n", g_actualLogPath);
            fflush(g_logFile);
            
            // Also show success in UI without popup
            if (g_mainPanelHandle > 0) {
                WriteToUI("[INFO] Log file created successfully");
                
                char logMsg[MEDIUM_BUFFER_SIZE];
                SAFE_SPRINTF(logMsg, sizeof(logMsg), "[INFO] Log file location: %s", g_actualLogPath);
                WriteToUI(logMsg);
            }
            
            #ifdef _DEBUG
            printf("Log file created at: %s\n", g_actualLogPath);
            #endif
        } else {
            // If we still can't open the log file, disable file logging
            g_logToFile = 0;
            
            // Output error to textbox instead of showing popup
            if (g_mainPanelHandle > 0) {
                WriteToUI("[WARNING] Could not create log file");
                
                char errorMsg[LARGE_BUFFER_SIZE];
                SAFE_SPRINTF(errorMsg, sizeof(errorMsg), 
                    "[WARNING] Failed to create log file at: %s", logPath);
                WriteToUI(errorMsg);
                
                WriteToUI("[WARNING] Logging to file has been disabled");
                WriteToUI("[INFO] Check that the directory exists and you have write permissions");
                WriteToUI("[INFO] Application will continue without file logging");
            }
            
            #ifdef _DEBUG
            printf("Failed to create log file at: %s\n", logPath);
            #endif
        }
    }
    
    return SUCCESS;
}

void LogStartupMessage(const char *message) {
    // This function can be called before full initialization
    if (g_mainPanelHandle > 0) {
        // Try to write directly to UI if panel exists
        char fullMsg[LARGE_BUFFER_SIZE];
        SAFE_SPRINTF(fullMsg, sizeof(fullMsg), "[STARTUP] %s", message);
        
        // Direct UI update if in main thread
        if (GetCurrentThreadId() == MainThreadId()) {
            InsertTextBoxLine(g_mainPanelHandle, PANEL_OUTPUT_TEXTBOX, -1, fullMsg);
            
            // Auto-scroll
            int totalLines;
            GetNumTextBoxLines(g_mainPanelHandle, PANEL_OUTPUT_TEXTBOX, &totalLines);
            if (totalLines > 0) {
                SetCtrlAttribute(g_mainPanelHandle, PANEL_OUTPUT_TEXTBOX, 
                               ATTR_FIRST_VISIBLE_LINE, totalLines);
            }
        }
    }
    
    // Also print to console
    #ifdef _DEBUG
    printf("%s\n", message);
    #endif
}

static void CleanupLogging(void) {
    if (!g_loggingInitialized) {
        return;
    }
    
    // Close log file
    if (g_logFile) {
        time_t now = time(NULL);
        char timeStr[64];
        FormatTimestamp(now, timeStr, sizeof(timeStr));
        fprintf(g_logFile, "=== Battery Tester Log Ended: %s ===\n", timeStr);
        fclose(g_logFile);
        g_logFile = NULL;
    }
    
    // Dispose of queue
    if (g_logQueue) {
        CmtDiscardTSQ(g_logQueue);
        g_logQueue = 0;
    }
    
    g_loggingInitialized = 0;
}

/******************************************************************************
 * Internal Logging Implementation
 ******************************************************************************/
static void LogMessageInternalEx(LogDevice device, LogLevel level, const char *format, va_list args) {
    char rawBuffer[MAX_LOG_LINE_LEN];
    char timeStr[64];
    time_t now;
    
    // Initialize logging if needed
    if (!g_loggingInitialized) {
        InitializeLogging();
    }
    
    // Format the message
    vsnprintf(rawBuffer, sizeof(rawBuffer) - 1, format, args);
    rawBuffer[sizeof(rawBuffer) - 1] = '\0';
    
    // Get timestamp
    now = time(NULL);
    FormatTimestamp(now, timeStr, sizeof(timeStr));
    
    // Get level string
    const char *levelStr = (level >= 0 && level < ARRAY_SIZE(g_logLevelNames)) 
                          ? g_logLevelNames[level] : "UNKNOWN";
    
    // Get device string
    const char *deviceStr = (device >= 0 && device < ARRAY_SIZE(g_deviceNames))
                           ? g_deviceNames[device] : "";
    
    // Write to log file
    if (g_logToFile && g_logFile) {
        WriteToLogFile(timeStr, deviceStr, levelStr, rawBuffer);
    }
    
    // Write to UI (if not debug or if debug mode is on)
    if (g_logToUI && g_mainPanelHandle > 0) {
        if (level != LOG_LEVEL_DEBUG || g_debugMode) {
            char uiMessage[MAX_LOG_LINE_LEN + 100];
            
            // Format message with level and device prefixes (level first)
            if (device != LOG_DEVICE_NONE && strlen(deviceStr) > 0) {
                SAFE_SPRINTF(uiMessage, sizeof(uiMessage), "[%s] [%s] %s", 
                           levelStr, deviceStr, rawBuffer);
            } else {
                SAFE_SPRINTF(uiMessage, sizeof(uiMessage), "[%s] %s", 
                           levelStr, rawBuffer);
            }
            
            WriteToUI(uiMessage);
        }
    }
    
    // Also write to console in debug builds
    #ifdef _DEBUG
    if (device != LOG_DEVICE_NONE && strlen(deviceStr) > 0) {
        fprintf(stderr, "[%s] %s [%s]: %s\n", timeStr, levelStr, deviceStr, rawBuffer);
    } else {
        fprintf(stderr, "[%s] %s: %s\n", timeStr, levelStr, rawBuffer);
    }
    #endif
}

static void WriteToLogFile(const char *timestamp, const char *deviceStr, const char *levelStr, const char *message) {
    if (!g_logFile) return;
    
    if (deviceStr && strlen(deviceStr) > 0) {
        fprintf(g_logFile, "%s [%s] [%s] %s\n", timestamp, levelStr, deviceStr, message);
    } else {
        fprintf(g_logFile, "%s [%s] %s\n", timestamp, levelStr, message);
    }
    
    fflush(g_logFile);  // Ensure immediate write
}

static void WriteToUI(const char *message) {
    if (g_mainPanelHandle <= 0) {
        return;
    }
    
    // Process tabs in the message
    char *processedMessage = ProcessTabs(message);
    if (!processedMessage) {
        return;
    }
    
    // Split by newlines and add to textbox
    char *processingString = my_strdup(processedMessage);
    if (!processingString) {
        free(processedMessage);
        return;
    }
    
    char *savePtr;
    char *line = my_strtok_r(processingString, "\n", &savePtr);
    int lineCount = 0;
    
    while (line != NULL && lineCount < MAX_LINES_PER_CALL) {
        // Thread-safe UI update
        if (GetCurrentThreadId() == MainThreadId()) {
            // We're in the main thread, update directly
            InsertTextBoxLine(g_mainPanelHandle, PANEL_OUTPUT_TEXTBOX, -1, line);
        } else {
            // We're in a background thread, use PostDeferredCall
            UIUpdateData *updateData = malloc(sizeof(UIUpdateData));
            if (updateData) {
                updateData->panel = g_mainPanelHandle;
                updateData->control = PANEL_OUTPUT_TEXTBOX;
                updateData->text = my_strdup(line);
                
                if (updateData->text) {
                    PostDeferredCall(DeferredTextBoxUpdate, updateData);
                } else {
                    free(updateData);
                }
            }
        }
        
        line = my_strtok_r(NULL, "\n", &savePtr);
        lineCount++;
    }
    
    // Auto-scroll to the end if in main thread
    if (GetCurrentThreadId() == MainThreadId()) {
        int totalLines;
        GetNumTextBoxLines(g_mainPanelHandle, PANEL_OUTPUT_TEXTBOX, &totalLines);
        if (totalLines > 0) {
            SetCtrlAttribute(g_mainPanelHandle, PANEL_OUTPUT_TEXTBOX, 
                           ATTR_FIRST_VISIBLE_LINE, totalLines);
        }
    }
    
    free(processingString);
    free(processedMessage);
}

static char* ProcessTabs(const char *input) {
    if (!input) return NULL;
    
    // Allocate buffer large enough for worst case (all tabs)
    size_t maxLen = strlen(input) * TAB_WIDTH + 1;
    char *output = (char*)malloc(maxLen);
    if (!output) return NULL;
    
    int i = 0, j = 0;
    while (input[i] != '\0' && j < maxLen - 1) {
        if (input[i] == '\t') {
            // Replace tab with spaces
            for (int k = 0; k < TAB_WIDTH && j < maxLen - 1; k++) {
                output[j++] = ' ';
            }
        } else {
            output[j++] = input[i];
        }
        i++;
    }
    output[j] = '\0';
    
    return output;
}

/******************************************************************************
 * Public Logging Functions - Extended versions with device support
 ******************************************************************************/
void LogMessageEx(LogDevice device, const char *format, ...) {
    va_list args;
    va_start(args, format);
    LogMessageInternalEx(device, LOG_LEVEL_INFO, format, args);
    va_end(args);
}

void LogErrorEx(LogDevice device, const char *format, ...) {
    va_list args;
    va_start(args, format);
    LogMessageInternalEx(device, LOG_LEVEL_ERROR, format, args);
    va_end(args);
}

void LogWarningEx(LogDevice device, const char *format, ...) {
    va_list args;
    va_start(args, format);
    LogMessageInternalEx(device, LOG_LEVEL_WARNING, format, args);
    va_end(args);
}

void LogDebugEx(LogDevice device, const char *format, ...) {
    if (!g_debugMode) return;
    
    va_list args;
    va_start(args, format);
    LogMessageInternalEx(device, LOG_LEVEL_DEBUG, format, args);
    va_end(args);
}

/******************************************************************************
 * Public Logging Functions - Original versions (no device prefix)
 ******************************************************************************/
void LogMessage(const char *format, ...) {
    va_list args;
    va_start(args, format);
    LogMessageInternalEx(LOG_DEVICE_NONE, LOG_LEVEL_INFO, format, args);
    va_end(args);
}

void LogError(const char *format, ...) {
    va_list args;
    va_start(args, format);
    LogMessageInternalEx(LOG_DEVICE_NONE, LOG_LEVEL_ERROR, format, args);
    va_end(args);
}

void LogWarning(const char *format, ...) {
    va_list args;
    va_start(args, format);
    LogMessageInternalEx(LOG_DEVICE_NONE, LOG_LEVEL_WARNING, format, args);
    va_end(args);
}

void LogDebug(const char *format, ...) {
    if (!g_debugMode) return;
    
    va_list args;
    va_start(args, format);
    LogMessageInternalEx(LOG_DEVICE_NONE, LOG_LEVEL_DEBUG, format, args);
    va_end(args);
}

/******************************************************************************
 * Logging Configuration Functions
 ******************************************************************************/
void SetLogToFile(int enable) {
    g_logToFile = enable;
    
    if (enable && !g_logFile && g_loggingInitialized) {
        // Re-open log file
        InitializeLogging();
    } else if (!enable && g_logFile) {
        // Close log file
        fclose(g_logFile);
        g_logFile = NULL;
    }
}

void SetLogToUI(int enable) {
    g_logToUI = enable;
}

void ClearLogDisplay(void) {
    if (g_mainPanelHandle > 0) {
        DeleteTextBoxLines(g_mainPanelHandle, PANEL_OUTPUT_TEXTBOX, 0, -1);
    }
}

const char* GetLogFilePath(void) {
    // If we have a stored path from successful initialization, return it
    if (g_actualLogPath[0] != '\0') {
        return g_actualLogPath;
    }
    
    // Otherwise return default location (current directory)
    return LOG_FILE_NAME;
}

/******************************************************************************
 * Module Registration (called from main)
 ******************************************************************************/
void RegisterLoggingCleanup(void) {
    // Register cleanup function to be called at exit
    atexit(CleanupLogging);
}

/******************************************************************************
 * Create Log File in Current Directory
 ******************************************************************************/
int CreateLogFileInCurrentDir(void) {
    if (g_logFile) {
        // Already have a log file open
        return SUCCESS;
    }
    
    // Try to create in current directory
    g_logFile = fopen(LOG_FILE_NAME, "a");
    if (g_logFile) {
        g_logToFile = 1;
        
        // Write header
        time_t now = time(NULL);
        char timeStr[64];
        FormatTimestamp(now, timeStr, sizeof(timeStr));
        fprintf(g_logFile, "\n=== Battery Tester Log Started: %s ===\n", timeStr);
        fprintf(g_logFile, "Log file location: %s (current directory)\n", LOG_FILE_NAME);
        fflush(g_logFile);
        
        return SUCCESS;
    }
    
    return ERR_OPERATION_FAILED;
}