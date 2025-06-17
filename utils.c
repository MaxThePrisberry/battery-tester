/******************************************************************************
 * utils.c
 * 
 * Implementation of common utility functions declared in common.h
 ******************************************************************************/

#include "common.h"
#include "utils.h"
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

// Note: GetErrorString is implemented in biologic_dll module
// We just provide these wrapper functions for module-specific errors

const char* GetBioLogicErrorString(int errorCode) {
    // Delegate to the main GetErrorString function
    return GetErrorString(errorCode);
}

const char* GetPSBErrorString(int errorCode) {
    // Delegate to the main GetErrorString function  
    return GetErrorString(errorCode);
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