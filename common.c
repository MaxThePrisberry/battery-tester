/******************************************************************************
 * common.c
 * 
 * Implementation of common utility functions declared in common.h
 ******************************************************************************/

#include "common.h"
#include "biologic_dll.h"  // For BL_GetErrorString
#include "psb10000_dll.h"  // For PSB_GetErrorString
#include "teensy_dll.h"    // For TNY_GetErrorString
#include "dtb4848_dll.h"   // For DTB_GetErrorString
#include "BatteryTester.h" // For UI control IDs
#include "logging.h"       // For LogWarning
#include <errno.h>         // For errno
#include <limits.h>        // For INT_MAX, INT_MIN
#include <ctype.h>         // For isspace
#include <stdint.h>        // For intptr_t

//==============================================================================
// Platform-specific includes (formerly in utils.h)
//==============================================================================
#ifdef _CVI_
    // LabWindows/CVI has its own directory functions
    // No additional includes needed
#elif defined(_WIN32)
    #include <direct.h>    // For _mkdir
    #include <io.h>        // For _access
#else
    #include <unistd.h>    // For access, getcwd
    #include <sys/stat.h>  // For mkdir
#endif

/******************************************************************************
 * Error Handling
 ******************************************************************************/

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
        case ERR_NOT_CONNECTED: return "Device not connected";
        
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
    
    // Check if it's a BioLogic error (-2000 range)
    if (errorCode <= ERR_BASE_BIOLOGIC && errorCode > (ERR_BASE_BIOLOGIC - 1000)) {
        return BIO_GetErrorString(errorCode);
    }
    
    // Check if it's a PSB error (-3000 range)
    if (errorCode <= ERR_BASE_PSB && errorCode > (ERR_BASE_PSB - 1000)) {
        return PSB_GetErrorString(errorCode);
    }
    
    // Check if it's a DTB error (-8000 range)
    if (errorCode <= ERR_BASE_DTB && errorCode > (ERR_BASE_DTB - 1000)) {
        return DTB_GetErrorString(errorCode);
    }
    
    // Check if it's a TNY (Teensy) error (-9000 range)
    if (errorCode <= ERR_BASE_TNY && errorCode > (ERR_BASE_TNY - 1000)) {
        return TNY_GetErrorString(errorCode);
    }
    
    // Check if it's a Test error (-4000 range)
    if (errorCode <= ERR_BASE_TEST && errorCode > (ERR_BASE_TEST - 1000)) {
        return "Test execution error";
    }
    
    // Check if it's a File error (-6000 range)
    if (errorCode <= ERR_BASE_FILE && errorCode > (ERR_BASE_FILE - 1000)) {
        return "File operation error";
    }
    
    // Default
    return "Unknown error";
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

char* my_strdup(const char* s) {
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

char *my_strtok_r(char *s, const char *delim, char **saveptr) {
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
    
    snprintf(buffer, bufferSize, "%02d:%02d:%02d", hours, minutes, secs);
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
        // LabWindows/CVI specific implementation
        // Temporarily disable runtime error checking for MakeDir
        int prevSetting = SetBreakOnLibraryErrors(0);
        
        // Try to create the directory
        int result = MakeDir(path);
        
        // Restore previous error checking setting
        SetBreakOnLibraryErrors(prevSetting);
        
        if (result == 0) {
            // Successfully created
            return SUCCESS;
        } else if (result == -9) {
            // Error -9 specifically means directory already exists
            // This is actually success for our purposes
            return SUCCESS;
        } else {
            // Some other error occurred
            // Common errors:
            // -1: Path not found (parent directory doesn't exist)
            // -2: Access denied
            // -3: Invalid path
            LogDebug("MakeDir failed with error code: %d for path: %s", result, path);
            return ERR_BASE_FILE - 1;
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
        strncpy(path, fullPath, pathSize);
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
 * UI Control Array Dimming Functions
 ******************************************************************************/

void DimControlArray(int panel, int arrayID, int dim) {
    // Manual approach - handle each control array by ID
    // Since we don't have the exact API for iterating control arrays,
    // we'll manually dim the controls that belong to each array
    
    switch (arrayID) {
        case BATTERY_CONSTANTS_ARR:
            // Battery constants controls - voltage and current settings
            SetCtrlAttribute(panel, PANEL_NUM_SET_CHARGE_V, ATTR_DIMMED, dim);
            SetCtrlAttribute(panel, PANEL_NUM_SET_DISCHARGE_V, ATTR_DIMMED, dim);
            SetCtrlAttribute(panel, PANEL_NUM_SET_CHARGE_I, ATTR_DIMMED, dim);
            SetCtrlAttribute(panel, PANEL_NUM_SET_DISCHARGE_I, ATTR_DIMMED, dim);
            break;
            
        case MANUAL_CONTROL_ARR:
            // Manual control controls - test buttons and remote mode toggle
            SetCtrlAttribute(panel, PANEL_TOGGLE_REMOTE_MODE, ATTR_DIMMED, dim);
            SetCtrlAttribute(panel, PANEL_BTN_TEST_PSB, ATTR_DIMMED, dim);
            SetCtrlAttribute(panel, PANEL_BTN_TEST_BIOLOGIC, ATTR_DIMMED, dim);
            break;
		
		case DTB_CONTROL_ARR:
			// DTB controls
			SetCtrlAttribute(panel, PANEL_NUM_DTB_1_SETPOINT, ATTR_DIMMED, dim);
			SetCtrlAttribute(panel, PANEL_BTN_DTB_1_RUN_STOP, ATTR_DIMMED, dim);
			break;
            
        default:
            LogWarning("DimControlArray: Unknown control array ID: %d", arrayID);
            break;
    }
}

void DimExperimentControls(int mainPanel, int tabPanel, int dim, int *controls, int numControls) {
    // Dim control arrays on main panel
    DimControlArray(mainPanel, BATTERY_CONSTANTS_ARR, dim);
    DimControlArray(mainPanel, MANUAL_CONTROL_ARR, dim);
	DimControlArray(mainPanel, DTB_CONTROL_ARR, dim);
    
    // Lock/unlock tab control - dim all tabs except the current one
    int numTabs;
    GetNumTabPages(mainPanel, PANEL_EXPERIMENTS, &numTabs);
    
    if (dim) {
        // Get current tab index
        int currentTab;
        GetActiveTabPage(mainPanel, PANEL_EXPERIMENTS, &currentTab);
        
        // Dim all other tabs
        for (int i = 0; i < numTabs; i++) {
            if (i != currentTab) {
                SetTabPageAttribute(mainPanel, PANEL_EXPERIMENTS, i, ATTR_DIMMED, 1);
            }
        }
    } else {
        // Re-enable all tabs
        for (int i = 0; i < numTabs; i++) {
            SetTabPageAttribute(mainPanel, PANEL_EXPERIMENTS, i, ATTR_DIMMED, 0);
        }
    }
    
    // Dim specific controls
    for (int i = 0; i < numControls; i++) {
        if (controls[i] > 0) {
            SetCtrlAttribute(tabPanel, controls[i], ATTR_DIMMED, dim);
        }
    }
}

/******************************************************************************
 * Directory Utilities
 ******************************************************************************/

int CreateTimestampedDirectory(const char *baseDir, const char *prefix, 
                              char *resultPath, int resultPathSize) {
    if (!baseDir || !resultPath || resultPathSize <= 0) {
        return ERR_NULL_POINTER;
    }
    
    // Get current time
    time_t now = time(NULL);
    struct tm *timeinfo = localtime(&now);
    char timestamp[64];
    
    // Format: YYYYMMDD_HHMMSS
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", timeinfo);
    
    // Build full path
    if (prefix && strlen(prefix) > 0) {
        snprintf(resultPath, resultPathSize, "%s%s%s_%s", 
                 baseDir, PATH_SEPARATOR, prefix, timestamp);
    } else {
        snprintf(resultPath, resultPathSize, "%s%s%s", 
                 baseDir, PATH_SEPARATOR, timestamp);
    }
    
    // Create the directory
    return CreateDirectoryPath(resultPath);
}

/******************************************************************************
 * Graph Utility Functions
 ******************************************************************************/

void ClearAllGraphs(int panel, const int graphs[], int numGraphs) {
    for (int i = 0; i < numGraphs; i++) {
        DeleteGraphPlot(panel, graphs[i], -1, VAL_DELAYED_DRAW);
    }
}

void ConfigureGraph(int panel, int graph, const char *title, const char *xLabel, 
                   const char *yLabel, double yMin, double yMax) {
    SetCtrlAttribute(panel, graph, ATTR_LABEL_TEXT, title);
    SetCtrlAttribute(panel, graph, ATTR_XNAME, xLabel);
    SetCtrlAttribute(panel, graph, ATTR_YNAME, yLabel);
    SetAxisScalingMode(panel, graph, VAL_LEFT_YAXIS, VAL_MANUAL, yMin, yMax);
    SetAxisScalingMode(panel, graph, VAL_BOTTOM_XAXIS, VAL_AUTOSCALE, 0.0, 0.0);
}

void PlotDataPoint(int panel, int graph, double x, double y, int style, int color) {
    PlotPoint(panel, graph, x, y, style, color);
}

/******************************************************************************
 * File Writing Utilities
 ******************************************************************************/

int WriteINISection(FILE *file, const char *sectionName) {
    if (!file || !sectionName) return ERR_NULL_POINTER;
    
    if (fprintf(file, "[%s]\n", sectionName) < 0) {
        return ERR_BASE_FILE;
    }
    
    return SUCCESS;
}

int WriteINIValue(FILE *file, const char *key, const char *format, ...) {
    if (!file || !key || !format) return ERR_NULL_POINTER;
    
    // Write key
    if (fprintf(file, "%s=", key) < 0) {
        return ERR_BASE_FILE;
    }
    
    // Write value
    va_list args;
    va_start(args, format);
    int result = vfprintf(file, format, args);
    va_end(args);
    
    if (result < 0) {
        return ERR_BASE_FILE;
    }
    
    // Write newline
    if (fprintf(file, "\n") < 0) {
        return ERR_BASE_FILE;
    }
    
    return SUCCESS;
}

int WriteINIDouble(FILE *file, const char *key, double value, int precision) {
    if (!file || !key) return ERR_NULL_POINTER;
    
    char format[32];
    snprintf(format, sizeof(format), "%%.%df", precision);
    
    return WriteINIValue(file, key, format, value);
}