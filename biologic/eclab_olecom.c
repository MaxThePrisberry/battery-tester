/******************************************************************************
 * eclab_olecom.c
 *
 * Implementation of EC-Lab OLE COM wrapper
 *
 * This file contains Windows COM automation code for interfacing with EC-Lab.
 * It uses the custom IEClabExe interface (NOT IDispatch) with direct vtable
 * calls for method invocation. BSTR string conversions and VARIANT type
 * management are still required for parameter passing.
 *
 * IMPORTANT: EC-Lab does not support IDispatch automation. This implementation
 * uses the custom IEClabExe interface defined in eclab_olecom_interface.h.
 ******************************************************************************/

#include "eclab_olecom.h"
#include "logging.h"
#include <oleauto.h>
#include <ocidl.h>      // For IProvideClassInfo
#include <tlhelp32.h>
#include <string.h>

/******************************************************************************
 * VARIANT Access Macros for LabWindows/CVI
 ******************************************************************************/

// LabWindows/CVI uses named unions in VARIANT structure (NONAMELESSUNION mode)
// Access pattern: variant.n1.n2.vt and variant.n1.n2.n3.lVal
#ifndef V_VT
#define V_VT(X)         ((X)->n1.n2.vt)
#endif
#ifndef V_I4
#define V_I4(X)         ((X)->n1.n2.n3.lVal)
#endif
#ifndef V_R8
#define V_R8(X)         ((X)->n1.n2.n3.dblVal)
#endif
#ifndef V_BOOL
#define V_BOOL(X)       ((X)->n1.n2.n3.boolVal)
#endif
#ifndef V_BSTR
#define V_BSTR(X)       ((X)->n1.n2.n3.bstrVal)
#endif
#ifndef V_ARRAY
#define V_ARRAY(X)      ((X)->n1.n2.n3.parray)
#endif

/******************************************************************************
 * Forward Declarations
 ******************************************************************************/

// Forward declare function from biologic_eclab.c to avoid circular includes
extern ECLabConnection* BIO_ECLAB_GetConnection(void);

/******************************************************************************
 * Internal Helper Functions
 ******************************************************************************/

/**
 * Check registry for COM server details
 */
static void DiagnoseCOMRegistration(const wchar_t *progId, CLSID *pClsid) {
    LogMessageEx(LOG_DEVICE_BIO, "");
    LogMessageEx(LOG_DEVICE_BIO, "=== COM Registration Diagnostics ===");

    // Convert CLSID to string
    LPOLESTR clsidStr = NULL;
    if (SUCCEEDED(StringFromCLSID(pClsid, &clsidStr))) {
        char clsidAscii[128];
        WideCharToMultiByte(CP_ACP, 0, clsidStr, -1, clsidAscii, sizeof(clsidAscii), NULL, NULL);
        LogMessageEx(LOG_DEVICE_BIO, "ProgID: EClabCOM.EClabExe");
        LogMessageEx(LOG_DEVICE_BIO, "CLSID: %s", clsidAscii);
        CoTaskMemFree(clsidStr);
    }

    // Check registry key for LocalServer32
    HKEY hKey;
    char keyPath[256];
    char clsidForRegistry[64];

    // Convert CLSID to ASCII string for registry path
    LPOLESTR clsidWStr = NULL;
    if (SUCCEEDED(StringFromCLSID(pClsid, &clsidWStr))) {
        WideCharToMultiByte(CP_ACP, 0, clsidWStr, -1, clsidForRegistry, sizeof(clsidForRegistry), NULL, NULL);
        CoTaskMemFree(clsidWStr);

        snprintf(keyPath, sizeof(keyPath), "CLSID\\%s\\LocalServer32", clsidForRegistry);

        if (RegOpenKeyExA(HKEY_CLASSES_ROOT, keyPath, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            char serverPath[MAX_PATH];
            DWORD bufferSize = sizeof(serverPath);
            DWORD type;

            if (RegQueryValueExA(hKey, NULL, NULL, &type, (LPBYTE)serverPath, &bufferSize) == ERROR_SUCCESS) {
                LogMessageEx(LOG_DEVICE_BIO, "Server path: %s", serverPath);

                // Check if file exists
                DWORD attr = GetFileAttributesA(serverPath);
                if (attr == INVALID_FILE_ATTRIBUTES) {
                    LogErrorEx(LOG_DEVICE_BIO, "WARNING: Server executable NOT FOUND at registered path!");
                } else {
                    LogMessageEx(LOG_DEVICE_BIO, "Server executable: EXISTS");
                }
            }
            RegCloseKey(hKey);
        } else {
            LogErrorEx(LOG_DEVICE_BIO, "WARNING: LocalServer32 registry key not found!");
            LogErrorEx(LOG_DEVICE_BIO, "This may indicate incomplete COM registration.");
        }
    }

    LogMessageEx(LOG_DEVICE_BIO, "===================================");
    LogMessageEx(LOG_DEVICE_BIO, "");
}

/**
 * Convert ASCII string to BSTR (wide string)
 */
static BSTR StringToBSTR(const char *str) {
    if (!str) return NULL;

    int len = MultiByteToWideChar(CP_ACP, 0, str, -1, NULL, 0);
    if (len == 0) return NULL;

    wchar_t *wstr = (wchar_t*)malloc(len * sizeof(wchar_t));
    if (!wstr) return NULL;

    MultiByteToWideChar(CP_ACP, 0, str, -1, wstr, len);
    BSTR bstr = SysAllocString(wstr);
    free(wstr);

    return bstr;
}

/**
 * Convert BSTR to ASCII string
 */
static int BSTRToString(BSTR bstr, char *str, int maxLen) {
    if (!bstr || !str || maxLen <= 0) return ERR_INVALID_PARAMETER;

    int len = WideCharToMultiByte(CP_ACP, 0, bstr, -1, NULL, 0, NULL, NULL);
    if (len == 0 || len > maxLen) return ECLAB_ERR_BSTR_CONVERSION;

    WideCharToMultiByte(CP_ACP, 0, bstr, -1, str, maxLen, NULL, NULL);
    return SUCCESS;
}

/**
 * Invoke IDispatch method by name
 */
static HRESULT InvokeMethod(IDispatch *pDisp, LPOLESTR methodName,
                           VARIANT *pResult, int numArgs, ...) {
    if (!pDisp) return E_POINTER;

    DISPID dispid;
    HRESULT hr = pDisp->lpVtbl->GetIDsOfNames(pDisp, &IID_NULL, &methodName,
                                               1, LOCALE_USER_DEFAULT, &dispid);
    if (FAILED(hr)) return hr;

    // Build parameter array (COM uses reverse order)
    VARIANT *pArgs = NULL;
    if (numArgs > 0) {
        pArgs = (VARIANT*)malloc(sizeof(VARIANT) * numArgs);
        if (!pArgs) return E_OUTOFMEMORY;

        va_list args;
        va_start(args, numArgs);
        for (int i = numArgs - 1; i >= 0; i--) {
            VariantInit(&pArgs[i]);
            VariantCopy(&pArgs[i], va_arg(args, VARIANT*));
        }
        va_end(args);
    }

    // Set up dispatch parameters
    DISPPARAMS params;
    params.cArgs = numArgs;
    params.rgvarg = pArgs;
    params.cNamedArgs = 0;
    params.rgdispidNamedArgs = NULL;

    // Invoke method
    EXCEPINFO excepInfo;
    UINT argErr;
    VariantInit(pResult);

    hr = pDisp->lpVtbl->Invoke(pDisp, dispid, &IID_NULL, LOCALE_USER_DEFAULT,
                               DISPATCH_METHOD, &params, pResult,
                               &excepInfo, &argErr);

    // Cleanup
    if (pArgs) {
        for (int i = 0; i < numArgs; i++) {
            VariantClear(&pArgs[i]);
        }
        free(pArgs);
    }

    return hr;
}

/**
 * Get property value from IDispatch
 */
static HRESULT GetProperty(IDispatch *pDisp, LPOLESTR propName, VARIANT *pResult) {
    if (!pDisp) return E_POINTER;

    DISPID dispid;
    HRESULT hr = pDisp->lpVtbl->GetIDsOfNames(pDisp, &IID_NULL, &propName,
                                               1, LOCALE_USER_DEFAULT, &dispid);
    if (FAILED(hr)) return hr;

    DISPPARAMS params = {NULL, NULL, 0, 0};
    VariantInit(pResult);

    hr = pDisp->lpVtbl->Invoke(pDisp, dispid, &IID_NULL, LOCALE_USER_DEFAULT,
                               DISPATCH_PROPERTYGET, &params, pResult, NULL, NULL);

    return hr;
}

/******************************************************************************
 * Initialization and Cleanup
 ******************************************************************************/

int ECLAB_Initialize(ECLabConnection **conn, const char *workingDir) {
    if (!conn) return ERR_NULL_POINTER;

    LogMessageEx(LOG_DEVICE_BIO, "========================================");
    LogMessageEx(LOG_DEVICE_BIO, "Initializing EC-Lab OLE COM connection");
    LogMessageEx(LOG_DEVICE_BIO, "========================================");

    // Allocate connection structure
    ECLabConnection *c = (ECLabConnection*)calloc(1, sizeof(ECLabConnection));
    if (!c) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to allocate memory for connection");
        return ERR_OUT_OF_MEMORY;
    }

    // Set working directory
    if (workingDir) {
        strncpy(c->workingDir, workingDir, MAX_PATH - 1);
        LogMessageEx(LOG_DEVICE_BIO, "Working directory: %s", c->workingDir);
    } else {
        GetCurrentDirectoryA(MAX_PATH, c->workingDir);
        LogMessageEx(LOG_DEVICE_BIO, "Using current directory: %s", c->workingDir);
    }

    // Initialize COM in Multithreaded Apartment (MTA) mode
    //
    // CRITICAL: MTA must be used instead of STA (Single-Threaded Apartment) because:
    // 1. The IEClabExe interface pointer is shared across multiple threads
    // 2. Status monitoring thread calls TestConnection() on this interface
    // 3. STA interface pointers CANNOT be used across threads without marshaling
    // 4. Using STA causes RPC_E_DISCONNECTED (0x8001010E) errors
    //
    // MTA allows interface pointers to be safely shared across all threads
    // that also initialize COM with COINIT_MULTITHREADED.
    LogMessageEx(LOG_DEVICE_BIO, "Step 1: Initializing COM (MTA mode)...");
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        LogErrorEx(LOG_DEVICE_BIO, "ERROR: CoInitializeEx failed with HRESULT: 0x%08X", hr);
        LogErrorEx(LOG_DEVICE_BIO, "This indicates a COM system error.");
        free(c);
        return ECLAB_ERR_COM_INIT_FAILED;
    }
    if (hr == RPC_E_CHANGED_MODE) {
        LogMessageEx(LOG_DEVICE_BIO, "COM already initialized in different mode (this is OK)");
    } else {
        LogMessageEx(LOG_DEVICE_BIO, "COM initialized successfully (MTA mode)");
    }

    // Get CLSID for EC-Lab
    LogMessageEx(LOG_DEVICE_BIO, "Step 2: Resolving ProgID 'EClabCOM.EClabExe'...");
    wchar_t progId[] = L"EClabCOM.EClabExe";
    hr = CLSIDFromProgID(progId, &c->clsid);
    if (FAILED(hr)) {
        LogErrorEx(LOG_DEVICE_BIO, "ERROR: CLSIDFromProgID failed with HRESULT: 0x%08X", hr);
        LogErrorEx(LOG_DEVICE_BIO, "");
        LogErrorEx(LOG_DEVICE_BIO, "This means EC-Lab is NOT registered as an OLE COM server.");
        LogErrorEx(LOG_DEVICE_BIO, "");
        LogErrorEx(LOG_DEVICE_BIO, "To fix this, run the following command as Administrator:");
        LogErrorEx(LOG_DEVICE_BIO, "  cd \"C:\\Program Files (x86)\\EC-Lab\"");
        LogErrorEx(LOG_DEVICE_BIO, "  ECLab.exe /regserver");
        LogErrorEx(LOG_DEVICE_BIO, "");
        LogErrorEx(LOG_DEVICE_BIO, "After registration, restart this application.");
        CoUninitialize();
        free(c);
        return ECLAB_ERR_COM_CREATE_FAILED;
    }
    LogMessageEx(LOG_DEVICE_BIO, "ProgID resolved successfully. EC-Lab is registered.");

    // Check if EC-Lab is running
    LogMessageEx(LOG_DEVICE_BIO, "Step 3: Checking if EC-Lab is running...");
    if (!ECLAB_IsRunning()) {
        LogErrorEx(LOG_DEVICE_BIO, "ERROR: EC-Lab.exe is not running!");
        LogErrorEx(LOG_DEVICE_BIO, "");
        LogErrorEx(LOG_DEVICE_BIO, "SOLUTION:");
        LogErrorEx(LOG_DEVICE_BIO, "  1. Start EC-Lab application");
        LogErrorEx(LOG_DEVICE_BIO, "  2. Wait for it to fully load");
        LogErrorEx(LOG_DEVICE_BIO, "  3. Verify it shows 'OLECOM' in the status bar");
        LogErrorEx(LOG_DEVICE_BIO, "  4. Then restart this application");
        CoUninitialize();
        free(c);
        return ECLAB_ERR_COM_CREATE_FAILED;
    }
    LogMessageEx(LOG_DEVICE_BIO, "EC-Lab process detected - proceeding with COM connection");

    // Create EC-Lab COM object using custom IEClabExe interface
    LogMessageEx(LOG_DEVICE_BIO, "Step 4: Creating EC-Lab COM instance...");
    LogMessageEx(LOG_DEVICE_BIO, "NOTE: This will connect to the running EC-Lab application.");
    LogMessageEx(LOG_DEVICE_BIO, "NOTE: Using custom IEClabExe interface (NOT IDispatch)");

    // Create instance with custom interface
    LogMessageEx(LOG_DEVICE_BIO, "Requesting IEClabExe interface...");
    hr = CoCreateInstance(&c->clsid, NULL, CLSCTX_LOCAL_SERVER,
                         &IID_IEClabExe, (void**)&c->pInterface);

    // If that fails with E_NOINTERFACE, show diagnostics
    if (hr == E_NOINTERFACE) {
        LogErrorEx(LOG_DEVICE_BIO, "CRITICAL: EC-Lab does not support IEClabExe interface!");
        LogErrorEx(LOG_DEVICE_BIO, "");

        // Show COM registration details
        DiagnoseCOMRegistration(L"EClabCOM.EClabExe", &c->clsid);

        LogErrorEx(LOG_DEVICE_BIO, "");
        LogErrorEx(LOG_DEVICE_BIO, "Possible causes:");
        LogErrorEx(LOG_DEVICE_BIO, "1. EC-Lab version is too old and doesn't have this interface");
        LogErrorEx(LOG_DEVICE_BIO, "2. EC-Lab COM registration is incomplete");
        LogErrorEx(LOG_DEVICE_BIO, "3. Wrong ProgID or CLSID");
    }

    if (FAILED(hr)) {
        LogErrorEx(LOG_DEVICE_BIO, "ERROR: CoCreateInstance failed with HRESULT: 0x%08X", hr);
        LogErrorEx(LOG_DEVICE_BIO, "");

        if (hr == 0x800401F3) {  // CLSID_E_CLASSSTRING
            LogErrorEx(LOG_DEVICE_BIO, "Class string error - EC-Lab may not be properly registered.");
        } else if (hr == 0x80080005) {  // CO_E_SERVER_EXEC_FAILURE
            LogErrorEx(LOG_DEVICE_BIO, "EC-Lab server execution failed.");
            LogErrorEx(LOG_DEVICE_BIO, "Possible causes:");
            LogErrorEx(LOG_DEVICE_BIO, "  1. EC-Lab is not running - START EC-Lab first");
            LogErrorEx(LOG_DEVICE_BIO, "  2. EC-Lab crashed during startup");
            LogErrorEx(LOG_DEVICE_BIO, "  3. Insufficient permissions");
        } else if (hr == 0x80070005) {  // E_ACCESSDENIED
            LogErrorEx(LOG_DEVICE_BIO, "Access denied - run as Administrator");
        } else if (hr == 0x80004002) {  // E_NOINTERFACE
            LogErrorEx(LOG_DEVICE_BIO, "E_NOINTERFACE error - EC-Lab COM interface issue.");
            LogErrorEx(LOG_DEVICE_BIO, "This typically means:");
            LogErrorEx(LOG_DEVICE_BIO, "  1. EC-Lab version mismatch (need version with OLE COM support)");
            LogErrorEx(LOG_DEVICE_BIO, "  2. EC-Lab not properly registered (run: ECLab.exe /regserver)");
            LogErrorEx(LOG_DEVICE_BIO, "  3. EC-Lab COM interface incompatible with this version");
            LogErrorEx(LOG_DEVICE_BIO, "  4. EC-Lab must be running AND fully loaded before connection");
        } else {
            LogErrorEx(LOG_DEVICE_BIO, "Unknown COM error occurred.");
        }

        LogErrorEx(LOG_DEVICE_BIO, "");
        LogErrorEx(LOG_DEVICE_BIO, "SOLUTION:");
        LogErrorEx(LOG_DEVICE_BIO, "  1. Verify EC-Lab version supports OLE COM (check manual)");
        LogErrorEx(LOG_DEVICE_BIO, "  2. Re-register EC-Lab: run as Admin: ECLab.exe /regserver");
        LogErrorEx(LOG_DEVICE_BIO, "  3. Start EC-Lab application BEFORE this program");
        LogErrorEx(LOG_DEVICE_BIO, "  4. Wait for EC-Lab to fully load");
        LogErrorEx(LOG_DEVICE_BIO, "  5. Connect a device in EC-Lab");
        LogErrorEx(LOG_DEVICE_BIO, "  6. Check for 'OLECOM' indicator in EC-Lab status bar");
        LogErrorEx(LOG_DEVICE_BIO, "  7. Then start this application");

        CoUninitialize();
        free(c);
        return ECLAB_ERR_COM_CREATE_FAILED;
    }
    LogMessageEx(LOG_DEVICE_BIO, "EC-Lab COM instance created successfully!");

    LogMessageEx(LOG_DEVICE_BIO, "========================================");
    LogMessageEx(LOG_DEVICE_BIO, "EC-Lab OLE COM connection initialized");
    LogMessageEx(LOG_DEVICE_BIO, "========================================");

    *conn = c;
    return SUCCESS;
}

int ECLAB_Shutdown(ECLabConnection *conn) {
    if (!conn) return ERR_NULL_POINTER;

    LogMessageEx(LOG_DEVICE_BIO, "Shutting down EC-Lab OLE COM connection");

    // Disconnect if connected
    if (conn->isConnected) {
        ECLAB_DisconnectDevice(conn);
    }

    // Release COM interface
    if (conn->pInterface) {
        conn->pInterface->lpVtbl->Release(conn->pInterface);
        conn->pInterface = NULL;
    }

    CoUninitialize();
    free(conn);

    LogMessageEx(LOG_DEVICE_BIO, "EC-Lab OLE COM connection shutdown complete");
    return SUCCESS;
}

int ECLAB_RegisterServer(const char *eclabPath) {
    if (!eclabPath) return ERR_NULL_POINTER;

    LogMessageEx(LOG_DEVICE_BIO, "Registering EC-Lab as OLE COM server: %s", eclabPath);

    // Build command: "ECLab.exe" /regserver
    char cmd[MAX_PATH * 2];
    snprintf(cmd, sizeof(cmd), "\"%s\" /regserver", eclabPath);

    // Execute registration command
    STARTUPINFO si = {sizeof(si)};
    PROCESS_INFORMATION pi;

    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to execute: %s (Error: %d)", cmd, GetLastError());
        return ECLAB_ERR_REGISTRATION_FAILED;
    }

    // Wait for registration to complete
    WaitForSingleObject(pi.hProcess, 10000);  // 10 second timeout

    DWORD exitCode;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exitCode != 0 && exitCode != STILL_ACTIVE) {
        LogErrorEx(LOG_DEVICE_BIO, "Registration failed with exit code: %d", exitCode);
        return ECLAB_ERR_REGISTRATION_FAILED;
    }

    LogMessageEx(LOG_DEVICE_BIO, "EC-Lab OLE COM registration complete");
    return SUCCESS;
}

/******************************************************************************
 * Device Management
 ******************************************************************************/

int ECLAB_ConnectDevice(ECLabConnection *conn, int deviceNumber) {
    if (!conn || !conn->pInterface) return ECLAB_ERR_INVALID_CONNECTION;
    if (conn->isConnected) return ECLAB_ERR_ALREADY_CONNECTED;

    LogMessageEx(LOG_DEVICE_BIO, "Connecting to EC-Lab device %d", deviceNumber);

    // Call ConnectDevice method directly through vtable
    int retVal = conn->pInterface->lpVtbl->ConnectDevice(conn->pInterface, deviceNumber);

    // EC-Lab returns 1 if connected, 0 otherwise (per manual section 3.2.1)
    // Any other value (negative, COM errors) indicates failure
    if (retVal == 1) {
        conn->deviceNumber = deviceNumber;
        conn->isConnected = true;

        // Immediately verify connection with TestConnection
        int testRetVal = conn->pInterface->lpVtbl->TestConnection(conn->pInterface, deviceNumber);

        if (testRetVal == 1) {
            LogMessageEx(LOG_DEVICE_BIO, "Connected to EC-Lab device %d successfully", deviceNumber);
        } else {
            LogWarningEx(LOG_DEVICE_BIO, "ConnectDevice succeeded but TestConnection failed (device may not be fully ready)");
            // Keep isConnected = true since ConnectDevice succeeded
        }
    } else {
        LogErrorEx(LOG_DEVICE_BIO, "ConnectDevice failed (returned %d)", retVal);
        if (retVal == 0) {
            LogErrorEx(LOG_DEVICE_BIO, "Device %d not connected in EC-Lab", deviceNumber);
        } else {
            LogErrorEx(LOG_DEVICE_BIO, "COM error 0x%08X - check EC-Lab status", retVal);
        }
        return ECLAB_ERR_DEVICE_NOT_FOUND;
    }

    return SUCCESS;
}

int ECLAB_DisconnectDevice(ECLabConnection *conn) {
    if (!conn || !conn->pInterface) return ECLAB_ERR_INVALID_CONNECTION;
    if (!conn->isConnected) return SUCCESS;  // Already disconnected

    LogMessageEx(LOG_DEVICE_BIO, "Disconnecting from EC-Lab device %d", conn->deviceNumber);

    // Call DisconnectDevice method directly through vtable
    int retVal = conn->pInterface->lpVtbl->DisconnectDevice(conn->pInterface, conn->deviceNumber);

    // EC-Lab returns 1 if success, 0 or other values if failed (per manual convention)
    if (retVal == 1) {
        // Only update flag if disconnect succeeded
        conn->isConnected = false;
        LogMessageEx(LOG_DEVICE_BIO, "Disconnected from EC-Lab device");
        return SUCCESS;
    } else {
        LogWarningEx(LOG_DEVICE_BIO, "DisconnectDevice failed (returned %d)", retVal);
        // Do NOT set isConnected = false here - disconnect failed!
        return ECLAB_ERR_COM_INVOKE_FAILED;
    }
}

int ECLAB_ForceReconnect(ECLabConnection *conn, int deviceNumber) {
    if (!conn || !conn->pInterface) return ECLAB_ERR_INVALID_CONNECTION;

    LogMessageEx(LOG_DEVICE_BIO, "========================================");
    LogMessageEx(LOG_DEVICE_BIO, "Force reconnecting to EC-Lab device %d", deviceNumber);
    LogMessageEx(LOG_DEVICE_BIO, "========================================");
    LogMessageEx(LOG_DEVICE_BIO, "This function is used when EC-Lab has detected a hardware");
    LogMessageEx(LOG_DEVICE_BIO, "disconnection (e.g., relay switch) and marked the device as");
    LogMessageEx(LOG_DEVICE_BIO, "'not connected' internally, but the device is now reconnected.");
    LogMessageEx(LOG_DEVICE_BIO, "");

    // Force the isConnected flag to false
    // This is necessary when EC-Lab thinks the device is disconnected but our
    // internal flag still shows connected, which prevents ConnectDevice from working.
    // This typically happens after relay switching when:
    //   1. EC-Lab detects the physical disconnection
    //   2. We try to call DisconnectDevice, but it fails because EC-Lab already
    //      considers the device disconnected
    //   3. The failed DisconnectDevice leaves our isConnected flag as true
    //   4. ConnectDevice then refuses to work because isConnected is true
    bool wasConnected = conn->isConnected;
    conn->isConnected = false;

    if (wasConnected) {
        LogMessageEx(LOG_DEVICE_BIO, "Forced isConnected flag from TRUE to FALSE");
    } else {
        LogMessageEx(LOG_DEVICE_BIO, "isConnected flag was already FALSE");
    }

    // Now call the standard ConnectDevice function
    LogMessageEx(LOG_DEVICE_BIO, "Calling ConnectDevice to re-establish connection...");
    int result = ECLAB_ConnectDevice(conn, deviceNumber);

    if (result == SUCCESS) {
        LogMessageEx(LOG_DEVICE_BIO, "========================================");
        LogMessageEx(LOG_DEVICE_BIO, "Force reconnect succeeded!");
        LogMessageEx(LOG_DEVICE_BIO, "========================================");
    } else {
        LogErrorEx(LOG_DEVICE_BIO, "========================================");
        LogErrorEx(LOG_DEVICE_BIO, "Force reconnect FAILED: %s", ECLAB_GetErrorString(result));
        LogErrorEx(LOG_DEVICE_BIO, "========================================");
    }

    return result;
}

int ECLAB_TestConnection(ECLabConnection *conn) {
    if (!conn || !conn->pInterface) return ECLAB_ERR_INVALID_CONNECTION;

    // Always test the actual connection - don't rely on cached flag
    // The purpose of TestConnection is to actively verify device connectivity

    // Call TestConnection method directly through vtable
    int retVal = conn->pInterface->lpVtbl->TestConnection(conn->pInterface, conn->deviceNumber);

    // EC-Lab returns 1 if connected, 0 otherwise (per manual section 3.2.3)
    if (retVal == 1) {
        conn->isConnected = true;   // Update flag based on actual test
        return SUCCESS;
    } else {
        conn->isConnected = false;  // Update flag based on actual test
        LogWarningEx(LOG_DEVICE_BIO, "TestConnection: Device %d is not connected", conn->deviceNumber);
        return ECLAB_ERR_NOT_CONNECTED;
    }
}

/******************************************************************************
 * Experiment Control
 ******************************************************************************/

int ECLAB_LoadSettings(ECLabConnection *conn, int device, int channel,
                      const char *mpsFilePath) {
    if (!conn || !conn->pInterface) return ECLAB_ERR_INVALID_CONNECTION;
    if (!mpsFilePath) return ERR_NULL_POINTER;

    LogMessageEx(LOG_DEVICE_BIO, "Loading settings from: %s", mpsFilePath);

    // Check file exists
    if (GetFileAttributesA(mpsFilePath) == INVALID_FILE_ATTRIBUTES) {
        LogErrorEx(LOG_DEVICE_BIO, "Settings file not found: %s", mpsFilePath);
        return ECLAB_ERR_FILE_NOT_FOUND;
    }

    // Convert file path to BSTR
    BSTR bstrFilePath = StringToBSTR(mpsFilePath);
    if (!bstrFilePath) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to convert file path to BSTR");
        return ECLAB_ERR_BSTR_CONVERSION;
    }

    // Call LoadSettings method directly through vtable
    int retVal = conn->pInterface->lpVtbl->LoadSettings(conn->pInterface,
                                                        device, channel, bstrFilePath);

    SysFreeString(bstrFilePath);

    // EC-Lab returns 1 if success, 0 or other values if failed (per manual section 3.2.5)
    if (retVal == 1) {
        LogMessageEx(LOG_DEVICE_BIO, "Settings loaded successfully");
        return SUCCESS;
    } else {
        // Log both decimal and hex to understand what EC-Lab is returning
        LogErrorEx(LOG_DEVICE_BIO, "LoadSettings failed (returned %d / 0x%08X)",
                  retVal, (unsigned int)retVal);
        if (retVal == (int)0x8001010E) {
            LogErrorEx(LOG_DEVICE_BIO, "  Error is RPC_E_DISCONNECTED (0x8001010E)");
            LogErrorEx(LOG_DEVICE_BIO, "  This typically means EC-Lab has invalidated the interface");
            LogErrorEx(LOG_DEVICE_BIO, "  Possible causes:");
            LogErrorEx(LOG_DEVICE_BIO, "    - Device hardware fault detected by EC-Lab");
            LogErrorEx(LOG_DEVICE_BIO, "    - Voltage transient from relay switching");
            LogErrorEx(LOG_DEVICE_BIO, "    - Channel in error/fault state");
        }
        return ECLAB_ERR_INVALID_MPS_FILE;
    }
}

int ECLAB_RunChannel(ECLabConnection *conn, int device, int channel,
                    const char *outputMprPath) {
    if (!conn || !conn->pInterface) return ECLAB_ERR_INVALID_CONNECTION;
    if (!outputMprPath) return ERR_NULL_POINTER;

    LogMessageEx(LOG_DEVICE_BIO, "Starting measurement, output: %s", outputMprPath);

    // Convert output path to BSTR
    BSTR bstrOutputPath = StringToBSTR(outputMprPath);
    if (!bstrOutputPath) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to convert output path to BSTR");
        return ECLAB_ERR_BSTR_CONVERSION;
    }

    // Call RunChannel method directly through vtable
    int retVal = conn->pInterface->lpVtbl->RunChannel(conn->pInterface,
                                                      device, channel, bstrOutputPath);

    SysFreeString(bstrOutputPath);

    // EC-Lab returns 1 if success, 0 or other values if failed (per manual section 3.2.6)
    if (retVal == 1) {
        LogMessageEx(LOG_DEVICE_BIO, "Measurement started");
        return SUCCESS;
    } else {
        LogErrorEx(LOG_DEVICE_BIO, "RunChannel failed (returned %d)", retVal);
        return ECLAB_ERR_RUN_FAILED;
    }
}

int ECLAB_StopChannel(ECLabConnection *conn, int device, int channel) {
    if (!conn || !conn->pInterface) return ECLAB_ERR_INVALID_CONNECTION;

    LogMessageEx(LOG_DEVICE_BIO, "Stopping measurement on device %d, channel %d",
                device, channel);

    // Call StopChannel method directly through vtable
    int retVal = conn->pInterface->lpVtbl->StopChannel(conn->pInterface, device, channel);

    // EC-Lab returns 1 if success, 0 or other values if failed (per manual section 3.2.7)
    if (retVal == 1) {
        LogMessageEx(LOG_DEVICE_BIO, "Measurement stopped");
        return SUCCESS;
    } else {
        // Log both decimal and hex to understand what EC-Lab is returning
        LogWarningEx(LOG_DEVICE_BIO, "StopChannel failed (returned %d / 0x%08X)",
                    retVal, (unsigned int)retVal);
        if (retVal == (int)0x8001010E) {
            LogWarningEx(LOG_DEVICE_BIO, "  Error is RPC_E_DISCONNECTED (0x8001010E)");
            LogWarningEx(LOG_DEVICE_BIO, "  This typically means EC-Lab has invalidated the interface");
        }
        return ECLAB_ERR_COM_INVOKE_FAILED;
    }
}

/******************************************************************************
 * Status Monitoring
 ******************************************************************************/

int ECLAB_MeasureStatus(ECLabConnection *conn, int device, int channel,
                       ECLAB_Status *status) {
    if (!conn || !conn->pInterface) return ECLAB_ERR_INVALID_CONNECTION;
    if (!status) return ERR_NULL_POINTER;

    memset(status, 0, sizeof(ECLAB_Status));

    // Call MeasureStatus method directly through vtable
    VARIANT statusResult;
    VariantInit(&statusResult);

    int retVal = conn->pInterface->lpVtbl->MeasureStatus(conn->pInterface,
                                                         device, channel, &statusResult);

    // EC-Lab returns 1 if success, 0 or other values if failed (per manual section 3.2.9)
    if (retVal != 1) {
        LogErrorEx(LOG_DEVICE_BIO, "MeasureStatus failed (returned %d)", retVal);
        VariantClear(&statusResult);
        return ECLAB_ERR_COM_INVOKE_FAILED;
    }

    // Parse status array - EC-Lab returns SAFEARRAY of doubles (VT_R8), not VARIANTs
    // Both VT_ARRAY|VT_R8 and VT_ARRAY|VT_VARIANT are acceptable
    VARTYPE vt = V_VT(&statusResult);

    if (vt == (VT_ARRAY | VT_R8) || vt == (VT_ARRAY | VT_VARIANT)) {
        SAFEARRAY *psa = V_ARRAY(&statusResult);
        LONG lBound, uBound;
        SafeArrayGetLBound(psa, 1, &lBound);
        SafeArrayGetUBound(psa, 1, &uBound);

        int numElements = uBound - lBound + 1;
        if (numElements != 32) {
            LogWarningEx(LOG_DEVICE_BIO, "Status array size mismatch: %d (expected 32)",
                        numElements);
        }

        // EC-Lab returns SAFEARRAY of doubles directly (VT_R8), not wrapped in VARIANTs
        // Elements are in this order (Bio-Logic manual section 3.2.9):
        // [0-14]: Integer values (but stored as doubles)
        // [15-29]: Floating point values
        // [30-31]: Integer values (but stored as doubles)
        if (vt == (VT_ARRAY | VT_R8) && numElements >= 32) {
            double *pData;
            SafeArrayAccessData(psa, (void**)&pData);

            // Extract values - integers are stored as doubles in the array
            status->status = (int)pData[0];
            status->oxRed = (int)pData[1];
            status->ocv = (int)pData[2];
            status->eis = (int)pData[3];
            status->techniqueNumber = (int)pData[4];
            status->techniqueCode = (int)pData[5];
            status->sequenceNumber = (int)pData[6];
            status->currentLoopIteration = (int)pData[7];
            status->currentSequenceInLoop = (int)pData[8];
            status->loopExperimentIteration = (int)pData[9];
            status->cycleNumber = (int)pData[10];
            status->counter1 = (int)pData[11];
            status->counter2 = (int)pData[12];
            status->counter3 = (int)pData[13];
            status->bufferSize = (int)pData[14];
            status->time = pData[15];
            status->ewe = pData[16];
            status->ece = pData[17];
            status->eoc = pData[18];
            status->current = pData[19];
            status->charge = pData[20];
            status->aux1 = pData[21];
            status->aux2 = pData[22];
            status->iRange = pData[23];
            status->rCompensation = pData[24];
            status->frequency = pData[25];
            status->zMagnitude = pData[26];
            status->currentPointIndex = (int)pData[27];
            status->totalPointIndex = (int)pData[28];
            status->temperature = pData[29];
            status->safetyLimit = (int)pData[30];
            status->connection = (int)pData[31];

            SafeArrayUnaccessData(psa);

        } else if (vt == (VT_ARRAY | VT_VARIANT) && numElements >= 32) {
            // Legacy path: SAFEARRAY of VARIANTs (not currently used by EC-Lab)
            VARIANT *pData;
            SafeArrayAccessData(psa, (void**)&pData);

            status->status = (V_VT(&pData[0]) == VT_I4) ? V_I4(&pData[0]) : 0;
            status->oxRed = (V_VT(&pData[1]) == VT_I4) ? V_I4(&pData[1]) : 0;
            status->ocv = (V_VT(&pData[2]) == VT_I4) ? V_I4(&pData[2]) : 0;
            status->eis = (V_VT(&pData[3]) == VT_I4) ? V_I4(&pData[3]) : 0;
            status->techniqueNumber = (V_VT(&pData[4]) == VT_I4) ? V_I4(&pData[4]) : 0;
            status->techniqueCode = (V_VT(&pData[5]) == VT_I4) ? V_I4(&pData[5]) : 0;
            status->sequenceNumber = (V_VT(&pData[6]) == VT_I4) ? V_I4(&pData[6]) : 0;
            status->currentLoopIteration = (V_VT(&pData[7]) == VT_I4) ? V_I4(&pData[7]) : 0;
            status->currentSequenceInLoop = (V_VT(&pData[8]) == VT_I4) ? V_I4(&pData[8]) : 0;
            status->loopExperimentIteration = (V_VT(&pData[9]) == VT_I4) ? V_I4(&pData[9]) : 0;
            status->cycleNumber = (V_VT(&pData[10]) == VT_I4) ? V_I4(&pData[10]) : 0;
            status->counter1 = (V_VT(&pData[11]) == VT_I4) ? V_I4(&pData[11]) : 0;
            status->counter2 = (V_VT(&pData[12]) == VT_I4) ? V_I4(&pData[12]) : 0;
            status->counter3 = (V_VT(&pData[13]) == VT_I4) ? V_I4(&pData[13]) : 0;
            status->bufferSize = (V_VT(&pData[14]) == VT_I4) ? V_I4(&pData[14]) : 0;
            status->time = (V_VT(&pData[15]) == VT_R8) ? V_R8(&pData[15]) : 0.0;
            status->ewe = (V_VT(&pData[16]) == VT_R8) ? V_R8(&pData[16]) : 0.0;
            status->ece = (V_VT(&pData[17]) == VT_R8) ? V_R8(&pData[17]) : 0.0;
            status->eoc = (V_VT(&pData[18]) == VT_R8) ? V_R8(&pData[18]) : 0.0;
            status->current = (V_VT(&pData[19]) == VT_R8) ? V_R8(&pData[19]) : 0.0;
            status->charge = (V_VT(&pData[20]) == VT_R8) ? V_R8(&pData[20]) : 0.0;
            status->aux1 = (V_VT(&pData[21]) == VT_R8) ? V_R8(&pData[21]) : 0.0;
            status->aux2 = (V_VT(&pData[22]) == VT_R8) ? V_R8(&pData[22]) : 0.0;
            status->iRange = (V_VT(&pData[23]) == VT_R8) ? V_R8(&pData[23]) : 0.0;
            status->rCompensation = (V_VT(&pData[24]) == VT_R8) ? V_R8(&pData[24]) : 0.0;
            status->frequency = (V_VT(&pData[25]) == VT_R8) ? V_R8(&pData[25]) : 0.0;
            status->zMagnitude = (V_VT(&pData[26]) == VT_R8) ? V_R8(&pData[26]) : 0.0;
            status->currentPointIndex = (V_VT(&pData[27]) == VT_I4) ? V_I4(&pData[27]) : 0;
            status->totalPointIndex = (V_VT(&pData[28]) == VT_I4) ? V_I4(&pData[28]) : 0;
            status->temperature = (V_VT(&pData[29]) == VT_R8) ? V_R8(&pData[29]) : 0.0;
            status->safetyLimit = (V_VT(&pData[30]) == VT_I4) ? V_I4(&pData[30]) : 0;
            status->connection = (V_VT(&pData[31]) == VT_I4) ? V_I4(&pData[31]) : 0;

            SafeArrayUnaccessData(psa);
        }
    } else {
        // VARIANT is not a recognized SAFEARRAY type
        LogErrorEx(LOG_DEVICE_BIO,
                  "MeasureStatus returned unexpected VARIANT type 0x%04X",
                  V_VT(&statusResult));
        LogErrorEx(LOG_DEVICE_BIO, "Expected: 0x%04X (VT_ARRAY|VT_R8) or 0x%04X (VT_ARRAY|VT_VARIANT)",
                  (VT_ARRAY | VT_R8), (VT_ARRAY | VT_VARIANT));
        LogErrorEx(LOG_DEVICE_BIO, "Status values will remain at zero - cannot parse this VARIANT type");
    }

    VariantClear(&statusResult);

    return SUCCESS;
}

/******************************************************************************
 * Data Retrieval
 ******************************************************************************/

int ECLAB_MeasureNumberOfPoints(const char *mprPath, int *numPoints) {
    if (!mprPath || !numPoints) return ERR_NULL_POINTER;

    // Get global connection - these file reading methods can be called
    // independently of channel operations
    ECLabConnection *conn = BIO_ECLAB_GetConnection();
    if (!conn || !conn->pInterface) return ECLAB_ERR_INVALID_CONNECTION;

    LogMessageEx(LOG_DEVICE_BIO, "Reading number of points from: %s", mprPath);

    // Convert file path to BSTR
    BSTR bstrPath = StringToBSTR(mprPath);
    if (!bstrPath) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to convert path to BSTR");
        return ECLAB_ERR_BSTR_CONVERSION;
    }

    // Call MeasureNumberOfPoints - returns int directly
    int retVal = conn->pInterface->lpVtbl->MeasureNumberOfPoints(conn->pInterface,
                                                                 bstrPath);

    SysFreeString(bstrPath);

    if (retVal >= 0) {
        *numPoints = retVal;
        LogMessageEx(LOG_DEVICE_BIO, "File contains %d data points", retVal);
        return SUCCESS;
    } else {
        LogErrorEx(LOG_DEVICE_BIO, "MeasureNumberOfPoints failed (returned %d)", retVal);
        *numPoints = 0;
        return ECLAB_ERR_COM_INVOKE_FAILED;
    }
}

int ECLAB_MeasureDcValue(const char *mprPath, int dataIndex,
                        double *time, double *voltage, double *current) {
    if (!mprPath || !time || !voltage || !current) return ERR_NULL_POINTER;

    ECLabConnection *conn = BIO_ECLAB_GetConnection();
    if (!conn || !conn->pInterface) return ECLAB_ERR_INVALID_CONNECTION;

    LogMessageEx(LOG_DEVICE_BIO, "Reading DC value at index %d from: %s",
                dataIndex, mprPath);

    // Convert file path to BSTR
    BSTR bstrPath = StringToBSTR(mprPath);
    if (!bstrPath) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to convert path to BSTR");
        return ECLAB_ERR_BSTR_CONVERSION;
    }

    // Call MeasureDcValue - returns int, output in VARIANT
    VARIANT dataResult;
    VariantInit(&dataResult);

    int retVal = conn->pInterface->lpVtbl->MeasureDcValue(conn->pInterface,
                                                         bstrPath, dataIndex,
                                                         &dataResult);

    SysFreeString(bstrPath);

    if (retVal != 1) {
        LogErrorEx(LOG_DEVICE_BIO, "MeasureDcValue failed (returned %d)", retVal);
        VariantClear(&dataResult);
        *time = *voltage = *current = 0.0;
        return ECLAB_ERR_COM_INVOKE_FAILED;
    }

    // Parse result array - should contain [time/s, Ewe/V, I/mA]
    // EC-Lab returns SAFEARRAY of doubles (VT_R8)
    VARTYPE vt = V_VT(&dataResult);

    if (vt == (VT_ARRAY | VT_R8)) {
        SAFEARRAY *psa = V_ARRAY(&dataResult);
        LONG lBound, uBound;
        SafeArrayGetLBound(psa, 1, &lBound);
        SafeArrayGetUBound(psa, 1, &uBound);
        LONG numElements = uBound - lBound + 1;

        if (numElements >= 3) {
            double *pData;
            SafeArrayAccessData(psa, (void**)&pData);

            *time = pData[0];      // time/s
            *voltage = pData[1];   // Ewe/V
            *current = pData[2];   // I/mA

            SafeArrayUnaccessData(psa);

            LogMessageEx(LOG_DEVICE_BIO, "DC data: t=%.3f s, V=%.6f V, I=%.6f mA",
                        *time, *voltage, *current);
        } else {
            LogErrorEx(LOG_DEVICE_BIO,
                      "MeasureDcValue returned array with %d elements (expected 3)",
                      numElements);
            VariantClear(&dataResult);
            *time = *voltage = *current = 0.0;
            return ECLAB_ERR_VARIANT_TYPE;
        }
    } else {
        LogErrorEx(LOG_DEVICE_BIO,
                  "MeasureDcValue returned unexpected VARIANT type 0x%04X (expected 0x%04X)",
                  vt, (VT_ARRAY | VT_R8));
        VariantClear(&dataResult);
        *time = *voltage = *current = 0.0;
        return ECLAB_ERR_VARIANT_TYPE;
    }

    VariantClear(&dataResult);
    return SUCCESS;
}

int ECLAB_MeasureEisValue(const char *mprPath, int dataIndex,
                         double *time, double *freq, double *zReal, double *zImag) {
    if (!mprPath || !time || !freq || !zReal || !zImag) return ERR_NULL_POINTER;

    ECLabConnection *conn = BIO_ECLAB_GetConnection();
    if (!conn || !conn->pInterface) return ECLAB_ERR_INVALID_CONNECTION;

    LogMessageEx(LOG_DEVICE_BIO, "Reading EIS value at index %d from: %s",
                dataIndex, mprPath);

    // Convert file path to BSTR
    BSTR bstrPath = StringToBSTR(mprPath);
    if (!bstrPath) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to convert path to BSTR");
        return ECLAB_ERR_BSTR_CONVERSION;
    }

    // Call MeasureEisValue - returns int, output in VARIANT
    VARIANT dataResult;
    VariantInit(&dataResult);

    int retVal = conn->pInterface->lpVtbl->MeasureEisValue(conn->pInterface,
                                                          bstrPath, dataIndex,
                                                          &dataResult);

    SysFreeString(bstrPath);

    if (retVal != 1) {
        LogErrorEx(LOG_DEVICE_BIO, "MeasureEisValue failed (returned %d)", retVal);
        VariantClear(&dataResult);
        *time = *freq = *zReal = *zImag = 0.0;
        return ECLAB_ERR_COM_INVOKE_FAILED;
    }

    // Parse result array - should contain [time/s, freq/Hz, Re(Z)/Ohm, -Im(Z)/Ohm]
    // EC-Lab returns SAFEARRAY of doubles (VT_R8)
    VARTYPE vt = V_VT(&dataResult);

    if (vt == (VT_ARRAY | VT_R8)) {
        SAFEARRAY *psa = V_ARRAY(&dataResult);
        LONG lBound, uBound;
        SafeArrayGetLBound(psa, 1, &lBound);
        SafeArrayGetUBound(psa, 1, &uBound);
        LONG numElements = uBound - lBound + 1;

        if (numElements >= 4) {
            double *pData;
            SafeArrayAccessData(psa, (void**)&pData);

            *time = pData[0];    // time/s
            *freq = pData[1];    // freq/Hz
            *zReal = pData[2];   // Re(Z)/Ohm
            *zImag = pData[3];   // -Im(Z)/Ohm

            SafeArrayUnaccessData(psa);

            LogMessageEx(LOG_DEVICE_BIO,
                        "EIS data: t=%.3f s, f=%.3f Hz, Re(Z)=%.6f Ohm, -Im(Z)=%.6f Ohm",
                        *time, *freq, *zReal, *zImag);
        } else {
            LogErrorEx(LOG_DEVICE_BIO,
                      "MeasureEisValue returned array with %d elements (expected 4)",
                      numElements);
            VariantClear(&dataResult);
            *time = *freq = *zReal = *zImag = 0.0;
            return ECLAB_ERR_VARIANT_TYPE;
        }
    } else {
        LogErrorEx(LOG_DEVICE_BIO,
                  "MeasureEisValue returned unexpected VARIANT type 0x%04X (expected 0x%04X)",
                  vt, (VT_ARRAY | VT_R8));
        VariantClear(&dataResult);
        *time = *freq = *zReal = *zImag = 0.0;
        return ECLAB_ERR_VARIANT_TYPE;
    }

    VariantClear(&dataResult);
    return SUCCESS;
}

int ECLAB_MeasureValueByCode(const char *mprPath, int varCode, int dataIndex,
                            double *value) {
    if (!mprPath || !value) return ERR_NULL_POINTER;

    ECLabConnection *conn = BIO_ECLAB_GetConnection();
    if (!conn || !conn->pInterface) return ECLAB_ERR_INVALID_CONNECTION;

    LogMessageEx(LOG_DEVICE_BIO,
                "Reading variable code %d at index %d from: %s",
                varCode, dataIndex, mprPath);

    // Convert file path to BSTR
    BSTR bstrPath = StringToBSTR(mprPath);
    if (!bstrPath) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to convert path to BSTR");
        return ECLAB_ERR_BSTR_CONVERSION;
    }

    // Call MeasureValueByCode - returns HRESULT, outputs double and int
    double dataValue = 0.0;
    int funcResult = 0;

    HRESULT hr = conn->pInterface->lpVtbl->MeasureValueByCode(conn->pInterface,
                                                              bstrPath, varCode,
                                                              dataIndex,
                                                              &dataValue,
                                                              &funcResult);

    SysFreeString(bstrPath);

    if (FAILED(hr)) {
        LogErrorEx(LOG_DEVICE_BIO,
                  "MeasureValueByCode failed with HRESULT 0x%08X", hr);
        *value = 0.0;
        return ECLAB_ERR_COM_INVOKE_FAILED;
    }

    if (funcResult != 1) {
        LogErrorEx(LOG_DEVICE_BIO,
                  "MeasureValueByCode function result: %d (expected 1)", funcResult);
        *value = 0.0;
        return ECLAB_ERR_COM_INVOKE_FAILED;
    }

    *value = dataValue;
    LogMessageEx(LOG_DEVICE_BIO, "Variable code %d = %.6f", varCode, dataValue);

    return SUCCESS;
}

/******************************************************************************
 * Utility Functions
 ******************************************************************************/

const char* ECLAB_GetErrorString(int errorCode) {
    switch (errorCode) {
        case SUCCESS: return "Success";
        case ECLAB_ERR_COM_INIT_FAILED: return "COM initialization failed";
        case ECLAB_ERR_COM_CREATE_FAILED: return "Failed to create EC-Lab COM object";
        case ECLAB_ERR_COM_INVOKE_FAILED: return "COM method invocation failed";
        case ECLAB_ERR_COM_RELEASE_FAILED: return "COM object release failed";
        case ECLAB_ERR_BSTR_CONVERSION: return "BSTR string conversion failed";
        case ECLAB_ERR_VARIANT_TYPE: return "Invalid VARIANT type";
        case ECLAB_ERR_INVALID_CONNECTION: return "Invalid connection handle";
        case ECLAB_ERR_NOT_CONNECTED: return "Not connected to device";
        case ECLAB_ERR_ALREADY_CONNECTED: return "Already connected to device";
        case ECLAB_ERR_DEVICE_NOT_FOUND: return "Device not found";
        case ECLAB_ERR_CHANNEL_NOT_FOUND: return "Channel not found";
        case ECLAB_ERR_FILE_NOT_FOUND: return "File not found";
        case ECLAB_ERR_INVALID_MPS_FILE: return "Invalid or corrupt .mps file";
        case ECLAB_ERR_INVALID_MPR_FILE: return "Invalid or corrupt .mpr file";
        case ECLAB_ERR_RUN_FAILED: return "Failed to start measurement";
        case ECLAB_ERR_STATUS_ARRAY_SIZE: return "Status array size mismatch";
        case ECLAB_ERR_DATA_NOT_AVAILABLE: return "Data not available";
        case ECLAB_ERR_REGISTRATION_FAILED: return "EC-Lab registration failed";
        default: return "Unknown EC-Lab error";
    }
}

bool ECLAB_IsRunning(void) {
    // Check if EC-Lab process is running
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);

    bool found = false;
    if (Process32First(hSnapshot, &pe32)) {
        do {
            if (stricmp(pe32.szExeFile, "ECLab.exe") == 0) {
                found = true;
                break;
            }
        } while (Process32Next(hSnapshot, &pe32));
    }

    CloseHandle(hSnapshot);
    return found;
}

int ECLAB_EnableMessagesWindows(ECLabConnection *conn, bool enable) {
    if (!conn || !conn->pInterface) return ECLAB_ERR_INVALID_CONNECTION;

    // Call EnableMessagesWindows method directly through vtable
    int functionResult;
    HRESULT hr = conn->pInterface->lpVtbl->EnableMessagesWindows(conn->pInterface,
                                                                 enable ? 1 : 0,
                                                                 &functionResult);

    if (FAILED(hr)) {
        LogErrorEx(LOG_DEVICE_BIO, "EnableMessagesWindows COM call failed: 0x%08X", hr);
        return ECLAB_ERR_COM_INVOKE_FAILED;
    }

    // EC-Lab returns 1 if success, 0 or other values if failed (per manual convention)
    if (functionResult == 1) {
        LogDebugEx(LOG_DEVICE_BIO, "EnableMessagesWindows succeeded (messages %s)",
                  enable ? "enabled" : "disabled");
        return SUCCESS;
    } else {
        LogWarningEx(LOG_DEVICE_BIO, "EnableMessagesWindows failed (returned %d)", functionResult);
        return ECLAB_ERR_COM_INVOKE_FAILED;
    }
}
