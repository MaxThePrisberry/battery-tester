/******************************************************************************
 * logging.h
 * 
 * Log Operations Module Header
 * Provides logging functionality for the Battery Tester application
 ******************************************************************************/

#ifndef LOGGING_H
#define LOGGING_H

//==============================================================================
// Device Identifiers
//==============================================================================
typedef enum {
    LOG_DEVICE_NONE = 0,    // No device prefix
    LOG_DEVICE_PSB,         // [PSB] prefix
    LOG_DEVICE_BIO,         // [BIO] prefix
	LOG_DEVICE_DTB,         // [DTB] prefix
	LOG_DEVICE_TNY          // [TNY] prefix
} LogDevice;

//==============================================================================
// Public Function Declarations
//==============================================================================


/******************************************************************************
 * Enhanced Logging Functions with Device Support
 ******************************************************************************/

/**
 * Log an informational message with optional device prefix
 * @param device - Device identifier (LOG_DEVICE_NONE, LOG_DEVICE_PSB, LOG_DEVICE_BIO)
 * @param format - printf-style format string
 * @param ... - Variable arguments
 */
void LogMessageEx(LogDevice device, const char *format, ...);

/**
 * Log an error message with optional device prefix
 * @param device - Device identifier
 * @param format - printf-style format string
 * @param ... - Variable arguments
 */
void LogErrorEx(LogDevice device, const char *format, ...);

/**
 * Log a warning message with optional device prefix
 * @param device - Device identifier
 * @param format - printf-style format string
 * @param ... - Variable arguments
 */
void LogWarningEx(LogDevice device, const char *format, ...);

/**
 * Log a debug message with optional device prefix
 * @param device - Device identifier
 * @param format - printf-style format string
 * @param ... - Variable arguments
 */
void LogDebugEx(LogDevice device, const char *format, ...);

/******************************************************************************
 * Original Logging Functions (now call extended versions with LOG_DEVICE_NONE)
 ******************************************************************************/

/**
 * Log an informational message
 * @param format - printf-style format string
 * @param ... - Variable arguments
 */
void LogMessage(const char *format, ...);

/**
 * Log an error message
 * @param format - printf-style format string
 * @param ... - Variable arguments
 */
void LogError(const char *format, ...);

/**
 * Log a warning message
 * @param format - printf-style format string
 * @param ... - Variable arguments
 */
void LogWarning(const char *format, ...);

/**
 * Log a debug message (only if debug mode is enabled)
 * @param format - printf-style format string
 * @param ... - Variable arguments
 */
void LogDebug(const char *format, ...);

/******************************************************************************
 * Special Purpose Logging Functions
 ******************************************************************************/

/**
 * Log a startup message (used before full initialization)
 * @param message - Message to log
 */
void LogStartupMessage(const char *message);

/******************************************************************************
 * Configuration Functions
 ******************************************************************************/

/**
 * Enable or disable logging to file
 * @param enable - 1 to enable file logging, 0 to disable
 */
void SetLogToFile(int enable);

/**
 * Enable or disable logging to UI textbox
 * @param enable - 1 to enable UI logging, 0 to disable
 */
void SetLogToUI(int enable);

/**
 * Clear the log display in the UI
 */
void ClearLogDisplay(void);

/**
 * Get the full path to the log file
 * @return Pointer to static string containing log file path
 */
const char* GetLogFilePath(void);

/**
 * Register logging cleanup function with atexit()
 * Should be called once during application initialization
 */
void RegisterLoggingCleanup(void);

/**
 * Create log file in current directory
 * Useful if the default location fails
 * @return SUCCESS if created, error code otherwise
 */
int CreateLogFileInCurrentDir(void);

//==============================================================================
// End of Header
//==============================================================================

#endif // LOGGING_H