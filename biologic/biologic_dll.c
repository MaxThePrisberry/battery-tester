#include "common.h"
#include "biologic_dll.h"
#include "biologic_queue.h"
#include "logging.h"
#include <stdio.h>
#include <string.h>

// ============================================================================
// EClib.dll Function Pointers - ALL 58 functions
// ============================================================================

// Function pointer typedefs for EClib.dll
typedef int (__stdcall *PFN_BL_Connect)(const char*, uint8_t, int*, TDeviceInfos_t*);
typedef int (__stdcall *PFN_BL_ConvertChannelNumericIntoSingle)(uint32_t, float*, uint32_t);
typedef int (__stdcall *PFN_BL_ConvertNumericIntoFloat)(unsigned int, double*);
typedef int (__stdcall *PFN_BL_ConvertNumericIntoSingle)(unsigned int, float*);
typedef int (__stdcall *PFN_BL_ConvertTimeChannelNumericIntoSeconds)(uint32_t*, double*, float, uint32_t);
typedef int (__stdcall *PFN_BL_ConvertTimeChannelNumericIntoTimebases)(uint32_t*, double*, float*, uint32_t);
typedef int (__stdcall *PFN_BL_DefineBoolParameter)(const char*, bool, int, TEccParam_t*);
typedef int (__stdcall *PFN_BL_DefineIntParameter)(const char*, int, int, TEccParam_t*);
typedef int (__stdcall *PFN_BL_DefineSglParameter)(const char*, float, int, TEccParam_t*);
typedef int (__stdcall *PFN_BL_Disconnect)(int);
typedef int (__stdcall *PFN_BL_GetChannelBoardType)(int, uint8_t, uint32_t*);
typedef int (__stdcall *PFN_BL_GetChannelFloatFormat)(int, uint8_t, int*);
typedef int (__stdcall *PFN_BL_GetChannelInfos)(int, uint8_t, TChannelInfos_t*);
typedef int (__stdcall *PFN_BL_GetChannelsPlugged)(int, uint8_t*, uint8_t);
typedef int (__stdcall *PFN_BL_GetCurrentValues)(int, uint8_t, TCurrentValues_t*);
typedef int (__stdcall *PFN_BL_GetCurrentValuesBk)(int, uint8_t, void*);
typedef int (__stdcall *PFN_BL_GetData)(int, uint8_t, TDataBuffer_t*, TDataInfos_t*, TCurrentValues_t*);
typedef int (__stdcall *PFN_BL_GetDataBk)(int, uint8_t, void*, void*, void*);
typedef int (__stdcall *PFN_BL_GetData_LV)(int, uint8_t, void*, void*, void*);
typedef int (__stdcall *PFN_BL_GetData_VEE)(int, uint8_t, void*, void*, void*);
typedef int (__stdcall *PFN_BL_GetErrorMsg)(int, char*, unsigned int*);
typedef int (__stdcall *PFN_BL_GetExperimentInfos)(int, uint8_t, TExperimentInfos_t*);
typedef int (__stdcall *PFN_BL_GetFCTData)(int, uint8_t, TDataBuffer_t*, TDataInfos_t*, TCurrentValues_t*);
typedef int (__stdcall *PFN_BL_GetFPGAVer)(int, uint8_t, uint32_t*);
typedef int (__stdcall *PFN_BL_GetHardConf)(int, uint8_t, THardwareConf_t*);
typedef int (__stdcall *PFN_BL_GetLibVersion)(char*, unsigned int*);
typedef int (__stdcall *PFN_BL_GetMessage)(int, uint8_t, char*, unsigned int*);
typedef int (__stdcall *PFN_BL_GetModuleInfos)(int, uint8_t, void*);
typedef int (__stdcall *PFN_BL_GetModulesPlugged)(int, uint8_t*, uint8_t);
typedef int (__stdcall *PFN_BL_GetOptErr)(int, uint8_t, int*, int*);
typedef int (__stdcall *PFN_BL_GetParamInfos)(int, uint8_t, int, int, void*);
typedef int (__stdcall *PFN_BL_GetTechniqueInfos)(int, uint8_t, int, void*);
typedef bool (__stdcall *PFN_BL_GetUSBdeviceinfos)(unsigned int, char*, unsigned int*, char*, unsigned int*, char*, unsigned int*);
typedef unsigned int (__stdcall *PFN_BL_GetVolumeSerialNumber)(void);
typedef bool (__stdcall *PFN_BL_IsChannelPlugged)(int, uint8_t);
typedef bool (__stdcall *PFN_BL_IsModulePlugged)(int, uint8_t);
typedef int (__stdcall *PFN_BL_LoadFirmware)(int, uint8_t*, int*, uint8_t, bool, bool, const char*, const char*);
typedef int (__stdcall *PFN_BL_LoadFlash)(int, const char*, bool);
typedef int (__stdcall *PFN_BL_LoadTechnique)(int, uint8_t, const char*, TEccParams_t, bool, bool, bool);
typedef int (__stdcall *PFN_BL_LoadTechnique_LV)(int, uint8_t, const char*, void*, bool, bool, bool);
typedef int (__stdcall *PFN_BL_LoadTechnique_VEE)(int, uint8_t, const char*, void*, bool, bool, bool);
typedef int (__stdcall *PFN_BL_ReadParameters)(int, uint8_t, void*);
typedef int (__stdcall *PFN_BL_SendEcalMsg)(int, uint8_t, void*, unsigned int*);
typedef int (__stdcall *PFN_BL_SendEcalMsgGroup)(int, uint8_t*, uint8_t, void*, unsigned int*);
typedef int (__stdcall *PFN_BL_SendMsg)(int, uint8_t, void*, unsigned int*);
typedef int (__stdcall *PFN_BL_SendMsgToRcvt)(int, void*, unsigned int*);
typedef int (__stdcall *PFN_BL_SendMsgToRcvt_g)(int, uint8_t, void*, unsigned int*);
typedef int (__stdcall *PFN_BL_SetExperimentInfos)(int, uint8_t, TExperimentInfos_t);
typedef int (__stdcall *PFN_BL_SetHardConf)(int, uint8_t, THardwareConf_t);
typedef int (__stdcall *PFN_BL_StartChannel)(int, uint8_t);
typedef int (__stdcall *PFN_BL_StartChannels)(int, uint8_t*, int*, uint8_t);
typedef int (__stdcall *PFN_BL_StopChannel)(int, uint8_t);
typedef int (__stdcall *PFN_BL_StopChannels)(int, uint8_t*, int*, uint8_t);
typedef int (__stdcall *PFN_BL_TestCommSpeed)(int, uint8_t, int*, int*);
typedef int (__stdcall *PFN_BL_TestConnection)(int);
typedef int (__stdcall *PFN_BL_UpdateParameters)(int, uint8_t, int, TEccParams_t, const char*);
typedef int (__stdcall *PFN_BL_UpdateParameters_LV)(int, uint8_t, int, void*, const char*);
typedef int (__stdcall *PFN_BL_UpdateParameters_VEE)(int, uint8_t, int, void*, const char*);

// ============================================================================
// blfind.dll Function Pointers - ALL 12 functions
// ============================================================================

// Function pointer typedefs for blfind.dll
typedef int (__stdcall *PFN_BL_EChemBCSEthDEV)(void*, void*);
typedef int (__stdcall *PFN_BL_FindEChemBCSDev)(char*, unsigned int*, unsigned int*);
typedef int (__stdcall *PFN_BL_FindEChemDev)(char*, unsigned int*, unsigned int*);
typedef int (__stdcall *PFN_BL_FindEChemEthDev)(char*, unsigned int*, unsigned int*);
typedef int (__stdcall *PFN_BL_FindEChemUsbDev)(char*, unsigned int*, unsigned int*);
typedef int (__stdcall *PFN_BL_FindKineticDev)(char*, unsigned int*, unsigned int*);
typedef int (__stdcall *PFN_BL_FindKineticEthDev)(char*, unsigned int*, unsigned int*);
typedef int (__stdcall *PFN_BL_FindKineticUsbDev)(char*, unsigned int*, unsigned int*);
typedef int (__stdcall *PFN_BLFind_GetErrorMsg)(int, char*, unsigned int*);
typedef int (__stdcall *PFN_BL_Init_Path)(const char*);
typedef int (__stdcall *PFN_BL_SetConfig)(char*, char*);
typedef int (__stdcall *PFN_BL_SetMAC)(char*);

// EClib.dll handle and function pointers
static HINSTANCE g_hEClibDLL = NULL;
static PFN_BL_Connect g_BL_Connect = NULL;
static PFN_BL_ConvertChannelNumericIntoSingle g_BL_ConvertChannelNumericIntoSingle = NULL;
static PFN_BL_ConvertNumericIntoFloat g_BL_ConvertNumericIntoFloat = NULL;
static PFN_BL_ConvertNumericIntoSingle g_BL_ConvertNumericIntoSingle = NULL;
static PFN_BL_ConvertTimeChannelNumericIntoSeconds g_BL_ConvertTimeChannelNumericIntoSeconds = NULL;
static PFN_BL_ConvertTimeChannelNumericIntoTimebases g_BL_ConvertTimeChannelNumericIntoTimebases = NULL;
static PFN_BL_DefineBoolParameter g_BL_DefineBoolParameter = NULL;
static PFN_BL_DefineIntParameter g_BL_DefineIntParameter = NULL;
static PFN_BL_DefineSglParameter g_BL_DefineSglParameter = NULL;
static PFN_BL_Disconnect g_BL_Disconnect = NULL;
static PFN_BL_GetChannelBoardType g_BL_GetChannelBoardType = NULL;
static PFN_BL_GetChannelFloatFormat g_BL_GetChannelFloatFormat = NULL;
static PFN_BL_GetChannelInfos g_BL_GetChannelInfos = NULL;
static PFN_BL_GetChannelsPlugged g_BL_GetChannelsPlugged = NULL;
static PFN_BL_GetCurrentValues g_BL_GetCurrentValues = NULL;
static PFN_BL_GetCurrentValuesBk g_BL_GetCurrentValuesBk = NULL;
static PFN_BL_GetData g_BL_GetData = NULL;
static PFN_BL_GetDataBk g_BL_GetDataBk = NULL;
static PFN_BL_GetData_LV g_BL_GetData_LV = NULL;
static PFN_BL_GetData_VEE g_BL_GetData_VEE = NULL;
static PFN_BL_GetErrorMsg g_BL_GetErrorMsg = NULL;
static PFN_BL_GetExperimentInfos g_BL_GetExperimentInfos = NULL;
static PFN_BL_GetFCTData g_BL_GetFCTData = NULL;
static PFN_BL_GetFPGAVer g_BL_GetFPGAVer = NULL;
static PFN_BL_GetHardConf g_BL_GetHardConf = NULL;
static PFN_BL_GetLibVersion g_BL_GetLibVersion = NULL;
static PFN_BL_GetMessage g_BL_GetMessage = NULL;
static PFN_BL_GetModuleInfos g_BL_GetModuleInfos = NULL;
static PFN_BL_GetModulesPlugged g_BL_GetModulesPlugged = NULL;
static PFN_BL_GetOptErr g_BL_GetOptErr = NULL;
static PFN_BL_GetParamInfos g_BL_GetParamInfos = NULL;
static PFN_BL_GetTechniqueInfos g_BL_GetTechniqueInfos = NULL;
static PFN_BL_GetUSBdeviceinfos g_BL_GetUSBdeviceinfos = NULL;
static PFN_BL_GetVolumeSerialNumber g_BL_GetVolumeSerialNumber = NULL;
static PFN_BL_IsChannelPlugged g_BL_IsChannelPlugged = NULL;
static PFN_BL_IsModulePlugged g_BL_IsModulePlugged = NULL;
static PFN_BL_LoadFirmware g_BL_LoadFirmware = NULL;
static PFN_BL_LoadFlash g_BL_LoadFlash = NULL;
static PFN_BL_LoadTechnique g_BL_LoadTechnique = NULL;
static PFN_BL_LoadTechnique_LV g_BL_LoadTechnique_LV = NULL;
static PFN_BL_LoadTechnique_VEE g_BL_LoadTechnique_VEE = NULL;
static PFN_BL_ReadParameters g_BL_ReadParameters = NULL;
static PFN_BL_SendEcalMsg g_BL_SendEcalMsg = NULL;
static PFN_BL_SendEcalMsgGroup g_BL_SendEcalMsgGroup = NULL;
static PFN_BL_SendMsg g_BL_SendMsg = NULL;
static PFN_BL_SendMsgToRcvt g_BL_SendMsgToRcvt = NULL;
static PFN_BL_SendMsgToRcvt_g g_BL_SendMsgToRcvt_g = NULL;
static PFN_BL_SetExperimentInfos g_BL_SetExperimentInfos = NULL;
static PFN_BL_SetHardConf g_BL_SetHardConf = NULL;
static PFN_BL_StartChannel g_BL_StartChannel = NULL;
static PFN_BL_StartChannels g_BL_StartChannels = NULL;
static PFN_BL_StopChannel g_BL_StopChannel = NULL;
static PFN_BL_StopChannels g_BL_StopChannels = NULL;
static PFN_BL_TestCommSpeed g_BL_TestCommSpeed = NULL;
static PFN_BL_TestConnection g_BL_TestConnection = NULL;
static PFN_BL_UpdateParameters g_BL_UpdateParameters = NULL;
static PFN_BL_UpdateParameters_LV g_BL_UpdateParameters_LV = NULL;
static PFN_BL_UpdateParameters_VEE g_BL_UpdateParameters_VEE = NULL;

// blfind.dll handle and function pointers
static HINSTANCE g_hBLFindDLL = NULL;
static PFN_BL_EChemBCSEthDEV g_BL_EChemBCSEthDEV = NULL;
static PFN_BL_FindEChemBCSDev g_BL_FindEChemBCSDev = NULL;
static PFN_BL_FindEChemDev g_BL_FindEChemDev = NULL;
static PFN_BL_FindEChemEthDev g_BL_FindEChemEthDev = NULL;
static PFN_BL_FindEChemUsbDev g_BL_FindEChemUsbDev = NULL;
static PFN_BL_FindKineticDev g_BL_FindKineticDev = NULL;
static PFN_BL_FindKineticEthDev g_BL_FindKineticEthDev = NULL;
static PFN_BL_FindKineticUsbDev g_BL_FindKineticUsbDev = NULL;
static PFN_BLFind_GetErrorMsg g_BLFind_GetErrorMsg = NULL;
static PFN_BL_Init_Path g_BL_Init_Path = NULL;
static PFN_BL_SetConfig g_BL_SetConfig = NULL;
static PFN_BL_SetMAC g_BL_SetMAC = NULL;

// ============================================================================
// Helper Functions
// ============================================================================

// Helper function to load a function from a DLL
static void* LoadFunctionFromDLL(HINSTANCE hDLL, const char* funcName, const char* decoratedName) {
    void* func = NULL;
    
    if (hDLL == NULL) return NULL;
    
    // Try undecorated name first
    func = (void*)GetProcAddress(hDLL, funcName);
    
    // If that fails, try decorated name
    if (func == NULL && decoratedName != NULL) {
        func = (void*)GetProcAddress(hDLL, decoratedName);
    }
    
    if (func == NULL) {
        LogWarningEx(LOG_DEVICE_BIO, "Could not load function %s", funcName);
    }
    
    return func;
}

// Convert Unicode (UTF-16) to ASCII
void ConvertUnicodeToAscii(const char* unicode, char* ascii, int unicodeLen) {
    int j = 0;
    for (int i = 0; i < unicodeLen && unicode[i] != 0; i += 2) {
        ascii[j++] = unicode[i];
        if (unicode[i+1] != 0) {
            // Non-standard Unicode character, stop
            break;
        }
    }
    ascii[j] = '\0';
}

// ============================================================================
// Auto-initialization wrapper
// ============================================================================

static int BL_EnsureInitialized(void) {
    if (!IsBioLogicInitialized()) {
        return InitializeBioLogic();
    }
    return SUCCESS;
}

// ============================================================================
// EClib.dll Initialization and Management
// ============================================================================

// Initialize the EClib DLL
int InitializeBioLogic(void) {
    char dllPath[MAX_PATH_LENGTH];
    
    // If already initialized, return success
    if (g_hEClibDLL != NULL) {
        return SUCCESS;
    }
    
    // Try to load from current directory first
    GetCurrentDirectory(MAX_PATH_LENGTH, dllPath);
    strcat(dllPath, "\\EClib.dll");
    
    g_hEClibDLL = LoadLibrary(dllPath);
    
    // If that fails, try just the DLL name (will search PATH)
    if (g_hEClibDLL == NULL) {
        g_hEClibDLL = LoadLibrary("EClib.dll");
    }
    
    if (g_hEClibDLL == NULL) {
        DWORD error = GetLastError();
        LogErrorEx(LOG_DEVICE_BIO, "Failed to load EClib.dll. Error code: %d", error);
        LogErrorEx(LOG_DEVICE_BIO, "Make sure EClib.dll is in the executable directory or in PATH");
        return ERR_NOT_INITIALIZED;
    }
    
    LogMessageEx(LOG_DEVICE_BIO, "EClib.dll loaded successfully");
    
    // Load all functions
    g_BL_Connect = (PFN_BL_Connect)LoadFunctionFromDLL(g_hEClibDLL, "BL_Connect", "_BL_Connect@16");
    g_BL_ConvertChannelNumericIntoSingle = (PFN_BL_ConvertChannelNumericIntoSingle)LoadFunctionFromDLL(g_hEClibDLL, "BL_ConvertChannelNumericIntoSingle", "_BL_ConvertChannelNumericIntoSingle@12");
    g_BL_ConvertNumericIntoFloat = (PFN_BL_ConvertNumericIntoFloat)LoadFunctionFromDLL(g_hEClibDLL, "BL_ConvertNumericIntoFloat", "_BL_ConvertNumericIntoFloat@8");
    g_BL_ConvertNumericIntoSingle = (PFN_BL_ConvertNumericIntoSingle)LoadFunctionFromDLL(g_hEClibDLL, "BL_ConvertNumericIntoSingle", "_BL_ConvertNumericIntoSingle@8");
    g_BL_ConvertTimeChannelNumericIntoSeconds = (PFN_BL_ConvertTimeChannelNumericIntoSeconds)LoadFunctionFromDLL(g_hEClibDLL, "BL_ConvertTimeChannelNumericIntoSeconds", "_BL_ConvertTimeChannelNumericIntoSeconds@16");
    g_BL_ConvertTimeChannelNumericIntoTimebases = (PFN_BL_ConvertTimeChannelNumericIntoTimebases)LoadFunctionFromDLL(g_hEClibDLL, "BL_ConvertTimeChannelNumericIntoTimebases", "_BL_ConvertTimeChannelNumericIntoTimebases@16");
    g_BL_DefineBoolParameter = (PFN_BL_DefineBoolParameter)LoadFunctionFromDLL(g_hEClibDLL, "BL_DefineBoolParameter", "_BL_DefineBoolParameter@16");
    g_BL_DefineIntParameter = (PFN_BL_DefineIntParameter)LoadFunctionFromDLL(g_hEClibDLL, "BL_DefineIntParameter", "_BL_DefineIntParameter@16");
    g_BL_DefineSglParameter = (PFN_BL_DefineSglParameter)LoadFunctionFromDLL(g_hEClibDLL, "BL_DefineSglParameter", "_BL_DefineSglParameter@16");
    g_BL_Disconnect = (PFN_BL_Disconnect)LoadFunctionFromDLL(g_hEClibDLL, "BL_Disconnect", "_BL_Disconnect@4");
    g_BL_GetChannelBoardType = (PFN_BL_GetChannelBoardType)LoadFunctionFromDLL(g_hEClibDLL, "BL_GetChannelBoardType", "_BL_GetChannelBoardType@12");
    g_BL_GetChannelFloatFormat = (PFN_BL_GetChannelFloatFormat)LoadFunctionFromDLL(g_hEClibDLL, "BL_GetChannelFloatFormat", "_BL_GetChannelFloatFormat@12");
    g_BL_GetChannelInfos = (PFN_BL_GetChannelInfos)LoadFunctionFromDLL(g_hEClibDLL, "BL_GetChannelInfos", "_BL_GetChannelInfos@12");
    g_BL_GetChannelsPlugged = (PFN_BL_GetChannelsPlugged)LoadFunctionFromDLL(g_hEClibDLL, "BL_GetChannelsPlugged", "_BL_GetChannelsPlugged@12");
    g_BL_GetCurrentValues = (PFN_BL_GetCurrentValues)LoadFunctionFromDLL(g_hEClibDLL, "BL_GetCurrentValues", "_BL_GetCurrentValues@12");
    g_BL_GetCurrentValuesBk = (PFN_BL_GetCurrentValuesBk)LoadFunctionFromDLL(g_hEClibDLL, "BL_GetCurrentValuesBk", "_BL_GetCurrentValuesBk@12");
    g_BL_GetData = (PFN_BL_GetData)LoadFunctionFromDLL(g_hEClibDLL, "BL_GetData", "_BL_GetData@20");
    g_BL_GetDataBk = (PFN_BL_GetDataBk)LoadFunctionFromDLL(g_hEClibDLL, "BL_GetDataBk", "_BL_GetDataBk@20");
    g_BL_GetData_LV = (PFN_BL_GetData_LV)LoadFunctionFromDLL(g_hEClibDLL, "BL_GetData_LV", "_BL_GetData_LV@20");
    g_BL_GetData_VEE = (PFN_BL_GetData_VEE)LoadFunctionFromDLL(g_hEClibDLL, "BL_GetData_VEE", "_BL_GetData_VEE@20");
    g_BL_GetErrorMsg = (PFN_BL_GetErrorMsg)LoadFunctionFromDLL(g_hEClibDLL, "BL_GetErrorMsg", "_BL_GetErrorMsg@12");
    g_BL_GetExperimentInfos = (PFN_BL_GetExperimentInfos)LoadFunctionFromDLL(g_hEClibDLL, "BL_GetExperimentInfos", "_BL_GetExperimentInfos@12");
    g_BL_GetFCTData = (PFN_BL_GetFCTData)LoadFunctionFromDLL(g_hEClibDLL, "BL_GetFCTData", "_BL_GetFCTData@20");
    g_BL_GetFPGAVer = (PFN_BL_GetFPGAVer)LoadFunctionFromDLL(g_hEClibDLL, "BL_GetFPGAVer", "_BL_GetFPGAVer@12");
    g_BL_GetHardConf = (PFN_BL_GetHardConf)LoadFunctionFromDLL(g_hEClibDLL, "BL_GetHardConf", "_BL_GetHardConf@12");
    g_BL_GetLibVersion = (PFN_BL_GetLibVersion)LoadFunctionFromDLL(g_hEClibDLL, "BL_GetLibVersion", "_BL_GetLibVersion@8");
    g_BL_GetMessage = (PFN_BL_GetMessage)LoadFunctionFromDLL(g_hEClibDLL, "BL_GetMessage", "_BL_GetMessage@16");
    g_BL_GetModuleInfos = (PFN_BL_GetModuleInfos)LoadFunctionFromDLL(g_hEClibDLL, "BL_GetModuleInfos", "_BL_GetModuleInfos@12");
    g_BL_GetModulesPlugged = (PFN_BL_GetModulesPlugged)LoadFunctionFromDLL(g_hEClibDLL, "BL_GetModulesPlugged", "_BL_GetModulesPlugged@12");
    g_BL_GetOptErr = (PFN_BL_GetOptErr)LoadFunctionFromDLL(g_hEClibDLL, "BL_GetOptErr", "_BL_GetOptErr@16");
    g_BL_GetParamInfos = (PFN_BL_GetParamInfos)LoadFunctionFromDLL(g_hEClibDLL, "BL_GetParamInfos", "_BL_GetParamInfos@20");
    g_BL_GetTechniqueInfos = (PFN_BL_GetTechniqueInfos)LoadFunctionFromDLL(g_hEClibDLL, "BL_GetTechniqueInfos", "_BL_GetTechniqueInfos@16");
    g_BL_GetUSBdeviceinfos = (PFN_BL_GetUSBdeviceinfos)LoadFunctionFromDLL(g_hEClibDLL, "BL_GetUSBdeviceinfos", "_BL_GetUSBdeviceinfos@28");
    g_BL_GetVolumeSerialNumber = (PFN_BL_GetVolumeSerialNumber)LoadFunctionFromDLL(g_hEClibDLL, "BL_GetVolumeSerialNumber", "_BL_GetVolumeSerialNumber@0");
    g_BL_IsChannelPlugged = (PFN_BL_IsChannelPlugged)LoadFunctionFromDLL(g_hEClibDLL, "BL_IsChannelPlugged", "_BL_IsChannelPlugged@8");
    g_BL_IsModulePlugged = (PFN_BL_IsModulePlugged)LoadFunctionFromDLL(g_hEClibDLL, "BL_IsModulePlugged", "_BL_IsModulePlugged@8");
    g_BL_LoadFirmware = (PFN_BL_LoadFirmware)LoadFunctionFromDLL(g_hEClibDLL, "BL_LoadFirmware", "_BL_LoadFirmware@32");
    g_BL_LoadFlash = (PFN_BL_LoadFlash)LoadFunctionFromDLL(g_hEClibDLL, "BL_LoadFlash", "_BL_LoadFlash@12");
    g_BL_LoadTechnique = (PFN_BL_LoadTechnique)LoadFunctionFromDLL(g_hEClibDLL, "BL_LoadTechnique", "_BL_LoadTechnique@28");
    g_BL_LoadTechnique_LV = (PFN_BL_LoadTechnique_LV)LoadFunctionFromDLL(g_hEClibDLL, "BL_LoadTechnique_LV", "_BL_LoadTechnique_LV@28");
    g_BL_LoadTechnique_VEE = (PFN_BL_LoadTechnique_VEE)LoadFunctionFromDLL(g_hEClibDLL, "BL_LoadTechnique_VEE", "_BL_LoadTechnique_VEE@28");
    g_BL_ReadParameters = (PFN_BL_ReadParameters)LoadFunctionFromDLL(g_hEClibDLL, "BL_ReadParameters", "_BL_ReadParameters@12");
    g_BL_SendEcalMsg = (PFN_BL_SendEcalMsg)LoadFunctionFromDLL(g_hEClibDLL, "BL_SendEcalMsg", "_BL_SendEcalMsg@16");
    g_BL_SendEcalMsgGroup = (PFN_BL_SendEcalMsgGroup)LoadFunctionFromDLL(g_hEClibDLL, "BL_SendEcalMsgGroup", "_BL_SendEcalMsgGroup@20");
    g_BL_SendMsg = (PFN_BL_SendMsg)LoadFunctionFromDLL(g_hEClibDLL, "BL_SendMsg", "_BL_SendMsg@16");
    g_BL_SendMsgToRcvt = (PFN_BL_SendMsgToRcvt)LoadFunctionFromDLL(g_hEClibDLL, "BL_SendMsgToRcvt", "_BL_SendMsgToRcvt@12");
    g_BL_SendMsgToRcvt_g = (PFN_BL_SendMsgToRcvt_g)LoadFunctionFromDLL(g_hEClibDLL, "BL_SendMsgToRcvt_g", "_BL_SendMsgToRcvt_g@16");
    g_BL_SetExperimentInfos = (PFN_BL_SetExperimentInfos)LoadFunctionFromDLL(g_hEClibDLL, "BL_SetExperimentInfos", "_BL_SetExperimentInfos@12");
    g_BL_SetHardConf = (PFN_BL_SetHardConf)LoadFunctionFromDLL(g_hEClibDLL, "BL_SetHardConf", "_BL_SetHardConf@12");
    g_BL_StartChannel = (PFN_BL_StartChannel)LoadFunctionFromDLL(g_hEClibDLL, "BL_StartChannel", "_BL_StartChannel@8");
    g_BL_StartChannels = (PFN_BL_StartChannels)LoadFunctionFromDLL(g_hEClibDLL, "BL_StartChannels", "_BL_StartChannels@16");
    g_BL_StopChannel = (PFN_BL_StopChannel)LoadFunctionFromDLL(g_hEClibDLL, "BL_StopChannel", "_BL_StopChannel@8");
    g_BL_StopChannels = (PFN_BL_StopChannels)LoadFunctionFromDLL(g_hEClibDLL, "BL_StopChannels", "_BL_StopChannels@16");
    g_BL_TestCommSpeed = (PFN_BL_TestCommSpeed)LoadFunctionFromDLL(g_hEClibDLL, "BL_TestCommSpeed", "_BL_TestCommSpeed@16");
    g_BL_TestConnection = (PFN_BL_TestConnection)LoadFunctionFromDLL(g_hEClibDLL, "BL_TestConnection", "_BL_TestConnection@4");
    g_BL_UpdateParameters = (PFN_BL_UpdateParameters)LoadFunctionFromDLL(g_hEClibDLL, "BL_UpdateParameters", "_BL_UpdateParameters@20");
    g_BL_UpdateParameters_LV = (PFN_BL_UpdateParameters_LV)LoadFunctionFromDLL(g_hEClibDLL, "BL_UpdateParameters_LV", "_BL_UpdateParameters_LV@20");
    g_BL_UpdateParameters_VEE = (PFN_BL_UpdateParameters_VEE)LoadFunctionFromDLL(g_hEClibDLL, "BL_UpdateParameters_VEE", "_BL_UpdateParameters_VEE@20");
    
    // Check if critical functions were loaded
    if (g_BL_Connect == NULL || g_BL_Disconnect == NULL) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to load critical functions from EClib.dll");
        CleanupBioLogic();
        return ERR_NOT_INITIALIZED;
    }
    
    // Get and display library version if available
    if (g_BL_GetLibVersion != NULL) {
        char version[256];
        unsigned int size = sizeof(version);
        if (g_BL_GetLibVersion(version, &size) == 0) {
            LogMessageEx(LOG_DEVICE_BIO, "EClib version: %s", version);
        }
    }
    
    return SUCCESS;
}

// Cleanup and unload the EClib DLL
void CleanupBioLogic(void) {
    if (g_hEClibDLL != NULL) {
        FreeLibrary(g_hEClibDLL);
        g_hEClibDLL = NULL;
        
        // Clear all function pointers (setting all 58 to NULL)
        g_BL_Connect = NULL;
        g_BL_ConvertChannelNumericIntoSingle = NULL;
        g_BL_ConvertNumericIntoFloat = NULL;
        g_BL_ConvertNumericIntoSingle = NULL;
        g_BL_ConvertTimeChannelNumericIntoSeconds = NULL;
        g_BL_ConvertTimeChannelNumericIntoTimebases = NULL;
        g_BL_DefineBoolParameter = NULL;
        g_BL_DefineIntParameter = NULL;
        g_BL_DefineSglParameter = NULL;
        g_BL_Disconnect = NULL;
        g_BL_GetChannelBoardType = NULL;
        g_BL_GetChannelFloatFormat = NULL;
        g_BL_GetChannelInfos = NULL;
        g_BL_GetChannelsPlugged = NULL;
        g_BL_GetCurrentValues = NULL;
        g_BL_GetCurrentValuesBk = NULL;
        g_BL_GetData = NULL;
        g_BL_GetDataBk = NULL;
        g_BL_GetData_LV = NULL;
        g_BL_GetData_VEE = NULL;
        g_BL_GetErrorMsg = NULL;
        g_BL_GetExperimentInfos = NULL;
        g_BL_GetFCTData = NULL;
        g_BL_GetFPGAVer = NULL;
        g_BL_GetHardConf = NULL;
        g_BL_GetLibVersion = NULL;
        g_BL_GetMessage = NULL;
        g_BL_GetModuleInfos = NULL;
        g_BL_GetModulesPlugged = NULL;
        g_BL_GetOptErr = NULL;
        g_BL_GetParamInfos = NULL;
        g_BL_GetTechniqueInfos = NULL;
        g_BL_GetUSBdeviceinfos = NULL;
        g_BL_GetVolumeSerialNumber = NULL;
        g_BL_IsChannelPlugged = NULL;
        g_BL_IsModulePlugged = NULL;
        g_BL_LoadFirmware = NULL;
        g_BL_LoadFlash = NULL;
        g_BL_LoadTechnique = NULL;
        g_BL_LoadTechnique_LV = NULL;
        g_BL_LoadTechnique_VEE = NULL;
        g_BL_ReadParameters = NULL;
        g_BL_SendEcalMsg = NULL;
        g_BL_SendEcalMsgGroup = NULL;
        g_BL_SendMsg = NULL;
        g_BL_SendMsgToRcvt = NULL;
        g_BL_SendMsgToRcvt_g = NULL;
        g_BL_SetExperimentInfos = NULL;
        g_BL_SetHardConf = NULL;
        g_BL_StartChannel = NULL;
        g_BL_StartChannels = NULL;
        g_BL_StopChannel = NULL;
        g_BL_StopChannels = NULL;
        g_BL_TestCommSpeed = NULL;
        g_BL_TestConnection = NULL;
        g_BL_UpdateParameters = NULL;
        g_BL_UpdateParameters_LV = NULL;
        g_BL_UpdateParameters_VEE = NULL;
    }
}

// Check if EClib is initialized
bool IsBioLogicInitialized(void) {
    return (g_hEClibDLL != NULL && g_BL_Connect != NULL && g_BL_Disconnect != NULL);
}

// ============================================================================
// blfind.dll Initialization and Management
// ============================================================================

// Initialize the BLFind DLL
int InitializeBLFind(void) {
    char dllPath[MAX_PATH_LENGTH];
    
    if (g_hBLFindDLL != NULL) {
        return SUCCESS; // Already initialized
    }
    
    // Try to load from current directory first
    GetCurrentDirectory(MAX_PATH_LENGTH, dllPath);
    strcat(dllPath, "\\blfind.dll");
    
    g_hBLFindDLL = LoadLibrary(dllPath);
    
    // If that fails, try just the DLL name (will search PATH)
    if (g_hBLFindDLL == NULL) {
        g_hBLFindDLL = LoadLibrary("blfind.dll");
    }
    
    if (g_hBLFindDLL == NULL) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to load blfind.dll. Error: %d", GetLastError());
        return ERR_NOT_INITIALIZED;
    }
    
    LogMessageEx(LOG_DEVICE_BIO, "blfind.dll loaded successfully");
    
    // Load all functions
    g_BL_EChemBCSEthDEV = (PFN_BL_EChemBCSEthDEV)LoadFunctionFromDLL(g_hBLFindDLL, "BL_EChemBCSEthDEV", "_BL_EChemBCSEthDEV@8");
    g_BL_FindEChemBCSDev = (PFN_BL_FindEChemBCSDev)LoadFunctionFromDLL(g_hBLFindDLL, "BL_FindEChemBCSDev", "_BL_FindEChemBCSDev@12");
    g_BL_FindEChemDev = (PFN_BL_FindEChemDev)LoadFunctionFromDLL(g_hBLFindDLL, "BL_FindEChemDev", "_BL_FindEChemDev@12");
    g_BL_FindEChemEthDev = (PFN_BL_FindEChemEthDev)LoadFunctionFromDLL(g_hBLFindDLL, "BL_FindEChemEthDev", "_BL_FindEChemEthDev@12");
    g_BL_FindEChemUsbDev = (PFN_BL_FindEChemUsbDev)LoadFunctionFromDLL(g_hBLFindDLL, "BL_FindEChemUsbDev", "_BL_FindEChemUsbDev@12");
    g_BL_FindKineticDev = (PFN_BL_FindKineticDev)LoadFunctionFromDLL(g_hBLFindDLL, "BL_FindKineticDev", "_BL_FindKineticDev@12");
    g_BL_FindKineticEthDev = (PFN_BL_FindKineticEthDev)LoadFunctionFromDLL(g_hBLFindDLL, "BL_FindKineticEthDev", "_BL_FindKineticEthDev@12");
    g_BL_FindKineticUsbDev = (PFN_BL_FindKineticUsbDev)LoadFunctionFromDLL(g_hBLFindDLL, "BL_FindKineticUsbDev", "_BL_FindKineticUsbDev@12");
    g_BLFind_GetErrorMsg = (PFN_BLFind_GetErrorMsg)LoadFunctionFromDLL(g_hBLFindDLL, "BL_GetErrorMsg", "_BL_GetErrorMsg@12");
    g_BL_Init_Path = (PFN_BL_Init_Path)LoadFunctionFromDLL(g_hBLFindDLL, "BL_Init_Path", "_BL_Init_Path@4");
    g_BL_SetConfig = (PFN_BL_SetConfig)LoadFunctionFromDLL(g_hBLFindDLL, "BL_SetConfig", "_BL_SetConfig@8");
    g_BL_SetMAC = (PFN_BL_SetMAC)LoadFunctionFromDLL(g_hBLFindDLL, "BL_SetMAC", "_BL_SetMAC@4");
    
    if (g_BL_FindEChemDev == NULL && g_BL_FindEChemEthDev == NULL && g_BL_FindEChemUsbDev == NULL) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to load any scanning functions from blfind.dll");
        CleanupBLFind();
        return ERR_NOT_INITIALIZED;
    }
    
    return SUCCESS;
}

// Cleanup BLFind DLL
void CleanupBLFind(void) {
    if (g_hBLFindDLL != NULL) {
        FreeLibrary(g_hBLFindDLL);
        g_hBLFindDLL = NULL;
        g_BL_EChemBCSEthDEV = NULL;
        g_BL_FindEChemBCSDev = NULL;
        g_BL_FindEChemDev = NULL;
        g_BL_FindEChemEthDev = NULL;
        g_BL_FindEChemUsbDev = NULL;
        g_BL_FindKineticDev = NULL;
        g_BL_FindKineticEthDev = NULL;
        g_BL_FindKineticUsbDev = NULL;
        g_BLFind_GetErrorMsg = NULL;
        g_BL_Init_Path = NULL;
        g_BL_SetConfig = NULL;
        g_BL_SetMAC = NULL;
    }
}

// Check if BLFind is initialized
bool IsBLFindInitialized(void) {
    return (g_hBLFindDLL != NULL);
}

// ============================================================================
// Error Handling
// ============================================================================

// Error code to string conversion
const char* BL_GetErrorString(int errorCode) {
    switch(errorCode) {
        case 0: return "Success";
        case -1: return "No instrument connected";
        case -2: return "Connection in progress";
        case -3: return "Selected channel(s) unplugged";
        case -4: return "Invalid function parameters";
        case -5: return "Selected file does not exist";
        case -6: return "Function failed";
        case -7: return "No channel selected";
        case -8: return "Invalid instrument configuration";
        case -9: return "EC-Lab firmware loaded on the instrument";
        case -10: return "Library not correctly loaded in memory";
        case -11: return "USB library not correctly loaded in memory";
        case -12: return "Function of the library already in progress";
        case -13: return "Selected channel(s) already used";
        case -14: return "Device not allowed";
        case -15: return "Invalid update function parameters";
        
        // Instrument errors
        case -101: return "Internal instrument communication failed";
        case -102: return "Too many data to transfer from the instrument";
        case -103: return "Selected channel(s) unplugged (device error)";
        case -104: return "Instrument response error";
        case -105: return "Invalid message size";
        
        // Communication errors
        case -200: return "Communication failed with the instrument";
        case -201: return "Cannot establish connection with the instrument";
        case -202: return "Waiting for the instrument response";
        case -203: return "Invalid IP address";
        case -204: return "Cannot allocate memory in the instrument";
        case -205: return "Cannot load firmware into selected channel(s)";
        case -206: return "Communication firmware not compatible with the library";
        case -207: return "Maximum number of allowed connections reached";
        
        // Firmware errors
        case -300: return "Cannot find kernel.bin file";
        case -301: return "Cannot read kernel.bin file";
        case -302: return "Invalid kernel.bin file";
        case -303: return "Cannot load kernel.bin on the selected channel(s)";
        case -304: return "Cannot find x100_01.txt file";
        case -305: return "Cannot read x100_01.txt file";
        case -306: return "Invalid x100_01.txt file";
        case -307: return "Cannot load x100_01.txt file on the selected channel(s)";
        case -308: return "No firmware loaded on the selected channel(s)";
        case -309: return "Loaded firmware not compatible with the library";
        
        // Technique errors
        case -400: return "Cannot find the selected ECC file";
        case -401: return "ECC file not compatible with the channel firmware";
        case -402: return "ECC file corrupted";
        case -403: return "Cannot load the ECC file";
        case -404: return "Data returned by the instrument are corrupted";
        case -405: return "Cannot load techniques: full memory";
        
        default: return "Unknown Biologic error";
    }
}

// ============================================================================
// Wrapper Functions for EClib.dll with Auto-initialization
// ============================================================================

// Connection functions
int BL_Connect(const char* address, uint8_t timeout, int* pID, TDeviceInfos_t* pInfos) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_Connect == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_Connect(address, timeout, pID, pInfos);
}

int BL_Disconnect(int ID) {
    if (!IsBioLogicInitialized() || g_BL_Disconnect == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_Disconnect(ID);
}

int BL_TestConnection(int ID) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_TestConnection == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_TestConnection(ID);
}

int BL_TestCommSpeed(int ID, uint8_t channel, int* spd_rcvt, int* spd_kernel) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_TestCommSpeed == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_TestCommSpeed(ID, channel, spd_rcvt, spd_kernel);
}

// General functions
int BL_GetLibVersion(char* pVersion, unsigned int* psize) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_GetLibVersion == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_GetLibVersion(pVersion, psize);
}

unsigned int BL_GetVolumeSerialNumber(void) {
    if (BL_EnsureInitialized() != SUCCESS) return 0;
    if (g_BL_GetVolumeSerialNumber == NULL) return 0;
    return g_BL_GetVolumeSerialNumber();
}

int BL_GetErrorMsg(int errorcode, char* pmsg, unsigned int* psize) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_GetErrorMsg == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_GetErrorMsg(errorcode, pmsg, psize);
}

int BL_GetUSBdeviceinfos(unsigned int USBindex, char* pcompany, unsigned int* pcompanysize, 
                         char* pdevice, unsigned int* pdevicesize, char* pSN, unsigned int* pSNsize) {
    if (BL_EnsureInitialized() != SUCCESS) return -1;
    if (g_BL_GetUSBdeviceinfos == NULL) return -1;
    return g_BL_GetUSBdeviceinfos(USBindex, pcompany, pcompanysize, pdevice, pdevicesize, pSN, pSNsize) ? 0 : -1;
}

// Firmware functions
int BL_LoadFirmware(int ID, uint8_t* pChannels, int* pResults, uint8_t Length, 
                    bool ShowGauge, bool ForceReload, const char* BinFile, const char* XlxFile) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_LoadFirmware == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_LoadFirmware(ID, pChannels, pResults, Length, ShowGauge, ForceReload, BinFile, XlxFile);
}

int BL_LoadFlash(int ID, const char* pfname, bool ShowGauge) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_LoadFlash == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_LoadFlash(ID, pfname, ShowGauge);
}

// Channel information functions
bool BL_IsChannelPlugged(int ID, uint8_t ch) {
    if (BL_EnsureInitialized() != SUCCESS) return false;
    if (g_BL_IsChannelPlugged == NULL) return false;
    return g_BL_IsChannelPlugged(ID, ch);
}

int BL_GetChannelsPlugged(int ID, uint8_t* pChPlugged, uint8_t Size) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_GetChannelsPlugged == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_GetChannelsPlugged(ID, pChPlugged, Size);
}

int BL_GetChannelInfos(int ID, uint8_t ch, TChannelInfos_t* pInfos) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_GetChannelInfos == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_GetChannelInfos(ID, ch, pInfos);
}

int BL_GetMessage(int ID, uint8_t ch, char* msg, unsigned int* size) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_GetMessage == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_GetMessage(ID, ch, msg, size);
}

int BL_GetHardConf(int ID, uint8_t ch, THardwareConf_t* pHardConf) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_GetHardConf == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_GetHardConf(ID, ch, pHardConf);
}

int BL_SetHardConf(int ID, uint8_t ch, THardwareConf_t HardConf) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_SetHardConf == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_SetHardConf(ID, ch, HardConf);
}

int BL_GetChannelBoardType(int ID, uint8_t Channel, uint32_t* pChannelType) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_GetChannelBoardType == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_GetChannelBoardType(ID, Channel, pChannelType);
}

// Module functions
bool BL_IsModulePlugged(int ID, uint8_t module) {
    if (BL_EnsureInitialized() != SUCCESS) return false;
    if (g_BL_IsModulePlugged == NULL) return false;
    return g_BL_IsModulePlugged(ID, module);
}

int BL_GetModulesPlugged(int ID, uint8_t* pModPlugged, uint8_t Size) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_GetModulesPlugged == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_GetModulesPlugged(ID, pModPlugged, Size);
}

int BL_GetModuleInfos(int ID, uint8_t module, void* pInfos) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_GetModuleInfos == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_GetModuleInfos(ID, module, pInfos);
}

// Technique functions
int BL_LoadTechnique(int ID, uint8_t channel, const char* pFName, TEccParams_t Params, 
                     bool FirstTechnique, bool LastTechnique, bool DisplayParams) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_LoadTechnique == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_LoadTechnique(ID, channel, pFName, Params, FirstTechnique, LastTechnique, DisplayParams);
}

int BL_DefineBoolParameter(const char* lbl, bool value, int index, TEccParam_t* pParam) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_DefineBoolParameter == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_DefineBoolParameter(lbl, value, index, pParam);
}

int BL_DefineSglParameter(const char* lbl, float value, int index, TEccParam_t* pParam) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_DefineSglParameter == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_DefineSglParameter(lbl, value, index, pParam);
}

int BL_DefineIntParameter(const char* lbl, int value, int index, TEccParam_t* pParam) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_DefineIntParameter == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_DefineIntParameter(lbl, value, index, pParam);
}

int BL_UpdateParameters(int ID, uint8_t channel, int TechIndx, TEccParams_t Params, const char* EccFileName) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_UpdateParameters == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_UpdateParameters(ID, channel, TechIndx, Params, EccFileName);
}

int BL_GetTechniqueInfos(int ID, uint8_t channel, int TechIndx, void* pInfos) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_GetTechniqueInfos == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_GetTechniqueInfos(ID, channel, TechIndx, pInfos);
}

int BL_GetParamInfos(int ID, uint8_t channel, int TechIndx, int ParamIndx, void* pInfos) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_GetParamInfos == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_GetParamInfos(ID, channel, TechIndx, ParamIndx, pInfos);
}

// Start/Stop functions
int BL_StartChannel(int ID, uint8_t channel) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_StartChannel == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_StartChannel(ID, channel);
}

int BL_StartChannels(int ID, uint8_t* pChannels, int* pResults, uint8_t length) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_StartChannels == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_StartChannels(ID, pChannels, pResults, length);
}

int BL_StopChannel(int ID, uint8_t channel) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_StopChannel == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_StopChannel(ID, channel);
}

int BL_StopChannels(int ID, uint8_t* pChannels, int* pResults, uint8_t length) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_StopChannels == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_StopChannels(ID, pChannels, pResults, length);
}

// Data functions
int BL_GetCurrentValues(int ID, uint8_t channel, TCurrentValues_t* pValues) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_GetCurrentValues == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_GetCurrentValues(ID, channel, pValues);
}

int BL_GetData(int ID, uint8_t channel, TDataBuffer_t* pBuf, TDataInfos_t* pInfos, TCurrentValues_t* pValues) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_GetData == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_GetData(ID, channel, pBuf, pInfos, pValues);
}

int BL_GetFCTData(int ID, uint8_t channel, TDataBuffer_t* pBuf, TDataInfos_t* pInfos, TCurrentValues_t* pValues) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_GetFCTData == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_GetFCTData(ID, channel, pBuf, pInfos, pValues);
}

int BL_ConvertNumericIntoSingle(unsigned int num, float* psgl) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_ConvertNumericIntoSingle == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_ConvertNumericIntoSingle(num, psgl);
}

int BL_ConvertChannelNumericIntoSingle(uint32_t num, float* pRetFloat, uint32_t ChannelType) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_ConvertChannelNumericIntoSingle == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_ConvertChannelNumericIntoSingle(num, pRetFloat, ChannelType);
}

int BL_ConvertTimeChannelNumericIntoSeconds(uint32_t* pnum, double* pRetTime, float Timebase, uint32_t ChannelType) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_ConvertTimeChannelNumericIntoSeconds == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_ConvertTimeChannelNumericIntoSeconds(pnum, pRetTime, Timebase, ChannelType);
}

// Additional data functions
int BL_GetCurrentValuesBk(int ID, uint8_t channel, void* pValues) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_GetCurrentValuesBk == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_GetCurrentValuesBk(ID, channel, pValues);
}

int BL_GetDataBk(int ID, uint8_t channel, void* pBuf, void* pInfos, void* pValues) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_GetDataBk == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_GetDataBk(ID, channel, pBuf, pInfos, pValues);
}

int BL_GetData_LV(int ID, uint8_t channel, void* pBuf, void* pInfos, void* pValues) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_GetData_LV == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_GetData_LV(ID, channel, pBuf, pInfos, pValues);
}

int BL_GetData_VEE(int ID, uint8_t channel, void* pBuf, void* pInfos, void* pValues) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_GetData_VEE == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_GetData_VEE(ID, channel, pBuf, pInfos, pValues);
}

// Experiment functions
int BL_SetExperimentInfos(int ID, uint8_t channel, TExperimentInfos_t TExpInfos) {
    int result = BL_EnsureInitialized();
    if (result != SUCCESS) return result;
    if (g_BL_SetExperimentInfos == NULL) return BL_ERR_LIBRARYNOTLOADED;
    return g_BL_SetExperimentInfos(ID, channel, TExpInfos);
}

int BL_GetExperimentInfos(int ID, uint8_t channel, TExperimentInfos_t* TExpInfos) {
   int result = BL_EnsureInitialized();
   if (result != SUCCESS) return result;
   if (g_BL_GetExperimentInfos == NULL) return BL_ERR_LIBRARYNOTLOADED;
   return g_BL_GetExperimentInfos(ID, channel, TExpInfos);
}

// Advanced functions
int BL_SendMsg(int ID, uint8_t ch, void* pBuf, unsigned int* pLen) {
   int result = BL_EnsureInitialized();
   if (result != SUCCESS) return result;
   if (g_BL_SendMsg == NULL) return BL_ERR_LIBRARYNOTLOADED;
   return g_BL_SendMsg(ID, ch, pBuf, pLen);
}

int BL_SendMsgToRcvt(int ID, void* pBuf, unsigned int* pLen) {
   int result = BL_EnsureInitialized();
   if (result != SUCCESS) return result;
   if (g_BL_SendMsgToRcvt == NULL) return BL_ERR_LIBRARYNOTLOADED;
   return g_BL_SendMsgToRcvt(ID, pBuf, pLen);
}

int BL_SendMsgToRcvt_g(int ID, uint8_t ch, void* pBuf, unsigned int* pLen) {
   int result = BL_EnsureInitialized();
   if (result != SUCCESS) return result;
   if (g_BL_SendMsgToRcvt_g == NULL) return BL_ERR_LIBRARYNOTLOADED;
   return g_BL_SendMsgToRcvt_g(ID, ch, pBuf, pLen);
}

int BL_SendEcalMsg(int ID, uint8_t ch, void* pBuf, unsigned int* pLen) {
   int result = BL_EnsureInitialized();
   if (result != SUCCESS) return result;
   if (g_BL_SendEcalMsg == NULL) return BL_ERR_LIBRARYNOTLOADED;
   return g_BL_SendEcalMsg(ID, ch, pBuf, pLen);
}

int BL_SendEcalMsgGroup(int ID, uint8_t* pChannels, uint8_t length, void* pBuf, unsigned int* pLen) {
   int result = BL_EnsureInitialized();
   if (result != SUCCESS) return result;
   if (g_BL_SendEcalMsgGroup == NULL) return BL_ERR_LIBRARYNOTLOADED;
   return g_BL_SendEcalMsgGroup(ID, pChannels, length, pBuf, pLen);
}

// Additional functions
int BL_GetFPGAVer(int ID, uint8_t channel, uint32_t* pVersion) {
   int result = BL_EnsureInitialized();
   if (result != SUCCESS) return result;
   if (g_BL_GetFPGAVer == NULL) return BL_ERR_LIBRARYNOTLOADED;
   return g_BL_GetFPGAVer(ID, channel, pVersion);
}

int BL_GetOptErr(int ID, uint8_t channel, int* pOptErr, int* pOptPos) {
   int result = BL_EnsureInitialized();
   if (result != SUCCESS) return result;
   if (g_BL_GetOptErr == NULL) return BL_ERR_LIBRARYNOTLOADED;
   return g_BL_GetOptErr(ID, channel, pOptErr, pOptPos);
}

int BL_ReadParameters(int ID, uint8_t channel, void* pParams) {
   int result = BL_EnsureInitialized();
   if (result != SUCCESS) return result;
   if (g_BL_ReadParameters == NULL) return BL_ERR_LIBRARYNOTLOADED;
   return g_BL_ReadParameters(ID, channel, pParams);
}

int BL_GetChannelFloatFormat(int ID, uint8_t channel, int* pFormat) {
   int result = BL_EnsureInitialized();
   if (result != SUCCESS) return result;
   if (g_BL_GetChannelFloatFormat == NULL) return BL_ERR_LIBRARYNOTLOADED;
   return g_BL_GetChannelFloatFormat(ID, channel, pFormat);
}

int BL_ConvertNumericIntoFloat(unsigned int num, double* pdbl) {
   int result = BL_EnsureInitialized();
   if (result != SUCCESS) return result;
   if (g_BL_ConvertNumericIntoFloat == NULL) return BL_ERR_LIBRARYNOTLOADED;
   return g_BL_ConvertNumericIntoFloat(num, pdbl);
}

int BL_ConvertTimeChannelNumericIntoTimebases(uint32_t* pnum, double* pRetTime, float* pTimebases, uint32_t ChannelType) {
   int result = BL_EnsureInitialized();
   if (result != SUCCESS) return result;
   if (g_BL_ConvertTimeChannelNumericIntoTimebases == NULL) return BL_ERR_LIBRARYNOTLOADED;
   return g_BL_ConvertTimeChannelNumericIntoTimebases(pnum, pRetTime, pTimebases, ChannelType);
}

// Technique loading variants
int BL_LoadTechnique_LV(int ID, uint8_t channel, const char* pFName, void* Params, 
                       bool FirstTechnique, bool LastTechnique, bool DisplayParams) {
   int result = BL_EnsureInitialized();
   if (result != SUCCESS) return result;
   if (g_BL_LoadTechnique_LV == NULL) return BL_ERR_LIBRARYNOTLOADED;
   return g_BL_LoadTechnique_LV(ID, channel, pFName, Params, FirstTechnique, LastTechnique, DisplayParams);
}

int BL_LoadTechnique_VEE(int ID, uint8_t channel, const char* pFName, void* Params, 
                        bool FirstTechnique, bool LastTechnique, bool DisplayParams) {
   int result = BL_EnsureInitialized();
   if (result != SUCCESS) return result;
   if (g_BL_LoadTechnique_VEE == NULL) return BL_ERR_LIBRARYNOTLOADED;
   return g_BL_LoadTechnique_VEE(ID, channel, pFName, Params, FirstTechnique, LastTechnique, DisplayParams);
}

int BL_UpdateParameters_LV(int ID, uint8_t channel, int TechIndx, void* Params, const char* EccFileName) {
   int result = BL_EnsureInitialized();
   if (result != SUCCESS) return result;
   if (g_BL_UpdateParameters_LV == NULL) return BL_ERR_LIBRARYNOTLOADED;
   return g_BL_UpdateParameters_LV(ID, channel, TechIndx, Params, EccFileName);
}

int BL_UpdateParameters_VEE(int ID, uint8_t channel, int TechIndx, void* Params, const char* EccFileName) {
   int result = BL_EnsureInitialized();
   if (result != SUCCESS) return result;
   if (g_BL_UpdateParameters_VEE == NULL) return BL_ERR_LIBRARYNOTLOADED;
   return g_BL_UpdateParameters_VEE(ID, channel, TechIndx, Params, EccFileName);
}

// ============================================================================
// Wrapper Functions for blfind.dll
// ============================================================================

int BL_FindEChemDev(char* pLstDev, unsigned int* pSize, unsigned int* pNbrDevice) {
   if (!IsBLFindInitialized() || g_BL_FindEChemDev == NULL) return BL_ERR_LIBRARYNOTLOADED;
   return g_BL_FindEChemDev(pLstDev, pSize, pNbrDevice);
}

int BL_FindEChemEthDev(char* pLstDev, unsigned int* pSize, unsigned int* pNbrDevice) {
   if (!IsBLFindInitialized() || g_BL_FindEChemEthDev == NULL) return BL_ERR_LIBRARYNOTLOADED;
   return g_BL_FindEChemEthDev(pLstDev, pSize, pNbrDevice);
}

int BL_FindEChemUsbDev(char* pLstDev, unsigned int* pSize, unsigned int* pNbrDevice) {
   if (!IsBLFindInitialized() || g_BL_FindEChemUsbDev == NULL) return BL_ERR_LIBRARYNOTLOADED;
   return g_BL_FindEChemUsbDev(pLstDev, pSize, pNbrDevice);
}

int BL_SetConfig(char* pIp, char* pCfg) {
   if (!IsBLFindInitialized() || g_BL_SetConfig == NULL) return BL_ERR_LIBRARYNOTLOADED;
   return g_BL_SetConfig(pIp, pCfg);
}

// Additional blfind functions
int BL_FindEChemBCSDev(char* pLstDev, unsigned int* pSize, unsigned int* pNbrDevice) {
   if (!IsBLFindInitialized() || g_BL_FindEChemBCSDev == NULL) return BL_ERR_LIBRARYNOTLOADED;
   return g_BL_FindEChemBCSDev(pLstDev, pSize, pNbrDevice);
}

int BL_EChemBCSEthDEV(void* param1, void* param2) {
   if (!IsBLFindInitialized() || g_BL_EChemBCSEthDEV == NULL) return BL_ERR_LIBRARYNOTLOADED;
   return g_BL_EChemBCSEthDEV(param1, param2);
}

int BL_FindKineticDev(char* pLstDev, unsigned int* pSize, unsigned int* pNbrDevice) {
   if (!IsBLFindInitialized() || g_BL_FindKineticDev == NULL) return BL_ERR_LIBRARYNOTLOADED;
   return g_BL_FindKineticDev(pLstDev, pSize, pNbrDevice);
}

int BL_FindKineticEthDev(char* pLstDev, unsigned int* pSize, unsigned int* pNbrDevice) {
   if (!IsBLFindInitialized() || g_BL_FindKineticEthDev == NULL) return BL_ERR_LIBRARYNOTLOADED;
   return g_BL_FindKineticEthDev(pLstDev, pSize, pNbrDevice);
}

int BL_FindKineticUsbDev(char* pLstDev, unsigned int* pSize, unsigned int* pNbrDevice) {
   if (!IsBLFindInitialized() || g_BL_FindKineticUsbDev == NULL) return BL_ERR_LIBRARYNOTLOADED;
   return g_BL_FindKineticUsbDev(pLstDev, pSize, pNbrDevice);
}

int BL_Init_Path(const char* path) {
   if (!IsBLFindInitialized() || g_BL_Init_Path == NULL) return BL_ERR_LIBRARYNOTLOADED;
   return g_BL_Init_Path(path);
}

int BL_SetMAC(char* mac) {
   if (!IsBLFindInitialized() || g_BL_SetMAC == NULL) return BL_ERR_LIBRARYNOTLOADED;
   return g_BL_SetMAC(mac);
}

// BLFind error message function
int BLFind_GetErrorMsg(int errorcode, char* pmsg, unsigned int* psize) {
   if (!IsBLFindInitialized() || g_BLFind_GetErrorMsg == NULL) return BL_ERR_LIBRARYNOTLOADED;
   return g_BLFind_GetErrorMsg(errorcode, pmsg, psize);
}

// ============================================================================
// Scanning Implementation
// ============================================================================

// Scan for BioLogic devices using blfind.dll
int ScanForBioLogicDevices(void) {
   char deviceList[4096];
   char asciiDeviceList[2048];
   unsigned int bufferSize;
   unsigned int deviceCount;
   int result;
   
   LogMessageEx(LOG_DEVICE_BIO, "=== Scanning for BioLogic Devices ===");
   
   // Initialize blfind.dll
   if (InitializeBLFind() != 0) {
       LogErrorEx(LOG_DEVICE_BIO, "Failed to initialize blfind.dll");
       return ERR_NOT_INITIALIZED;
   }
   
   // Initialize EClib.dll too if needed
   if (!IsBioLogicInitialized()) {
       if (InitializeBioLogic() != 0) {
           LogErrorEx(LOG_DEVICE_BIO, "Failed to initialize EClib.dll");
           CleanupBLFind();
           return ERR_NOT_INITIALIZED;
       }
   }
   
   // Scan for USB devices
   if (g_BL_FindEChemUsbDev != NULL) {
       LogMessageEx(LOG_DEVICE_BIO, "Scanning for USB devices...");
       memset(deviceList, 0, sizeof(deviceList));
       bufferSize = sizeof(deviceList);
       deviceCount = 0;
       
       result = BL_FindEChemUsbDev(deviceList, &bufferSize, &deviceCount);
       
       if (result == 0) {
           LogMessageEx(LOG_DEVICE_BIO, "Found %d USB device(s)", deviceCount);
           if (deviceCount > 0) {
               // Convert from Unicode to ASCII
               ConvertUnicodeToAscii(deviceList, asciiDeviceList, bufferSize);
               LogMessageEx(LOG_DEVICE_BIO, "Device string: %s", asciiDeviceList);
               
               // Parse the device string
               // Format appears to be: USB$0$$$SP-150e$[serial]$...
               char* deviceCopy = my_strdup(asciiDeviceList);
               char* token = strtok(deviceCopy, "$");
               int fieldCount = 0;
               char connectionType[32] = "";
               char portNumber[32] = "";
               char deviceType[64] = "";
               
               while (token != NULL) {
                   switch(fieldCount) {
                       case 0: // Connection type (USB)
                           strcpy(connectionType, token);
                           break;
                       case 1: // Port number
                           strcpy(portNumber, token);
                           break;
                       case 6: // Device type (after several empty fields)
                           strcpy(deviceType, token);
                           break;
                   }
                   LogDebugEx(LOG_DEVICE_BIO, "  Field %d: %s", fieldCount, token);
                   token = strtok(NULL, "$");
                   fieldCount++;
               }
               
               free(deviceCopy);
               
               LogMessageEx(LOG_DEVICE_BIO, "Parsed information:");
               LogMessageEx(LOG_DEVICE_BIO, "  Connection: %s", connectionType);
               LogMessageEx(LOG_DEVICE_BIO, "  Port: %s", portNumber);
               LogMessageEx(LOG_DEVICE_BIO, "  Device: %s", deviceType);
               
               LogMessageEx(LOG_DEVICE_BIO, "*** Try connecting with: \"USB%s\" ***", portNumber);
           }
       } else {
           LogErrorEx(LOG_DEVICE_BIO, "USB scan error: %d", result);
           
           // Try to get error message from blfind
           if (g_BLFind_GetErrorMsg != NULL) {
               char errorMsg[256];
               unsigned int msgSize = sizeof(errorMsg);
               if (BLFind_GetErrorMsg(result, errorMsg, &msgSize) == 0) {
                   LogErrorEx(LOG_DEVICE_BIO, "BLFind error: %s", errorMsg);
               }
           }
       }
   }
   
   // Scan for Ethernet devices
   if (g_BL_FindEChemEthDev != NULL) {
       LogMessageEx(LOG_DEVICE_BIO, "Scanning for Ethernet devices...");
       memset(deviceList, 0, sizeof(deviceList));
       bufferSize = sizeof(deviceList);
       deviceCount = 0;
       
       result = BL_FindEChemEthDev(deviceList, &bufferSize, &deviceCount);
       
       if (result == 0) {
           LogMessageEx(LOG_DEVICE_BIO, "Found %d Ethernet device(s)", deviceCount);
           if (deviceCount > 0) {
               ConvertUnicodeToAscii(deviceList, asciiDeviceList, bufferSize);
               LogMessageEx(LOG_DEVICE_BIO, "Device string: %s", asciiDeviceList);
           }
       } else {
           LogErrorEx(LOG_DEVICE_BIO, "Ethernet scan error: %d", result);
       }
   }
   
   // Scan for BCS devices (Battery Cycling Systems)
   if (g_BL_FindEChemBCSDev != NULL) {
       LogMessageEx(LOG_DEVICE_BIO, "Scanning for BCS devices...");
       memset(deviceList, 0, sizeof(deviceList));
       bufferSize = sizeof(deviceList);
       deviceCount = 0;
       
       result = BL_FindEChemBCSDev(deviceList, &bufferSize, &deviceCount);
       
       if (result == 0) {
           LogMessageEx(LOG_DEVICE_BIO, "Found %d BCS device(s)", deviceCount);
           if (deviceCount > 0) {
               ConvertUnicodeToAscii(deviceList, asciiDeviceList, bufferSize);
               LogMessageEx(LOG_DEVICE_BIO, "Device string: %s", asciiDeviceList);
           }
       } else {
           LogErrorEx(LOG_DEVICE_BIO, "BCS scan error: %d", result);
       }
   }
   
   // Scan for Kinetic devices
   if (g_BL_FindKineticDev != NULL) {
       LogMessageEx(LOG_DEVICE_BIO, "Scanning for Kinetic devices...");
       memset(deviceList, 0, sizeof(deviceList));
       bufferSize = sizeof(deviceList);
       deviceCount = 0;
       
       result = BL_FindKineticDev(deviceList, &bufferSize, &deviceCount);
       
       if (result == 0) {
           LogMessageEx(LOG_DEVICE_BIO, "Found %d Kinetic device(s)", deviceCount);
           if (deviceCount > 0) {
               ConvertUnicodeToAscii(deviceList, asciiDeviceList, bufferSize);
               LogMessageEx(LOG_DEVICE_BIO, "Device string: %s", asciiDeviceList);
           }
       } else {
           LogErrorEx(LOG_DEVICE_BIO, "Kinetic scan error: %d", result);
       }
   }
   
   LogMessageEx(LOG_DEVICE_BIO, "=== Scan Complete ===");
   
   return SUCCESS;
}

// ============================================================================
// High-Level Technique Functions - State Machine Implementation
// ============================================================================

// Create a technique context
BL_TechniqueContext* BL_CreateTechniqueContext(int ID, uint8_t channel, BioTechniqueType type) {
    BL_TechniqueContext *context = calloc(1, sizeof(BL_TechniqueContext));
    if (!context) return NULL;
    
    context->deviceID = ID;
    context->channel = channel;
    context->state = BIO_TECH_STATE_IDLE;
    context->config.type = type;
    context->startTime = Timer();
    context->lastUpdateTime = context->startTime;
    
    return context;
}

void BL_FreeTechniqueContext(BL_TechniqueContext *context) {
    if (!context) return;
    
    // Free parameter copy
    if (context->config.paramsCopy) {
        free(context->config.paramsCopy);
        context->config.paramsCopy = NULL;
    }
    
    // Free raw data buffer
    if (context->rawData.rawData) {
        free(context->rawData.rawData);
        context->rawData.rawData = NULL;
    }
    
    // Free converted data if owned by context
    if (context->convertedData) {
        BL_FreeConvertedData(context->convertedData);
        context->convertedData = NULL;
    }
    
    free(context);
}

// Update technique state machine
int BL_UpdateTechnique(BL_TechniqueContext *context) {
    if (!context) return BL_ERR_INVALIDPARAMETERS;
    
    int result;
    TCurrentValues_t currentValues;
    
    // Update timestamp
    context->lastUpdateTime = Timer();
    context->updateCount++;
    
    switch (context->state) {
        case BIO_TECH_STATE_LOADING:
            // Check if channel is ready
            result = BL_GetCurrentValues(context->deviceID, context->channel, &currentValues);
            if (result != SUCCESS) {
                context->lastError = result;
                context->state = BIO_TECH_STATE_ERROR;
                return result;
            }
            
            // If channel is running, move to running state
            if (currentValues.State == KBIO_STATE_RUN) {
                context->state = BIO_TECH_STATE_RUNNING;
                context->memFilledAtStart = currentValues.MemFilled;
                LogDebugEx(LOG_DEVICE_BIO, "Technique started, initial MemFilled: %d", context->memFilledAtStart);
            }
            break;
            
        case BIO_TECH_STATE_RUNNING:
            // Get current values
            result = BL_GetCurrentValues(context->deviceID, context->channel, &currentValues);
            if (result != SUCCESS) {
                context->lastError = result;
                context->state = BIO_TECH_STATE_ERROR;
                return result;
            }
            
            context->lastCurrentValues = currentValues;
            
            // Check for errors
            if (currentValues.OptErr != 0) {
                LogWarningEx(LOG_DEVICE_BIO, "Hardware option error: %d at position %d", 
                           currentValues.OptErr, currentValues.OptPos);
            }
            
            // Call progress callback if set
            if (context->progressCallback) {
                double elapsed = Timer() - context->startTime;
                context->progressCallback(elapsed, currentValues.MemFilled, context->userData);
            }
            
            // Check if technique completed (state changed to STOP)
            if (currentValues.State == KBIO_STATE_STOP) {
                LogDebugEx(LOG_DEVICE_BIO, "Technique completed, retrieving data...");
                
                // Get data with timeout protection
                TDataBuffer_t dataBuffer;
                TDataInfos_t dataInfo;
                
                // For impedance techniques, try to get the right process data
                bool gotData = false;
                int targetProcessIndex = -1;
                
                // Determine target process based on technique type
                if (context->config.type == BIO_TECHNIQUE_PEIS || 
                    context->config.type == BIO_TECHNIQUE_SPEIS ||
                    context->config.type == BIO_TECHNIQUE_GEIS || 
                    context->config.type == BIO_TECHNIQUE_SGEIS) {
                    targetProcessIndex = 1;  // Impedance data
                } else {
                    targetProcessIndex = 0;  // Default to process 0
                }
                
                // Try to get data up to 3 times
                for (int attempt = 0; attempt < 3; attempt++) {
                    // Check if there's data available
                    if (currentValues.MemFilled == 0) {
                        LogWarningEx(LOG_DEVICE_BIO, "No data in device memory");
                        break;
                    }
                    
                    result = BL_GetData(context->deviceID, context->channel, 
                                      &dataBuffer, &dataInfo, &currentValues);
                    
                    if (result == SUCCESS) {
                        LogDebugEx(LOG_DEVICE_BIO, "Retrieved data - TechniqueID: %d, ProcessIndex: %d, Points: %d, Cols: %d",
                                 dataInfo.TechniqueID, dataInfo.ProcessIndex, dataInfo.NbRows, dataInfo.NbCols);
                        
                        // Check if this is the process we want
                        if (targetProcessIndex == -1 || dataInfo.ProcessIndex == targetProcessIndex) {
                            // This is the data we want
                            int dataSize = dataInfo.NbRows * dataInfo.NbCols;
                            
                            // Free any existing raw data first
                            if (context->rawData.rawData) {
                                free(context->rawData.rawData);
                                context->rawData.rawData = NULL;
                            }
                            
                            // Allocate new buffer for raw data
                            context->rawData.rawData = malloc(dataSize * sizeof(unsigned int));
                            
                            if (context->rawData.rawData) {
                                // Copy raw data
                                memcpy(context->rawData.rawData, dataBuffer.data, 
                                       dataSize * sizeof(unsigned int));
                                context->rawData.bufferSize = dataSize;
                                context->rawData.numPoints = dataInfo.NbRows;
                                context->rawData.numVariables = dataInfo.NbCols;
                                context->rawData.techniqueID = dataInfo.TechniqueID;
                                context->rawData.processIndex = dataInfo.ProcessIndex;
                                
                                gotData = true;
                                
                                LogDebugEx(LOG_DEVICE_BIO, "Stored %d data points with %d variables (Process %d)",
                                         dataInfo.NbRows, dataInfo.NbCols, dataInfo.ProcessIndex);
                                
                                // For single-process techniques, break after first data
                                if (targetProcessIndex == 0) {
                                    break;
                                }
                            }
                        } else {
                            LogDebugEx(LOG_DEVICE_BIO, "Skipping process %d data (looking for process %d)",
                                     dataInfo.ProcessIndex, targetProcessIndex);
                        }
                        
                        // For multi-process techniques, continue if we haven't found target process
                        if (gotData && dataInfo.ProcessIndex == targetProcessIndex) {
                            break;
                        }
                    } else if (result == BL_ERR_TECH_DATACORRUPTED) {
                        LogWarningEx(LOG_DEVICE_BIO, "Data corrupted on attempt %d", attempt + 1);
                        // Don't retry on data corruption
                        break;
                    } else {
                        LogWarningEx(LOG_DEVICE_BIO, "Failed to get data on attempt %d: %s", 
                                   attempt + 1, BL_GetErrorString(result));
                        // For other errors, break immediately
                        break;
                    }
                }
                
                // Process data if requested and we have data
                if (context->processData && gotData && context->rawData.rawData && context->rawData.numPoints > 0) {
                    // Get channel type for conversion
                    uint32_t channelType;
                    result = BL_GetChannelBoardType(context->deviceID, context->channel, &channelType);
                    
                    if (result == SUCCESS) {
                        float timebase = currentValues.TimeBase;
                        
                        // Free any existing converted data first
                        if (context->convertedData) {
                            BL_FreeConvertedData(context->convertedData);
                            context->convertedData = NULL;
                        }
                        
                        // Attempt to process the data
                        result = BL_ProcessTechniqueData(&context->rawData, 
                                                       context->rawData.techniqueID,
                                                       context->rawData.processIndex,
                                                       channelType,
                                                       timebase,
                                                       &context->convertedData);
                        
                        if (result == SUCCESS) {
                            LogDebugEx(LOG_DEVICE_BIO, "Data processed: %d variables converted",
                                     context->convertedData->numVariables);
                        } else {
                            LogWarningEx(LOG_DEVICE_BIO, "Failed to process data: %d", result);
                        }
                    }
                }
                
                // Call data callback if set
                if (context->dataCallback && gotData) {
                    TDataInfos_t info = {
                        .NbRows = context->rawData.numPoints,
                        .NbCols = context->rawData.numVariables,
                        .TechniqueID = context->rawData.techniqueID,
                        .ProcessIndex = context->rawData.processIndex
                    };
                    context->dataCallback(&info, context->userData);
                }
                
                // Determine final state
                if (gotData) {
                    context->state = BIO_TECH_STATE_COMPLETED;
                } else if (currentValues.OptErr != 0) {
                    context->lastError = currentValues.OptErr;
                    snprintf(context->errorMessage, sizeof(context->errorMessage),
                           "Technique stopped with OptErr=%d", currentValues.OptErr);
                    context->state = BIO_TECH_STATE_ERROR;
                } else {
                    context->lastError = BL_ERR_FUNCTIONFAILED;
                    snprintf(context->errorMessage, sizeof(context->errorMessage),
                           "No data retrieved from technique");
                    context->state = BIO_TECH_STATE_ERROR;
                }
            }
            break;
            
        case BIO_TECH_STATE_COMPLETED:
        case BIO_TECH_STATE_ERROR:
        case BIO_TECH_STATE_CANCELLED:
            // Terminal states - no update needed
            break;
            
        default:
            LogWarningEx(LOG_DEVICE_BIO, "Unknown technique state: %d", context->state);
            break;
    }
    
    return SUCCESS;
}

// Check if technique is complete
bool BL_IsTechniqueComplete(BL_TechniqueContext *context) {
    if (!context) return true;
    
    return (context->state == BIO_TECH_STATE_COMPLETED ||
            context->state == BIO_TECH_STATE_ERROR ||
            context->state == BIO_TECH_STATE_CANCELLED);
}

// Stop technique
int BL_StopTechnique(BL_TechniqueContext *context) {
    if (!context) return BL_ERR_INVALIDPARAMETERS;
    
    int result = BL_StopChannel(context->deviceID, context->channel);
    
    if (context->state == BIO_TECH_STATE_RUNNING || 
        context->state == BIO_TECH_STATE_LOADING) {
        context->state = BIO_TECH_STATE_CANCELLED;
    }
    
    return result;
}

// Get raw data
int BL_GetTechniqueRawData(BL_TechniqueContext *context, BL_RawDataBuffer **data) {
    if (!context || !data) return BL_ERR_INVALIDPARAMETERS;
    
    if (context->rawData.rawData && context->rawData.numPoints > 0) {
        *data = &context->rawData;
        return SUCCESS;
    }
    
    return BL_ERR_FUNCTIONFAILED;
}

int BL_ProcessTechniqueData(BL_RawDataBuffer *rawData, int techniqueID, int processIndex,
                           uint32_t channelType, float timebase,
                           BL_ConvertedData **convertedData) {
    if (!rawData || !convertedData || rawData->numPoints == 0) {
        return BL_ERR_INVALIDPARAMETERS;
    }
    
    // Allocate converted data structure
    BL_ConvertedData *converted = calloc(1, sizeof(BL_ConvertedData));
    if (!converted) return BL_ERR_FUNCTIONFAILED;
    
    converted->techniqueID = techniqueID;
    converted->processIndex = processIndex;
    converted->numPoints = rawData->numPoints;
    
    // Process based on technique type
    switch (techniqueID) {
        case KBIO_TECHID_OCV:  // OCV - technique ID 100
            if (processIndex == 0) {
                // OCV has: time, Ewe, Ece
                converted->numVariables = 3;
                converted->variableNames = malloc(3 * sizeof(char*));
                converted->variableUnits = malloc(3 * sizeof(char*));
                converted->data = malloc(3 * sizeof(double*));
                
                converted->variableNames[0] = my_strdup("Time");
                converted->variableNames[1] = my_strdup("Ewe");
                converted->variableNames[2] = my_strdup("Ece");
                
                converted->variableUnits[0] = my_strdup("s");
                converted->variableUnits[1] = my_strdup("V");
                converted->variableUnits[2] = my_strdup("V");
                
                // Allocate data arrays
                for (int v = 0; v < 3; v++) {
                    converted->data[v] = malloc(rawData->numPoints * sizeof(double));
                }
                
                // Convert data
                for (int i = 0; i < rawData->numPoints; i++) {
                    unsigned int *row = &rawData->rawData[i * rawData->numVariables];
                    
                    // Time (needs special handling - 2 uints)
                    uint32_t timeData[2] = {row[0], row[1]};
                    BL_ConvertTimeChannelNumericIntoSeconds(timeData, &converted->data[0][i], 
                                                           timebase, channelType);
                    
                    // Ewe and Ece
                    float temp;
                    BL_ConvertChannelNumericIntoSingle(row[2], &temp, channelType);
                    converted->data[1][i] = temp;
                    
                    BL_ConvertChannelNumericIntoSingle(row[3], &temp, channelType);
                    converted->data[2][i] = temp;
                }
            }
            break;
            
        case KBIO_TECHID_PEIS:  // PEIS - technique ID 104
        case KBIO_TECHID_GEIS:  // GEIS - technique ID 107 (same format as PEIS)
            if (processIndex == 1) {  // Impedance data
                // PEIS/GEIS impedance data
                converted->numVariables = 11;
                converted->variableNames = malloc(11 * sizeof(char*));
                converted->variableUnits = malloc(11 * sizeof(char*));
                converted->data = malloc(11 * sizeof(double*));
                
                char *names[] = {"Frequency", "|Ewe|", "|I|", "Phase_Zwe", "Re(Zwe)", "Im(Zwe)", 
                                "Ewe", "I", "|Ece|", "|Ice|", "Time"};
                char *units[] = {"Hz", "V", "A", "deg", "Ohm", "Ohm", 
                                "V", "A", "V", "A", "s"};
                
                for (int v = 0; v < 11; v++) {
                    converted->variableNames[v] = my_strdup(names[v]);
                    converted->variableUnits[v] = my_strdup(units[v]);
                    converted->data[v] = malloc(rawData->numPoints * sizeof(double));
                }
                
                // Convert data
                for (int i = 0; i < rawData->numPoints; i++) {
                    unsigned int *row = &rawData->rawData[i * rawData->numVariables];
                    float temp;
                    
                    // Basic conversions
                    BL_ConvertChannelNumericIntoSingle(row[0], &temp, channelType);  // freq
                    converted->data[0][i] = temp;
                    
                    BL_ConvertChannelNumericIntoSingle(row[1], &temp, channelType);  // |Ewe|
                    converted->data[1][i] = temp;
                    
                    BL_ConvertChannelNumericIntoSingle(row[2], &temp, channelType);  // |I|
                    converted->data[2][i] = temp;
                    
                    BL_ConvertChannelNumericIntoSingle(row[3], &temp, channelType);  // Phase
                    converted->data[3][i] = temp;
                    
                    // Calculate Re(Z) and Im(Z) from magnitude and phase
                    double magnitude = converted->data[1][i] / converted->data[2][i];  // |Ewe|/|I|
                    double phase_rad = converted->data[3][i] * M_PI / 180.0;
                    converted->data[4][i] = magnitude * cos(phase_rad);  // Re(Z)
                    converted->data[5][i] = magnitude * sin(phase_rad);  // Im(Z)
                    
                    BL_ConvertChannelNumericIntoSingle(row[4], &temp, channelType);  // Ewe
                    converted->data[6][i] = temp;
                    
                    BL_ConvertChannelNumericIntoSingle(row[5], &temp, channelType);  // I
                    converted->data[7][i] = temp;
                    
                    BL_ConvertChannelNumericIntoSingle(row[7], &temp, channelType);  // |Ece|
                    converted->data[8][i] = temp;
                    
                    BL_ConvertChannelNumericIntoSingle(row[8], &temp, channelType);  // |Ice|
                    converted->data[9][i] = temp;
                    
                    // Time is at different positions for VMP3 vs VMP-300
                    int timeCol = (rawData->numVariables > 15) ? 13 : 13;  // Adjust if needed
                    BL_ConvertChannelNumericIntoSingle(row[timeCol], &temp, channelType);
                    converted->data[10][i] = temp;
                }
            } else if (processIndex == 0) {
                // Process 0: time series data during stabilization
                converted->numVariables = 3;
                converted->variableNames = malloc(3 * sizeof(char*));
                converted->variableUnits = malloc(3 * sizeof(char*));
                converted->data = malloc(3 * sizeof(double*));
                
                converted->variableNames[0] = my_strdup("Time");
                converted->variableNames[1] = my_strdup("Ewe");
                converted->variableNames[2] = my_strdup("I");
                
                converted->variableUnits[0] = my_strdup("s");
                converted->variableUnits[1] = my_strdup("V");
                converted->variableUnits[2] = my_strdup("A");
                
                for (int v = 0; v < 3; v++) {
                    converted->data[v] = malloc(rawData->numPoints * sizeof(double));
                }
                
                for (int i = 0; i < rawData->numPoints; i++) {
                    unsigned int *row = &rawData->rawData[i * rawData->numVariables];
                    
                    // Time
                    uint32_t timeData[2] = {row[0], row[1]};
                    BL_ConvertTimeChannelNumericIntoSeconds(timeData, &converted->data[0][i], 
                                                           timebase, channelType);
                    
                    // Ewe and I
                    float temp;
                    BL_ConvertChannelNumericIntoSingle(row[2], &temp, channelType);
                    converted->data[1][i] = temp;
                    
                    BL_ConvertChannelNumericIntoSingle(row[3], &temp, channelType);
                    converted->data[2][i] = temp;
                }
            }
            break;
            
        case KBIO_TECHID_SPEIS:  // SPEIS - technique ID 113
        case KBIO_TECHID_SGEIS:  // SGEIS - technique ID 114 (same format as SPEIS)
            if (processIndex == 1) {  // Impedance data with step info
                // Similar to PEIS but with step number
                converted->numVariables = 12;
                converted->variableNames = malloc(12 * sizeof(char*));
                converted->variableUnits = malloc(12 * sizeof(char*));
                converted->data = malloc(12 * sizeof(double*));
                
                char *names[] = {"Frequency", "|Ewe|", "|I|", "Phase_Zwe", "Re(Zwe)", "Im(Zwe)", 
                                "Ewe", "I", "|Ece|", "|Ice|", "Time", "Step"};
                char *units[] = {"Hz", "V", "A", "deg", "Ohm", "Ohm", 
                                "V", "A", "V", "A", "s", ""};
                
                for (int v = 0; v < 12; v++) {
                    converted->variableNames[v] = my_strdup(names[v]);
                    converted->variableUnits[v] = my_strdup(units[v]);
                    converted->data[v] = malloc(rawData->numPoints * sizeof(double));
                }
                
                // Convert data (similar to PEIS but with step number)
                for (int i = 0; i < rawData->numPoints; i++) {
                    unsigned int *row = &rawData->rawData[i * rawData->numVariables];
                    float temp;
                    
                    // Same conversions as PEIS
                    BL_ConvertChannelNumericIntoSingle(row[0], &temp, channelType);
                    converted->data[0][i] = temp;
                    
                    BL_ConvertChannelNumericIntoSingle(row[1], &temp, channelType);
                    converted->data[1][i] = temp;
                    
                    BL_ConvertChannelNumericIntoSingle(row[2], &temp, channelType);
                    converted->data[2][i] = temp;
                    
                    BL_ConvertChannelNumericIntoSingle(row[3], &temp, channelType);
                    converted->data[3][i] = temp;
                    
                    // Calculate Re(Z) and Im(Z)
                    double magnitude = converted->data[1][i] / converted->data[2][i];
                    double phase_rad = converted->data[3][i] * M_PI / 180.0;
                    converted->data[4][i] = magnitude * cos(phase_rad);
                    converted->data[5][i] = magnitude * sin(phase_rad);
                    
                    BL_ConvertChannelNumericIntoSingle(row[4], &temp, channelType);
                    converted->data[6][i] = temp;
                    
                    BL_ConvertChannelNumericIntoSingle(row[5], &temp, channelType);
                    converted->data[7][i] = temp;
                    
                    BL_ConvertChannelNumericIntoSingle(row[7], &temp, channelType);
                    converted->data[8][i] = temp;
                    
                    BL_ConvertChannelNumericIntoSingle(row[8], &temp, channelType);
                    converted->data[9][i] = temp;
                    
                    // Time and step
                    int timeCol = (rawData->numVariables > 16) ? 13 : 13;
                    int stepCol = (rawData->numVariables > 16) ? 15 : 14;
                    
                    BL_ConvertChannelNumericIntoSingle(row[timeCol], &temp, channelType);
                    converted->data[10][i] = temp;
                    
                    converted->data[11][i] = row[stepCol];  // Step number (no conversion needed)
                }
            } else if (processIndex == 0) {
                // Process 0: time series with step
                converted->numVariables = 4;
                converted->variableNames = malloc(4 * sizeof(char*));
                converted->variableUnits = malloc(4 * sizeof(char*));
                converted->data = malloc(4 * sizeof(double*));
                
                converted->variableNames[0] = my_strdup("Time");
                converted->variableNames[1] = my_strdup("Ewe");
                converted->variableNames[2] = my_strdup("I");
                converted->variableNames[3] = my_strdup("Step");
                
                converted->variableUnits[0] = my_strdup("s");
                converted->variableUnits[1] = my_strdup("V");
                converted->variableUnits[2] = my_strdup("A");
                converted->variableUnits[3] = my_strdup("");
                
                for (int v = 0; v < 4; v++) {
                    converted->data[v] = malloc(rawData->numPoints * sizeof(double));
                }
                
                for (int i = 0; i < rawData->numPoints; i++) {
                    unsigned int *row = &rawData->rawData[i * rawData->numVariables];
                    
                    uint32_t timeData[2] = {row[0], row[1]};
                    BL_ConvertTimeChannelNumericIntoSeconds(timeData, &converted->data[0][i], 
                                                           timebase, channelType);
                    
                    float temp;
                    BL_ConvertChannelNumericIntoSingle(row[2], &temp, channelType);
                    converted->data[1][i] = temp;
                    
                    BL_ConvertChannelNumericIntoSingle(row[3], &temp, channelType);
                    converted->data[2][i] = temp;
                    
                    converted->data[3][i] = row[4];  // Step number
                }
            }
            break;
            
        default:
            // Unknown technique - just copy raw data info
            converted->numVariables = rawData->numVariables;
            converted->variableNames = NULL;
            converted->variableUnits = NULL;
            converted->data = NULL;
            break;
    }
    
    *convertedData = converted;
    return SUCCESS;
}

// Helper function to copy raw data buffer
BL_RawDataBuffer* BL_CopyRawDataBuffer(BL_RawDataBuffer *src) {
    if (!src || !src->rawData) return NULL;
    
    BL_RawDataBuffer *copy = malloc(sizeof(BL_RawDataBuffer));
    if (!copy) return NULL;
    
    *copy = *src;  // Copy all fields
    
    // Deep copy the data array
    int dataSize = src->bufferSize * sizeof(unsigned int);
    copy->rawData = malloc(dataSize);
    if (!copy->rawData) {
        free(copy);
        return NULL;
    }
    
    memcpy(copy->rawData, src->rawData, dataSize);
    return copy;
}

void BL_FreeConvertedData(BL_ConvertedData *data) {
    if (!data) return;
    
    // Free variable names and units
    if (data->variableNames) {
        for (int i = 0; i < data->numVariables; i++) {
            if (data->variableNames[i]) free(data->variableNames[i]);
        }
        free(data->variableNames);
    }
    
    if (data->variableUnits) {
        for (int i = 0; i < data->numVariables; i++) {
            if (data->variableUnits[i]) free(data->variableUnits[i]);
        }
        free(data->variableUnits);
    }
    
    // Free data arrays
    if (data->data) {
        for (int i = 0; i < data->numVariables; i++) {
            if (data->data[i]) free(data->data[i]);
        }
        free(data->data);
    }
    
    free(data);
}

void BL_FreeTechniqueData(BL_TechniqueData *data) {
    if (!data) return;
    
    // Free raw data
    if (data->rawData) {
        if (data->rawData->rawData) {
            free(data->rawData->rawData);
        }
        free(data->rawData);
    }
    
    // Free converted data
    if (data->convertedData) {
        BL_FreeConvertedData(data->convertedData);
    }
    
    free(data);
}

int BL_GetTechniqueData(BL_TechniqueContext *context, BL_TechniqueData **data) {
    if (!context || !data) return BL_ERR_INVALIDPARAMETERS;
    
    if (context->rawData.rawData && context->rawData.numPoints > 0) {
        BL_TechniqueData *techData = calloc(1, sizeof(BL_TechniqueData));
        if (!techData) return BL_ERR_FUNCTIONFAILED;
        
        // Copy raw data
        techData->rawData = BL_CopyRawDataBuffer(&context->rawData);
        
        // Copy converted data if available
        if (context->convertedData) {
            // Would need to implement CopyConvertedData function
            techData->convertedData = context->convertedData;
            context->convertedData = NULL;  // Transfer ownership
        }
        
        *data = techData;
        return SUCCESS;
    }
    
    return BL_ERR_FUNCTIONFAILED;
}

// Start OCV measurement
int BL_StartOCV(int ID, uint8_t channel,
                double duration_s,
                double sample_interval_s,
                double record_every_dE,     // mV
                double record_every_dT,     // seconds
                int e_range,                // 0=2.5V, 1=5V, 2=10V, 3=Auto
				bool processData,
                BL_TechniqueContext **context) {
    
    if (!context) return BL_ERR_INVALIDPARAMETERS;
    
    int result;
    
    // Create context
    BL_TechniqueContext *ctx = BL_CreateTechniqueContext(ID, channel, BIO_TECHNIQUE_OCV);
    if (!ctx) return BL_ERR_FUNCTIONFAILED;
    
    // Store key parameters
    ctx->config.key.duration_s = duration_s;
    ctx->config.key.sampleInterval_s = sample_interval_s;
    ctx->config.key.recordEvery_dE = record_every_dE;
    ctx->config.key.recordEvery_dT = record_every_dT;
    ctx->config.key.eRange = e_range;
	ctx->processData = processData;
    
    // Build OCV parameters
    TEccParam_t params[4];
    ctx->config.originalParams.len = 4;
    ctx->config.originalParams.pParams = params;
    
    result = BL_DefineSglParameter("Rest_time_T", (float)duration_s, 0, &params[0]);
    if (result != SUCCESS) goto error;
    
    result = BL_DefineSglParameter("Record_every_dE", (float)record_every_dE, 0, &params[1]);
    if (result != SUCCESS) goto error;
    
    result = BL_DefineSglParameter("Record_every_dT", (float)record_every_dT, 0, &params[2]);
    if (result != SUCCESS) goto error;
    
    result = BL_DefineIntParameter("E_Range", e_range, 0, &params[3]);
    if (result != SUCCESS) goto error;
    
    // Make a copy of parameters
    ctx->config.paramsCopy = malloc(4 * sizeof(TEccParam_t));
    if (ctx->config.paramsCopy) {
        memcpy(ctx->config.paramsCopy, params, 4 * sizeof(TEccParam_t));
        ctx->config.originalParams.pParams = ctx->config.paramsCopy;
    }
    
    // Store technique file
    strcpy(ctx->config.eccFile, "lib\\ocv.ecc");
    
    // Stop channel if running
    result = BL_StopChannel(ID, channel);
    if (result != SUCCESS && result != BL_ERR_CHANNELNOTPLUGGED) {
        LogWarningEx(LOG_DEVICE_BIO, "Failed to stop channel: %s", BL_GetErrorString(result));
    }
    
    // Small delay after stop
    Delay(0.2);
    
    // Load technique
    ctx->state = BIO_TECH_STATE_LOADING;
    result = BL_LoadTechnique(ID, channel, ctx->config.eccFile, 
                            ctx->config.originalParams, true, true, false);
    
    if (result != SUCCESS) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to load OCV technique: %s", BL_GetErrorString(result));
        goto error;
    }
    
    // Start channel
    result = BL_StartChannel(ID, channel);
    if (result != SUCCESS) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to start channel: %s", BL_GetErrorString(result));
        goto error;
    }
    
    *context = ctx;
    return SUCCESS;
    
error:
    ctx->lastError = result;
    ctx->state = BIO_TECH_STATE_ERROR;
    BL_FreeTechniqueContext(ctx);
    return result;
}

// Start PEIS measurement
int BL_StartPEIS(int ID, uint8_t channel,
                 bool vs_initial,               // Voltage step vs initial
                 double initial_voltage_step,   // Initial voltage step (V)
                 double duration_step,          // Step duration (s)
                 double record_every_dT,        // Record every dt (s)
                 double record_every_dI,        // Record every dI (A)
                 double initial_freq,           // Initial frequency (Hz)
                 double final_freq,             // Final frequency (Hz)
                 bool sweep_linear,             // TRUE for linear, FALSE for logarithmic
                 double amplitude_voltage,      // Sine amplitude (V)
                 int frequency_number,          // Number of frequencies
                 int average_n_times,           // Number of repeat times
                 bool correction,               // Non-stationary correction
                 double wait_for_steady,        // Number of periods to wait
				 bool processData,
                 BL_TechniqueContext **context) {
    
    if (!context) return BL_ERR_INVALIDPARAMETERS;
    
    int result;
    
    // Create context
    BL_TechniqueContext *ctx = BL_CreateTechniqueContext(ID, channel, BIO_TECHNIQUE_PEIS);
    if (!ctx) return BL_ERR_FUNCTIONFAILED;
    
    // Store key parameters
    ctx->config.key.freqStart = initial_freq;
    ctx->config.key.freqEnd = final_freq;
	ctx->processData = processData;
    
    // Build PEIS parameters according to documentation
    TEccParam_t params[13];
    ctx->config.originalParams.len = 13;
    ctx->config.originalParams.pParams = params;
    
    int idx = 0;
    
    // vs_initial
    result = BL_DefineBoolParameter("vs_initial", vs_initial, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Initial_Voltage_step
    result = BL_DefineSglParameter("Initial_Voltage_step", (float)initial_voltage_step, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Duration_step
    result = BL_DefineSglParameter("Duration_step", (float)duration_step, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Record_every_dT
    result = BL_DefineSglParameter("Record_every_dT", (float)record_every_dT, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Record_every_dI
    result = BL_DefineSglParameter("Record_every_dI", (float)record_every_dI, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Final_frequency
    result = BL_DefineSglParameter("Final_frequency", (float)final_freq, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Initial_frequency
    result = BL_DefineSglParameter("Initial_frequency", (float)initial_freq, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // sweep
    result = BL_DefineBoolParameter("sweep", sweep_linear, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Amplitude_Voltage
    result = BL_DefineSglParameter("Amplitude_Voltage", (float)amplitude_voltage, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Frequency_number
    result = BL_DefineIntParameter("Frequency_number", frequency_number, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Average_N_times
    result = BL_DefineIntParameter("Average_N_times", average_n_times, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Correction
    result = BL_DefineBoolParameter("Correction", correction, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Wait_for_steady
    result = BL_DefineSglParameter("Wait_for_steady", (float)wait_for_steady, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Make a copy of parameters
    ctx->config.paramsCopy = malloc(idx * sizeof(TEccParam_t));
    if (ctx->config.paramsCopy) {
        memcpy(ctx->config.paramsCopy, params, idx * sizeof(TEccParam_t));
        ctx->config.originalParams.pParams = ctx->config.paramsCopy;
    }
    
    // Store technique file
    strcpy(ctx->config.eccFile, "lib\\peis.ecc");
    
    // Stop channel if running
    result = BL_StopChannel(ID, channel);
    if (result != SUCCESS && result != BL_ERR_CHANNELNOTPLUGGED) {
        LogWarningEx(LOG_DEVICE_BIO, "Failed to stop channel: %s", BL_GetErrorString(result));
    }
    
    // Small delay after stop
    Delay(0.2);
    
    // Load technique
    ctx->state = BIO_TECH_STATE_LOADING;
    result = BL_LoadTechnique(ID, channel, ctx->config.eccFile, 
                            ctx->config.originalParams, true, true, false);
    
    if (result != SUCCESS) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to load PEIS technique: %s", BL_GetErrorString(result));
        goto error;
    }
    
    // Start channel
    result = BL_StartChannel(ID, channel);
    if (result != SUCCESS) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to start channel: %s", BL_GetErrorString(result));
        goto error;
    }
    
    *context = ctx;
    return SUCCESS;
    
error:
    ctx->lastError = result;
    ctx->state = BIO_TECH_STATE_ERROR;
    BL_FreeTechniqueContext(ctx);
    return result;
}

// Start SPEIS measurement
int BL_StartSPEIS(int ID, uint8_t channel,
                  bool vs_initial,
                  bool vs_final,
                  double initial_voltage_step,
                  double final_voltage_step,
                  double duration_step,
                  int step_number,
                  double record_every_dT,
                  double record_every_dI,
                  double initial_freq,
                  double final_freq,
                  bool sweep_linear,
                  double amplitude_voltage,
                  int frequency_number,
                  int average_n_times,
                  bool correction,
                  double wait_for_steady,
				  bool processData,
                  BL_TechniqueContext **context) {
    
    if (!context) return BL_ERR_INVALIDPARAMETERS;
    
    int result;
    
    // Create context
    BL_TechniqueContext *ctx = BL_CreateTechniqueContext(ID, channel, BIO_TECHNIQUE_SPEIS);
    if (!ctx) return BL_ERR_FUNCTIONFAILED;
    
    // Store key parameters
    ctx->config.key.freqStart = initial_freq;
    ctx->config.key.freqEnd = final_freq;
	ctx->processData = processData;
    
    // Build SPEIS parameters according to documentation
    TEccParam_t params[16];
    ctx->config.originalParams.len = 16;
    ctx->config.originalParams.pParams = params;
    
    int idx = 0;
    
    // vs_initial
    result = BL_DefineBoolParameter("vs_initial", vs_initial, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // vs_final
    result = BL_DefineBoolParameter("vs_final", vs_final, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Initial_Voltage_step
    result = BL_DefineSglParameter("Initial_Voltage_step", (float)initial_voltage_step, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Final_Voltage_step
    result = BL_DefineSglParameter("Final_Voltage_step", (float)final_voltage_step, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Duration_step
    result = BL_DefineSglParameter("Duration_step", (float)duration_step, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Step_number
    result = BL_DefineIntParameter("Step_number", step_number, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Record_every_dT
    result = BL_DefineSglParameter("Record_every_dT", (float)record_every_dT, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Record_every_dI
    result = BL_DefineSglParameter("Record_every_dI", (float)record_every_dI, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Final_frequency
    result = BL_DefineSglParameter("Final_frequency", (float)final_freq, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Initial_frequency
    result = BL_DefineSglParameter("Initial_frequency", (float)initial_freq, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // sweep
    result = BL_DefineBoolParameter("sweep", sweep_linear, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Amplitude_Voltage
    result = BL_DefineSglParameter("Amplitude_Voltage", (float)amplitude_voltage, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Frequency_number
    result = BL_DefineIntParameter("Frequency_number", frequency_number, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Average_N_times
    result = BL_DefineIntParameter("Average_N_times", average_n_times, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Correction
    result = BL_DefineBoolParameter("Correction", correction, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Wait_for_steady
    result = BL_DefineSglParameter("Wait_for_steady", (float)wait_for_steady, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Make a copy of parameters
    ctx->config.paramsCopy = malloc(idx * sizeof(TEccParam_t));
    if (ctx->config.paramsCopy) {
        memcpy(ctx->config.paramsCopy, params, idx * sizeof(TEccParam_t));
        ctx->config.originalParams.pParams = ctx->config.paramsCopy;
    }
    
    // Store technique file
    strcpy(ctx->config.eccFile, "lib\\seisp.ecc");
    
    // Stop channel if running
    result = BL_StopChannel(ID, channel);
    if (result != SUCCESS && result != BL_ERR_CHANNELNOTPLUGGED) {
        LogWarningEx(LOG_DEVICE_BIO, "Failed to stop channel: %s", BL_GetErrorString(result));
    }
    
    // Small delay after stop
    Delay(0.2);
    
    // Load technique
    ctx->state = BIO_TECH_STATE_LOADING;
    result = BL_LoadTechnique(ID, channel, ctx->config.eccFile, 
                            ctx->config.originalParams, true, true, false);
    
    if (result != SUCCESS) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to load SPEIS technique: %s", BL_GetErrorString(result));
        goto error;
    }
    
    // Start channel
    result = BL_StartChannel(ID, channel);
    if (result != SUCCESS) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to start channel: %s", BL_GetErrorString(result));
        goto error;
    }
    
    *context = ctx;
    return SUCCESS;
    
error:
    ctx->lastError = result;
    ctx->state = BIO_TECH_STATE_ERROR;
    BL_FreeTechniqueContext(ctx);
    return result;
}

// Start GEIS measurement
int BL_StartGEIS(int ID, uint8_t channel,
                 bool vs_initial,
                 double initial_current_step,
                 double duration_step,
                 double record_every_dT,
                 double record_every_dE,
                 double initial_freq,
                 double final_freq,
                 bool sweep_linear,
                 double amplitude_current,
                 int frequency_number,
                 int average_n_times,
                 bool correction,
                 double wait_for_steady,
                 int i_range,
				 bool processData,
                 BL_TechniqueContext **context) {
    
    if (!context) return BL_ERR_INVALIDPARAMETERS;
    
    // Validate i_range (cannot be auto)
    if (i_range == KBIO_IRANGE_AUTO) {
        LogErrorEx(LOG_DEVICE_BIO, "GEIS: Auto range not allowed for current range");
        return BL_ERR_INVALIDPARAMETERS;
    }
    
    int result;
    
    // Create context
    BL_TechniqueContext *ctx = BL_CreateTechniqueContext(ID, channel, BIO_TECHNIQUE_GEIS);
    if (!ctx) return BL_ERR_FUNCTIONFAILED;
    
    // Store key parameters
    ctx->config.key.freqStart = initial_freq;
    ctx->config.key.freqEnd = final_freq;
	ctx->processData = processData;
    
    // Build GEIS parameters according to documentation
    TEccParam_t params[14];
    ctx->config.originalParams.len = 14;
    ctx->config.originalParams.pParams = params;
    
    int idx = 0;
    
    // vs_initial
    result = BL_DefineBoolParameter("vs_initial", vs_initial, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Initial_Current_step
    result = BL_DefineSglParameter("Initial_Current_step", (float)initial_current_step, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Duration_step
    result = BL_DefineSglParameter("Duration_step", (float)duration_step, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Record_every_dT
    result = BL_DefineSglParameter("Record_every_dT", (float)record_every_dT, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Record_every_dE
    result = BL_DefineSglParameter("Record_every_dE", (float)record_every_dE, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Final_frequency
    result = BL_DefineSglParameter("Final_frequency", (float)final_freq, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Initial_frequency
    result = BL_DefineSglParameter("Initial_frequency", (float)initial_freq, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // sweep
    result = BL_DefineBoolParameter("sweep", sweep_linear, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Amplitude_Current
    result = BL_DefineSglParameter("Amplitude_Current", (float)amplitude_current, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Frequency_number
    result = BL_DefineIntParameter("Frequency_number", frequency_number, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Average_N_times
    result = BL_DefineIntParameter("Average_N_times", average_n_times, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Correction
    result = BL_DefineBoolParameter("Correction", correction, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Wait_for_steady
    result = BL_DefineSglParameter("Wait_for_steady", (float)wait_for_steady, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // I_Range
    result = BL_DefineIntParameter("I_Range", i_range, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Make a copy of parameters
    ctx->config.paramsCopy = malloc(idx * sizeof(TEccParam_t));
    if (ctx->config.paramsCopy) {
        memcpy(ctx->config.paramsCopy, params, idx * sizeof(TEccParam_t));
        ctx->config.originalParams.pParams = ctx->config.paramsCopy;
    }
    
    // Store technique file
    strcpy(ctx->config.eccFile, "lib\\geis.ecc");
    
    // Stop channel if running
    result = BL_StopChannel(ID, channel);
    if (result != SUCCESS && result != BL_ERR_CHANNELNOTPLUGGED) {
        LogWarningEx(LOG_DEVICE_BIO, "Failed to stop channel: %s", BL_GetErrorString(result));
    }
    
    // Small delay after stop
    Delay(0.2);
    
    // Load technique
    ctx->state = BIO_TECH_STATE_LOADING;
    result = BL_LoadTechnique(ID, channel, ctx->config.eccFile, 
                            ctx->config.originalParams, true, true, false);
    
    if (result != SUCCESS) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to load GEIS technique: %s", BL_GetErrorString(result));
        goto error;
    }
    
    // Start channel
    result = BL_StartChannel(ID, channel);
    if (result != SUCCESS) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to start channel: %s", BL_GetErrorString(result));
        goto error;
    }
    
    *context = ctx;
    return SUCCESS;
    
error:
    ctx->lastError = result;
    ctx->state = BIO_TECH_STATE_ERROR;
    BL_FreeTechniqueContext(ctx);
    return result;
}

// Start SGEIS measurement
int BL_StartSGEIS(int ID, uint8_t channel,
                  bool vs_initial,
                  bool vs_final,
                  double initial_current_step,
                  double final_current_step,
                  double duration_step,
                  int step_number,
                  double record_every_dT,
                  double record_every_dE,
                  double initial_freq,
                  double final_freq,
                  bool sweep_linear,
                  double amplitude_current,
                  int frequency_number,
                  int average_n_times,
                  bool correction,
                  double wait_for_steady,
                  int i_range,
				  bool processData,
                  BL_TechniqueContext **context) {
    
    if (!context) return BL_ERR_INVALIDPARAMETERS;
    
    // Validate i_range (cannot be auto)
    if (i_range == KBIO_IRANGE_AUTO) {
        LogErrorEx(LOG_DEVICE_BIO, "SGEIS: Auto range not allowed for current range");
        return BL_ERR_INVALIDPARAMETERS;
    }
    
    int result;
    
    // Create context
    BL_TechniqueContext *ctx = BL_CreateTechniqueContext(ID, channel, BIO_TECHNIQUE_SGEIS);
    if (!ctx) return BL_ERR_FUNCTIONFAILED;
    
    // Store key parameters
    ctx->config.key.freqStart = initial_freq;
    ctx->config.key.freqEnd = final_freq;
	ctx->processData = processData;
    
    // Build SGEIS parameters according to documentation
    TEccParam_t params[17];
    ctx->config.originalParams.len = 17;
    ctx->config.originalParams.pParams = params;
    
    int idx = 0;
    
    // vs_initial
    result = BL_DefineBoolParameter("vs_initial", vs_initial, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // vs_final
    result = BL_DefineBoolParameter("vs_final", vs_final, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Initial_Current_step
    result = BL_DefineSglParameter("Initial_Current_step", (float)initial_current_step, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Final_Current_step
    result = BL_DefineSglParameter("Final_Current_step", (float)final_current_step, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Duration_step
    result = BL_DefineSglParameter("Duration_step", (float)duration_step, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Step_number
    result = BL_DefineIntParameter("Step_number", step_number, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Record_every_dT
    result = BL_DefineSglParameter("Record_every_dT", (float)record_every_dT, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Record_every_dE
    result = BL_DefineSglParameter("Record_every_dE", (float)record_every_dE, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Final_frequency
    result = BL_DefineSglParameter("Final_frequency", (float)final_freq, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Initial_frequency
    result = BL_DefineSglParameter("Initial_frequency", (float)initial_freq, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // sweep
    result = BL_DefineBoolParameter("sweep", sweep_linear, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Amplitude_Current
    result = BL_DefineSglParameter("Amplitude_Current", (float)amplitude_current, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Frequency_number
    result = BL_DefineIntParameter("Frequency_number", frequency_number, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Average_N_times
    result = BL_DefineIntParameter("Average_N_times", average_n_times, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Correction
    result = BL_DefineBoolParameter("Correction", correction, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Wait_for_steady
    result = BL_DefineSglParameter("Wait_for_steady", (float)wait_for_steady, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // I_Range
    result = BL_DefineIntParameter("I_Range", i_range, 0, &params[idx++]);
    if (result != SUCCESS) goto error;
    
    // Make a copy of parameters
    ctx->config.paramsCopy = malloc(idx * sizeof(TEccParam_t));
    if (ctx->config.paramsCopy) {
        memcpy(ctx->config.paramsCopy, params, idx * sizeof(TEccParam_t));
        ctx->config.originalParams.pParams = ctx->config.paramsCopy;
    }
    
    // Store technique file
    strcpy(ctx->config.eccFile, "lib\\seisg.ecc");
    
    // Stop channel if running
    result = BL_StopChannel(ID, channel);
    if (result != SUCCESS && result != BL_ERR_CHANNELNOTPLUGGED) {
        LogWarningEx(LOG_DEVICE_BIO, "Failed to stop channel: %s", BL_GetErrorString(result));
    }
    
    // Small delay after stop
    Delay(0.2);
    
    // Load technique
    ctx->state = BIO_TECH_STATE_LOADING;
    result = BL_LoadTechnique(ID, channel, ctx->config.eccFile, 
                            ctx->config.originalParams, true, true, false);
    
    if (result != SUCCESS) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to load SGEIS technique: %s", BL_GetErrorString(result));
        goto error;
    }
    
    // Start channel
    result = BL_StartChannel(ID, channel);
    if (result != SUCCESS) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to start channel: %s", BL_GetErrorString(result));
        goto error;
    }
    
    *context = ctx;
    return SUCCESS;
    
error:
    ctx->lastError = result;
    ctx->state = BIO_TECH_STATE_ERROR;
    BL_FreeTechniqueContext(ctx);
    return result;
}