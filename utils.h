/******************************************************************************
 * utils.h
 * 
 * Header for common utility functions implementation
 * Note: Function declarations are in common.h, this header is for 
 * implementation-specific details only
 ******************************************************************************/

#ifndef COMMON_UTILS_H
#define COMMON_UTILS_H

//==============================================================================
// Implementation Notes
//==============================================================================

/*
 * This header is primarily for internal use by utils.c
 * All public function declarations are in common.h
 * 
 * This file can be used for:
 * - Internal constants specific to the implementation
 * - Internal data structures
 * - Platform-specific includes needed by the implementation
 */

//==============================================================================
// Platform-Specific Includes (for implementation only)
//==============================================================================

#ifdef _CVI_
    // LabWindows/CVI has its own directory functions
    // No additional includes needed
#elif defined(_WIN32)
    #include <direct.h>  // For _mkdir
    #include <io.h>      // For _access
#else
    #include <unistd.h>      // For access, getcwd
    #include <sys/stat.h>    // For mkdir
#endif

//==============================================================================
// Internal Constants
//==============================================================================

// None currently needed - all constants are in common.h

//==============================================================================
// Internal UI Control IDs for DimControlArray
//==============================================================================

// These are needed for the implementation of DimControlArray
// They map array IDs to specific controls
// Note: These values should match what's defined in BatteryTester.h

//==============================================================================
// End of Header
//==============================================================================

#endif // COMMON_UTILS_H