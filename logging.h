/******************************************************************************
 * logging.h
 * 
 * Log Operations Module Header
 * Provides logging functionality for the Battery Tester application
 ******************************************************************************/

#ifndef LOGGING_H
#define LOGGING_H

//==============================================================================
// Public Function Declarations
//==============================================================================

// Note: Core logging functions are declared in Common.h
// This header provides additional logging-specific functionality

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