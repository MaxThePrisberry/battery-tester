/******************************************************************************
 * utils.c
 * 
 * Implementation of common utility functions declared in common.h
 ******************************************************************************/

#include "common.h"
#include "utils.h"
#include "biologic_dll.h"  // For BL_GetErrorString
#include "psb10000_dll.h"  // For PSB_GetErrorString
#include <errno.h>  // For errno
#include <limits.h> // For INT_MAX, INT_MIN
#include <ctype.h>  // For isspace
#include <stdint.h> // For intptr_t

/******************************************************************************
 * Error Handling
 ******************************************************************************/

/******************************************************************************
 * Error Handling
 ******************************************************************************/

// Thread-local storage for last error
// Note: __thread might not be supported in all compilers
// Using static for now - not thread-safe but compatible
static int g_lastErrorCode = SUCCESS;
static char g_lastErrorMessage[MAX_ERROR_MSG_LENGTH] = {0};

const char* GetErrorString(int errorCode) {
    // Handle common/system errors first
    switch(errorCode) {
        case SUCCESS: return "Success";
        
        // System errors (-1000 range)
        case ERR_INVALID_PARAMETER: return "Invalid parameter";
        case ERR_NULL_POINTER: return "Null pointer";
        case ERR_OUT_OF_MEMORY: return "Out of memory";
        case ERR_NOT_INITIALIZED: return "Not initialized";
        case ERR_ALREADY_INITIALIZED: return "Already initialized";
        case ERR_TIMEOUT: return "Operation timed out";
        case ERR_OPERATION_FAILED: return "Operation failed";
        case ERR_NOT_SUPPORTED: return "Operation not supported";
        case ERR_INVALID_STATE: return "Invalid state";
        case ERR_COMM_FAILED: return "Communication failed";
        
        // Queue-specific errors
        case ERR_QUEUE_FULL: return "Command queue is full";
        case ERR_QUEUE_EMPTY: return "Command queue is empty";
        case ERR_QUEUE_TIMEOUT: return "Queue operation timed out";
        case ERR_QUEUE_NOT_INIT: return "Queue not initialized";
		case ERR_CANCELLED: return "Operation was cancelled";
        
        // UI errors (-5000 range)
        case ERR_UI: return "UI error";
        
        // Thread errors (-7000 range)
        case ERR_THREAD_CREATE: return "Failed to create thread";
        case ERR_THREAD_POOL: return "Thread pool error";
        case ERR_THREAD_SYNC: return "Thread synchronization error";
    }
    
    // Check if it's a BioLogic error (-1 to -405 range)
    if (errorCode >= -405 && errorCode <= -1) {
        return BL_GetErrorString(errorCode);
    }
    
    // Check if it's a PSB error (-3000 range)
    if (errorCode <= ERR_BASE_PSB && errorCode > (ERR_BASE_PSB - 100)) {
        return PSB_GetErrorString(errorCode);
    }
    
    // Default
    return "Unknown error";
}

void ClearLastError(void) {
    g_lastErrorCode = SUCCESS;
    g_lastErrorMessage[0] = '\0';
}

void SetLastErrorMessage(int errorCode, const char *format, ...) {
    g_lastErrorCode = errorCode;
    
    if (format) {
        va_list args;
        va_start(args, format);
        vsnprintf(g_lastErrorMessage, sizeof(g_lastErrorMessage), format, args);
        va_end(args);
    } else {
        // GetErrorString is provided by biologic_dll module
        SAFE_STRCPY(g_lastErrorMessage, GetErrorString(errorCode), sizeof(g_lastErrorMessage));
    }
}

/******************************************************************************
 * String Utilities
 ******************************************************************************/

char* TrimWhitespace(char *str) {
    if (!str) return NULL;
    
    // Trim leading whitespace
    while (isspace((unsigned char)*str)) str++;
    
    // All spaces?
    if (*str == 0) return str;
    
    // Trim trailing whitespace
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    
    // Write new null terminator
    end[1] = '\0';
    
    return str;
}

int ParseDouble(const char *str, double *value) {
    if (!str || !value) return ERR_NULL_POINTER;
    
    char *endptr;
    errno = 0;
    
    *value = strtod(str, &endptr);
    
    if (errno != 0 || endptr == str || *endptr != '\0') {
        return ERR_INVALID_PARAMETER;
    }
    
    return SUCCESS;
}

int ParseInt(const char *str, int *value) {
    if (!str || !value) return ERR_NULL_POINTER;
    
    char *endptr;
    errno = 0;
    
    long temp = strtol(str, &endptr, 10);
    
    if (errno != 0 || endptr == str || *endptr != '\0') {
        return ERR_INVALID_PARAMETER;
    }
    
    if (temp > INT_MAX || temp < INT_MIN) {
        return ERR_INVALID_PARAMETER;
    }
    
    *value = (int)temp;
    return SUCCESS;
}

/******************************************************************************
 * Time Utilities
 ******************************************************************************/

double GetTimestamp(void) {
    return Timer();
}

void FormatTimeString(double seconds, char *buffer, int bufferSize) {
    if (!buffer || bufferSize <= 0) return;
    
    int hours = (int)(seconds / 3600);
    int minutes = (int)((seconds - hours * 3600) / 60);
    int secs = (int)(seconds - hours * 3600 - minutes * 60);
    
    SAFE_SPRINTF(buffer, bufferSize, "%02d:%02d:%02d", hours, minutes, secs);
}

void FormatTimestamp(time_t timestamp, char *buffer, int bufferSize) {
    if (!buffer || bufferSize <= 0) return;
    
    struct tm *timeinfo = localtime(&timestamp);
    strftime(buffer, bufferSize, "%Y-%m-%d %H:%M:%S", timeinfo);
}

/******************************************************************************
 * File Utilities
 ******************************************************************************/

int FileExists(const char *filename) {
    if (!filename) return 0;
    
    FILE *file = fopen(filename, "r");
    if (file) {
        fclose(file);
        return 1;
    }
    return 0;
}

int CreateDirectoryPath(const char *path) {
    if (!path) return ERR_NULL_POINTER;
    
    #ifdef _CVI_
        // LabWindows/CVI provides MakeDir function
        if (MakeDir(path) == 0 || GetFileAttrs(path, NULL, NULL, NULL, NULL) >= 0) {
            return SUCCESS;
        }
    #elif defined(_WIN32)
        if (_mkdir(path) == 0 || errno == EEXIST) {
            return SUCCESS;
        }
    #else
        if (mkdir(path, 0755) == 0 || errno == EEXIST) {
            return SUCCESS;
        }
    #endif
    
    return ERR_BASE_FILE - 1;
}

int GetExecutableDirectory(char *path, int pathSize) {
    if (!path || pathSize <= 0) return ERR_NULL_POINTER;
    
    #ifdef _CVI_
        // LabWindows/CVI specific implementation
        char fullPath[MAX_PATH_LENGTH];
        if (GetProjectDir(fullPath) < 0) {
            // Fallback to current directory
            if (GetDir(fullPath) < 0) {
                return ERR_OPERATION_FAILED;
            }
        }
        SAFE_STRCPY(path, fullPath, pathSize);
    #elif defined(_WIN32)
        char fullPath[MAX_PATH_LENGTH];
        if (GetModuleFileName(NULL, fullPath, MAX_PATH_LENGTH) == 0) {
            return ERR_OPERATION_FAILED;
        }
        
        // Remove filename to get directory
        char *lastSlash = strrchr(fullPath, '\\');
        if (lastSlash) {
            *lastSlash = '\0';
        }
        
        SAFE_STRCPY(path, fullPath, pathSize);
    #else
        if (getcwd(path, pathSize) == NULL) {
            return ERR_OPERATION_FAILED;
        }
    #endif
    
    return SUCCESS;
}

/******************************************************************************
 * UI Helper Functions (Deferred Call Implementations)
 ******************************************************************************/

void UpdateUIString(void *panel, void *control, void *text) {
    int panelHandle = (int)(intptr_t)panel;
    int controlID = (int)(intptr_t)control;
    char *textStr = (char*)text;
    
    if (panelHandle > 0 && controlID > 0 && textStr) {
        // For text boxes, the logging module handles them specifically
        // For other controls, just set the value
        SetCtrlVal(panelHandle, controlID, textStr);
        
        // Free the allocated string
        free(textStr);
    }
}

void UpdateUINumeric(void *panel, void *control, void *value) {
    int panelHandle = (int)(intptr_t)panel;
    int controlID = (int)(intptr_t)control;
    double *numValue = (double*)value;
    
    if (panelHandle > 0 && controlID > 0 && numValue) {
        SetCtrlVal(panelHandle, controlID, *numValue);
        
        // Free the allocated value
        free(numValue);
    }
}

void EnablePanel(int panel, int enable) {
    if (panel > 0) {
        SetPanelAttribute(panel, ATTR_DIMMED, !enable);
    }
}

void ShowBusyCursor(int show) {
    SetWaitCursor(show);
}

/******************************************************************************
 * Thread Synchronization Helpers
 ******************************************************************************/

int WaitForCondition(int (*condition)(void), double timeoutSeconds) {
    if (!condition) return ERR_NULL_POINTER;
    
    double startTime = Timer();
    
    while (!condition()) {
        if ((Timer() - startTime) > timeoutSeconds) {
            return ERR_TIMEOUT;
        }
        
        ProcessSystemEvents();
        Delay(0.01);  // 10ms polling interval
    }
    
    return SUCCESS;
}