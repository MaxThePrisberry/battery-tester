#include <stdio.h>
#include <string.h>
#include "biologic.h"
#include <ansi_c.h>

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
        printf("Warning: Could not load function %s\n", funcName);
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

static char* my_strdup(const char* s) {
    size_t len;
    char* copy;
    
    if (s == NULL) return NULL;
    
    len = strlen(s) + 1;
    copy = (char*)malloc(len);
    if (copy != NULL) {
        memcpy(copy, s, len);
    }
    return copy;
}

// ============================================================================
// EClib.dll Initialization and Management
// ============================================================================

// Initialize the EClib DLL
int InitializeBioLogic(void) {
    char dllPath[MAX_PATHNAME_LEN];
    
    // If already initialized, return success
    if (g_hEClibDLL != NULL) {
        return 0;
    }
    
    // Try to load from current directory first
    GetCurrentDirectory(MAX_PATHNAME_LEN, dllPath);
    strcat(dllPath, "\\EClib.dll");
    
    g_hEClibDLL = LoadLibrary(dllPath);
    
    // If that fails, try just the DLL name (will search PATH)
    if (g_hEClibDLL == NULL) {
        g_hEClibDLL = LoadLibrary("EClib.dll");
    }
    
    if (g_hEClibDLL == NULL) {
        DWORD error = GetLastError();
        printf("Failed to load EClib.dll. Error code: %d\n", error);
        printf("Make sure EClib.dll is in the executable directory or in PATH\n");
        return -1;
    }
    
    printf("EClib.dll loaded successfully\n");
    
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
        printf("Failed to load critical functions from EClib.dll\n");
        CleanupBioLogic();
        return -1;
    }
    
    // Get and display library version if available
    if (g_BL_GetLibVersion != NULL) {
        char version[256];
        unsigned int size = sizeof(version);
        if (g_BL_GetLibVersion(version, &size) == 0) {
            printf("EClib version: %s\n", version);
        }
    }
    
    return 0;
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
    char dllPath[MAX_PATHNAME_LEN];
    
    if (g_hBLFindDLL != NULL) {
        return 0; // Already initialized
    }
    
    // Try to load from current directory first
    GetCurrentDirectory(MAX_PATHNAME_LEN, dllPath);
    strcat(dllPath, "\\blfind.dll");
    
    g_hBLFindDLL = LoadLibrary(dllPath);
    
    // If that fails, try just the DLL name (will search PATH)
    if (g_hBLFindDLL == NULL) {
        g_hBLFindDLL = LoadLibrary("blfind.dll");
    }
    
    if (g_hBLFindDLL == NULL) {
        printf("Failed to load blfind.dll. Error: %d\n", GetLastError());
        return -1;
    }
    
    printf("blfind.dll loaded successfully\n");
    
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
        printf("Failed to load any scanning functions from blfind.dll\n");
        CleanupBLFind();
        return -1;
    }
    
    return 0;
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
const char* GetErrorString(int errorCode) {
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
        
        default: return "Unknown error";
    }
}

// ============================================================================
// Wrapper Functions for EClib.dll
// ============================================================================

// Connection functions
int BL_Connect(const char* address, uint8_t timeout, int* pID, TDeviceInfos_t* pInfos) {
    if (!IsBioLogicInitialized() || g_BL_Connect == NULL) return -10;
    return g_BL_Connect(address, timeout, pID, pInfos);
}

int BL_Disconnect(int ID) {
    if (!IsBioLogicInitialized() || g_BL_Disconnect == NULL) return -10;
    return g_BL_Disconnect(ID);
}

int BL_TestConnection(int ID) {
    if (!IsBioLogicInitialized() || g_BL_TestConnection == NULL) return -10;
    return g_BL_TestConnection(ID);
}

int BL_TestCommSpeed(int ID, uint8_t channel, int* spd_rcvt, int* spd_kernel) {
    if (!IsBioLogicInitialized() || g_BL_TestCommSpeed == NULL) return -10;
    return g_BL_TestCommSpeed(ID, channel, spd_rcvt, spd_kernel);
}

// General functions
int BL_GetLibVersion(char* pVersion, unsigned int* psize) {
    if (!IsBioLogicInitialized() || g_BL_GetLibVersion == NULL) return -10;
    return g_BL_GetLibVersion(pVersion, psize);
}

unsigned int BL_GetVolumeSerialNumber(void) {
    if (!IsBioLogicInitialized() || g_BL_GetVolumeSerialNumber == NULL) return 0;
    return g_BL_GetVolumeSerialNumber();
}

int BL_GetErrorMsg(int errorcode, char* pmsg, unsigned int* psize) {
    if (!IsBioLogicInitialized() || g_BL_GetErrorMsg == NULL) return -10;
    return g_BL_GetErrorMsg(errorcode, pmsg, psize);
}

int BL_GetUSBdeviceinfos(unsigned int USBindex, char* pcompany, unsigned int* pcompanysize, 
                         char* pdevice, unsigned int* pdevicesize, char* pSN, unsigned int* pSNsize) {
    if (!IsBioLogicInitialized() || g_BL_GetUSBdeviceinfos == NULL) return 0;
    return g_BL_GetUSBdeviceinfos(USBindex, pcompany, pcompanysize, pdevice, pdevicesize, pSN, pSNsize) ? 0 : -1;
}

// Firmware functions
int BL_LoadFirmware(int ID, uint8_t* pChannels, int* pResults, uint8_t Length, 
                    bool ShowGauge, bool ForceReload, const char* BinFile, const char* XlxFile) {
    if (!IsBioLogicInitialized() || g_BL_LoadFirmware == NULL) return -10;
    return g_BL_LoadFirmware(ID, pChannels, pResults, Length, ShowGauge, ForceReload, BinFile, XlxFile);
}

int BL_LoadFlash(int ID, const char* pfname, bool ShowGauge) {
    if (!IsBioLogicInitialized() || g_BL_LoadFlash == NULL) return -10;
    return g_BL_LoadFlash(ID, pfname, ShowGauge);
}

// Channel information functions
bool BL_IsChannelPlugged(int ID, uint8_t ch) {
    if (!IsBioLogicInitialized() || g_BL_IsChannelPlugged == NULL) return false;
    return g_BL_IsChannelPlugged(ID, ch);
}

int BL_GetChannelsPlugged(int ID, uint8_t* pChPlugged, uint8_t Size) {
    if (!IsBioLogicInitialized() || g_BL_GetChannelsPlugged == NULL) return -10;
    return g_BL_GetChannelsPlugged(ID, pChPlugged, Size);
}

int BL_GetChannelInfos(int ID, uint8_t ch, TChannelInfos_t* pInfos) {
    if (!IsBioLogicInitialized() || g_BL_GetChannelInfos == NULL) return -10;
    return g_BL_GetChannelInfos(ID, ch, pInfos);
}

int BL_GetMessage(int ID, uint8_t ch, char* msg, unsigned int* size) {
    if (!IsBioLogicInitialized() || g_BL_GetMessage == NULL) return -10;
    return g_BL_GetMessage(ID, ch, msg, size);
}

int BL_GetHardConf(int ID, uint8_t ch, THardwareConf_t* pHardConf) {
    if (!IsBioLogicInitialized() || g_BL_GetHardConf == NULL) return -10;
    return g_BL_GetHardConf(ID, ch, pHardConf);
}

int BL_SetHardConf(int ID, uint8_t ch, THardwareConf_t HardConf) {
    if (!IsBioLogicInitialized() || g_BL_SetHardConf == NULL) return -10;
    return g_BL_SetHardConf(ID, ch, HardConf);
}

int BL_GetChannelBoardType(int ID, uint8_t Channel, uint32_t* pChannelType) {
    if (!IsBioLogicInitialized() || g_BL_GetChannelBoardType == NULL) return -10;
    return g_BL_GetChannelBoardType(ID, Channel, pChannelType);
}

// Module functions
bool BL_IsModulePlugged(int ID, uint8_t module) {
    if (!IsBioLogicInitialized() || g_BL_IsModulePlugged == NULL) return false;
    return g_BL_IsModulePlugged(ID, module);
}

int BL_GetModulesPlugged(int ID, uint8_t* pModPlugged, uint8_t Size) {
    if (!IsBioLogicInitialized() || g_BL_GetModulesPlugged == NULL) return -10;
    return g_BL_GetModulesPlugged(ID, pModPlugged, Size);
}

int BL_GetModuleInfos(int ID, uint8_t module, void* pInfos) {
    if (!IsBioLogicInitialized() || g_BL_GetModuleInfos == NULL) return -10;
    return g_BL_GetModuleInfos(ID, module, pInfos);
}

// Technique functions
int BL_LoadTechnique(int ID, uint8_t channel, const char* pFName, TEccParams_t Params, 
                     bool FirstTechnique, bool LastTechnique, bool DisplayParams) {
    if (!IsBioLogicInitialized() || g_BL_LoadTechnique == NULL) return -10;
    return g_BL_LoadTechnique(ID, channel, pFName, Params, FirstTechnique, LastTechnique, DisplayParams);
}

int BL_DefineBoolParameter(const char* lbl, bool value, int index, TEccParam_t* pParam) {
    if (!IsBioLogicInitialized() || g_BL_DefineBoolParameter == NULL) return -10;
    return g_BL_DefineBoolParameter(lbl, value, index, pParam);
}

int BL_DefineSglParameter(const char* lbl, float value, int index, TEccParam_t* pParam) {
    if (!IsBioLogicInitialized() || g_BL_DefineSglParameter == NULL) return -10;
    return g_BL_DefineSglParameter(lbl, value, index, pParam);
}

int BL_DefineIntParameter(const char* lbl, int value, int index, TEccParam_t* pParam) {
    if (!IsBioLogicInitialized() || g_BL_DefineIntParameter == NULL) return -10;
    return g_BL_DefineIntParameter(lbl, value, index, pParam);
}

int BL_UpdateParameters(int ID, uint8_t channel, int TechIndx, TEccParams_t Params, const char* EccFileName) {
    if (!IsBioLogicInitialized() || g_BL_UpdateParameters == NULL) return -10;
    return g_BL_UpdateParameters(ID, channel, TechIndx, Params, EccFileName);
}

int BL_GetTechniqueInfos(int ID, uint8_t channel, int TechIndx, void* pInfos) {
    if (!IsBioLogicInitialized() || g_BL_GetTechniqueInfos == NULL) return -10;
    return g_BL_GetTechniqueInfos(ID, channel, TechIndx, pInfos);
}

int BL_GetParamInfos(int ID, uint8_t channel, int TechIndx, int ParamIndx, void* pInfos) {
    if (!IsBioLogicInitialized() || g_BL_GetParamInfos == NULL) return -10;
    return g_BL_GetParamInfos(ID, channel, TechIndx, ParamIndx, pInfos);
}

// Start/Stop functions
int BL_StartChannel(int ID, uint8_t channel) {
    if (!IsBioLogicInitialized() || g_BL_StartChannel == NULL) return -10;
    return g_BL_StartChannel(ID, channel);
}

int BL_StartChannels(int ID, uint8_t* pChannels, int* pResults, uint8_t length) {
    if (!IsBioLogicInitialized() || g_BL_StartChannels == NULL) return -10;
    return g_BL_StartChannels(ID, pChannels, pResults, length);
}

int BL_StopChannel(int ID, uint8_t channel) {
    if (!IsBioLogicInitialized() || g_BL_StopChannel == NULL) return -10;
    return g_BL_StopChannel(ID, channel);
}

int BL_StopChannels(int ID, uint8_t* pChannels, int* pResults, uint8_t length) {
    if (!IsBioLogicInitialized() || g_BL_StopChannels == NULL) return -10;
    return g_BL_StopChannels(ID, pChannels, pResults, length);
}

// Data functions
int BL_GetCurrentValues(int ID, uint8_t channel, TCurrentValues_t* pValues) {
    if (!IsBioLogicInitialized() || g_BL_GetCurrentValues == NULL) return -10;
    return g_BL_GetCurrentValues(ID, channel, pValues);
}

int BL_GetData(int ID, uint8_t channel, TDataBuffer_t* pBuf, TDataInfos_t* pInfos, TCurrentValues_t* pValues) {
    if (!IsBioLogicInitialized() || g_BL_GetData == NULL) return -10;
    return g_BL_GetData(ID, channel, pBuf, pInfos, pValues);
}

int BL_GetFCTData(int ID, uint8_t channel, TDataBuffer_t* pBuf, TDataInfos_t* pInfos, TCurrentValues_t* pValues) {
    if (!IsBioLogicInitialized() || g_BL_GetFCTData == NULL) return -10;
    return g_BL_GetFCTData(ID, channel, pBuf, pInfos, pValues);
}

int BL_ConvertNumericIntoSingle(unsigned int num, float* psgl) {
    if (!IsBioLogicInitialized() || g_BL_ConvertNumericIntoSingle == NULL) return -10;
    return g_BL_ConvertNumericIntoSingle(num, psgl);
}

int BL_ConvertChannelNumericIntoSingle(uint32_t num, float* pRetFloat, uint32_t ChannelType) {
    if (!IsBioLogicInitialized() || g_BL_ConvertChannelNumericIntoSingle == NULL) return -10;
    return g_BL_ConvertChannelNumericIntoSingle(num, pRetFloat, ChannelType);
}

int BL_ConvertTimeChannelNumericIntoSeconds(uint32_t* pnum, double* pRetTime, float Timebase, uint32_t ChannelType) {
    if (!IsBioLogicInitialized() || g_BL_ConvertTimeChannelNumericIntoSeconds == NULL) return -10;
    return g_BL_ConvertTimeChannelNumericIntoSeconds(pnum, pRetTime, Timebase, ChannelType);
}

// Additional data functions
int BL_GetCurrentValuesBk(int ID, uint8_t channel, void* pValues) {
    if (!IsBioLogicInitialized() || g_BL_GetCurrentValuesBk == NULL) return -10;
    return g_BL_GetCurrentValuesBk(ID, channel, pValues);
}

int BL_GetDataBk(int ID, uint8_t channel, void* pBuf, void* pInfos, void* pValues) {
    if (!IsBioLogicInitialized() || g_BL_GetDataBk == NULL) return -10;
    return g_BL_GetDataBk(ID, channel, pBuf, pInfos, pValues);
}

int BL_GetData_LV(int ID, uint8_t channel, void* pBuf, void* pInfos, void* pValues) {
    if (!IsBioLogicInitialized() || g_BL_GetData_LV == NULL) return -10;
    return g_BL_GetData_LV(ID, channel, pBuf, pInfos, pValues);
}

int BL_GetData_VEE(int ID, uint8_t channel, void* pBuf, void* pInfos, void* pValues) {
    if (!IsBioLogicInitialized() || g_BL_GetData_VEE == NULL) return -10;
    return g_BL_GetData_VEE(ID, channel, pBuf, pInfos, pValues);
}

// Experiment functions
int BL_SetExperimentInfos(int ID, uint8_t channel, TExperimentInfos_t TExpInfos) {
    if (!IsBioLogicInitialized() || g_BL_SetExperimentInfos == NULL) return -10;
    return g_BL_SetExperimentInfos(ID, channel, TExpInfos);
}

int BL_GetExperimentInfos(int ID, uint8_t channel, TExperimentInfos_t* TExpInfos) {
   if (!IsBioLogicInitialized() || g_BL_GetExperimentInfos == NULL) return -10;
   return g_BL_GetExperimentInfos(ID, channel, TExpInfos);
}

// Advanced functions
int BL_SendMsg(int ID, uint8_t ch, void* pBuf, unsigned int* pLen) {
   if (!IsBioLogicInitialized() || g_BL_SendMsg == NULL) return -10;
   return g_BL_SendMsg(ID, ch, pBuf, pLen);
}

int BL_SendMsgToRcvt(int ID, void* pBuf, unsigned int* pLen) {
   if (!IsBioLogicInitialized() || g_BL_SendMsgToRcvt == NULL) return -10;
   return g_BL_SendMsgToRcvt(ID, pBuf, pLen);
}

int BL_SendMsgToRcvt_g(int ID, uint8_t ch, void* pBuf, unsigned int* pLen) {
   if (!IsBioLogicInitialized() || g_BL_SendMsgToRcvt_g == NULL) return -10;
   return g_BL_SendMsgToRcvt_g(ID, ch, pBuf, pLen);
}

int BL_SendEcalMsg(int ID, uint8_t ch, void* pBuf, unsigned int* pLen) {
   if (!IsBioLogicInitialized() || g_BL_SendEcalMsg == NULL) return -10;
   return g_BL_SendEcalMsg(ID, ch, pBuf, pLen);
}

int BL_SendEcalMsgGroup(int ID, uint8_t* pChannels, uint8_t length, void* pBuf, unsigned int* pLen) {
   if (!IsBioLogicInitialized() || g_BL_SendEcalMsgGroup == NULL) return -10;
   return g_BL_SendEcalMsgGroup(ID, pChannels, length, pBuf, pLen);
}

// Additional functions
int BL_GetFPGAVer(int ID, uint8_t channel, uint32_t* pVersion) {
   if (!IsBioLogicInitialized() || g_BL_GetFPGAVer == NULL) return -10;
   return g_BL_GetFPGAVer(ID, channel, pVersion);
}

int BL_GetOptErr(int ID, uint8_t channel, int* pOptErr, int* pOptPos) {
   if (!IsBioLogicInitialized() || g_BL_GetOptErr == NULL) return -10;
   return g_BL_GetOptErr(ID, channel, pOptErr, pOptPos);
}

int BL_ReadParameters(int ID, uint8_t channel, void* pParams) {
   if (!IsBioLogicInitialized() || g_BL_ReadParameters == NULL) return -10;
   return g_BL_ReadParameters(ID, channel, pParams);
}

int BL_GetChannelFloatFormat(int ID, uint8_t channel, int* pFormat) {
   if (!IsBioLogicInitialized() || g_BL_GetChannelFloatFormat == NULL) return -10;
   return g_BL_GetChannelFloatFormat(ID, channel, pFormat);
}

int BL_ConvertNumericIntoFloat(unsigned int num, double* pdbl) {
   if (!IsBioLogicInitialized() || g_BL_ConvertNumericIntoFloat == NULL) return -10;
   return g_BL_ConvertNumericIntoFloat(num, pdbl);
}

int BL_ConvertTimeChannelNumericIntoTimebases(uint32_t* pnum, double* pRetTime, float* pTimebases, uint32_t ChannelType) {
   if (!IsBioLogicInitialized() || g_BL_ConvertTimeChannelNumericIntoTimebases == NULL) return -10;
   return g_BL_ConvertTimeChannelNumericIntoTimebases(pnum, pRetTime, pTimebases, ChannelType);
}

// Technique loading variants
int BL_LoadTechnique_LV(int ID, uint8_t channel, const char* pFName, void* Params, 
                       bool FirstTechnique, bool LastTechnique, bool DisplayParams) {
   if (!IsBioLogicInitialized() || g_BL_LoadTechnique_LV == NULL) return -10;
   return g_BL_LoadTechnique_LV(ID, channel, pFName, Params, FirstTechnique, LastTechnique, DisplayParams);
}

int BL_LoadTechnique_VEE(int ID, uint8_t channel, const char* pFName, void* Params, 
                        bool FirstTechnique, bool LastTechnique, bool DisplayParams) {
   if (!IsBioLogicInitialized() || g_BL_LoadTechnique_VEE == NULL) return -10;
   return g_BL_LoadTechnique_VEE(ID, channel, pFName, Params, FirstTechnique, LastTechnique, DisplayParams);
}

int BL_UpdateParameters_LV(int ID, uint8_t channel, int TechIndx, void* Params, const char* EccFileName) {
   if (!IsBioLogicInitialized() || g_BL_UpdateParameters_LV == NULL) return -10;
   return g_BL_UpdateParameters_LV(ID, channel, TechIndx, Params, EccFileName);
}

int BL_UpdateParameters_VEE(int ID, uint8_t channel, int TechIndx, void* Params, const char* EccFileName) {
   if (!IsBioLogicInitialized() || g_BL_UpdateParameters_VEE == NULL) return -10;
   return g_BL_UpdateParameters_VEE(ID, channel, TechIndx, Params, EccFileName);
}

// ============================================================================
// Wrapper Functions for blfind.dll
// ============================================================================

int BL_FindEChemDev(char* pLstDev, unsigned int* pSize, unsigned int* pNbrDevice) {
   if (!IsBLFindInitialized() || g_BL_FindEChemDev == NULL) return -10;
   return g_BL_FindEChemDev(pLstDev, pSize, pNbrDevice);
}

int BL_FindEChemEthDev(char* pLstDev, unsigned int* pSize, unsigned int* pNbrDevice) {
   if (!IsBLFindInitialized() || g_BL_FindEChemEthDev == NULL) return -10;
   return g_BL_FindEChemEthDev(pLstDev, pSize, pNbrDevice);
}

int BL_FindEChemUsbDev(char* pLstDev, unsigned int* pSize, unsigned int* pNbrDevice) {
   if (!IsBLFindInitialized() || g_BL_FindEChemUsbDev == NULL) return -10;
   return g_BL_FindEChemUsbDev(pLstDev, pSize, pNbrDevice);
}

int BL_SetConfig(char* pIp, char* pCfg) {
   if (!IsBLFindInitialized() || g_BL_SetConfig == NULL) return -10;
   return g_BL_SetConfig(pIp, pCfg);
}

// Additional blfind functions
int BL_FindEChemBCSDev(char* pLstDev, unsigned int* pSize, unsigned int* pNbrDevice) {
   if (!IsBLFindInitialized() || g_BL_FindEChemBCSDev == NULL) return -10;
   return g_BL_FindEChemBCSDev(pLstDev, pSize, pNbrDevice);
}

int BL_EChemBCSEthDEV(void* param1, void* param2) {
   if (!IsBLFindInitialized() || g_BL_EChemBCSEthDEV == NULL) return -10;
   return g_BL_EChemBCSEthDEV(param1, param2);
}

int BL_FindKineticDev(char* pLstDev, unsigned int* pSize, unsigned int* pNbrDevice) {
   if (!IsBLFindInitialized() || g_BL_FindKineticDev == NULL) return -10;
   return g_BL_FindKineticDev(pLstDev, pSize, pNbrDevice);
}

int BL_FindKineticEthDev(char* pLstDev, unsigned int* pSize, unsigned int* pNbrDevice) {
   if (!IsBLFindInitialized() || g_BL_FindKineticEthDev == NULL) return -10;
   return g_BL_FindKineticEthDev(pLstDev, pSize, pNbrDevice);
}

int BL_FindKineticUsbDev(char* pLstDev, unsigned int* pSize, unsigned int* pNbrDevice) {
   if (!IsBLFindInitialized() || g_BL_FindKineticUsbDev == NULL) return -10;
   return g_BL_FindKineticUsbDev(pLstDev, pSize, pNbrDevice);
}

int BL_Init_Path(const char* path) {
   if (!IsBLFindInitialized() || g_BL_Init_Path == NULL) return -10;
   return g_BL_Init_Path(path);
}

int BL_SetMAC(char* mac) {
   if (!IsBLFindInitialized() || g_BL_SetMAC == NULL) return -10;
   return g_BL_SetMAC(mac);
}

// BLFind error message function
int BLFind_GetErrorMsg(int errorcode, char* pmsg, unsigned int* psize) {
   if (!IsBLFindInitialized() || g_BLFind_GetErrorMsg == NULL) return -10;
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
   
   printf("\n=== Scanning for BioLogic Devices ===\n\n");
   
   // Initialize blfind.dll
   if (InitializeBLFind() != 0) {
       printf("Failed to initialize blfind.dll\n");
       return -1;
   }
   
   // Initialize EClib.dll too if needed
   if (!IsBioLogicInitialized()) {
       if (InitializeBioLogic() != 0) {
           printf("Failed to initialize EClib.dll\n");
           CleanupBLFind();
           return -1;
       }
   }
   
   // Scan for USB devices
   if (g_BL_FindEChemUsbDev != NULL) {
       printf("Scanning for USB devices...\n");
       memset(deviceList, 0, sizeof(deviceList));
       bufferSize = sizeof(deviceList);
       deviceCount = 0;
       
       result = BL_FindEChemUsbDev(deviceList, &bufferSize, &deviceCount);
       
       if (result == 0) {
           printf("Found %d USB device(s)\n", deviceCount);
           if (deviceCount > 0) {
               // Convert from Unicode to ASCII
               ConvertUnicodeToAscii(deviceList, asciiDeviceList, bufferSize);
               printf("Device string: %s\n", asciiDeviceList);
               
               // Parse the device string
               // Format appears to be: USB$0$$$$$SP-150e$[serial]$...
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
                   printf("  Field %d: %s\n", fieldCount, token);
                   token = strtok(NULL, "$");
                   fieldCount++;
               }
               
               free(deviceCopy);
               
               printf("\nParsed information:\n");
               printf("  Connection: %s\n", connectionType);
               printf("  Port: %s\n", portNumber);
               printf("  Device: %s\n", deviceType);
               
               printf("\n*** Try connecting with: \"USB%s\" ***\n", portNumber);
           }
       } else {
           printf("USB scan error: %d\n", result);
           
           // Try to get error message from blfind
           if (g_BLFind_GetErrorMsg != NULL) {
               char errorMsg[256];
               unsigned int msgSize = sizeof(errorMsg);
               if (BLFind_GetErrorMsg(result, errorMsg, &msgSize) == 0) {
                   printf("BLFind error: %s\n", errorMsg);
               }
           }
       }
   }
   
   // Scan for Ethernet devices
   if (g_BL_FindEChemEthDev != NULL) {
       printf("\nScanning for Ethernet devices...\n");
       memset(deviceList, 0, sizeof(deviceList));
       bufferSize = sizeof(deviceList);
       deviceCount = 0;
       
       result = BL_FindEChemEthDev(deviceList, &bufferSize, &deviceCount);
       
       if (result == 0) {
           printf("Found %d Ethernet device(s)\n", deviceCount);
           if (deviceCount > 0) {
               ConvertUnicodeToAscii(deviceList, asciiDeviceList, bufferSize);
               printf("Device string: %s\n", asciiDeviceList);
           }
       } else {
           printf("Ethernet scan error: %d\n", result);
       }
   }
   
   // Scan for BCS devices (Battery Cycling Systems)
   if (g_BL_FindEChemBCSDev != NULL) {
       printf("\nScanning for BCS devices...\n");
       memset(deviceList, 0, sizeof(deviceList));
       bufferSize = sizeof(deviceList);
       deviceCount = 0;
       
       result = BL_FindEChemBCSDev(deviceList, &bufferSize, &deviceCount);
       
       if (result == 0) {
           printf("Found %d BCS device(s)\n", deviceCount);
           if (deviceCount > 0) {
               ConvertUnicodeToAscii(deviceList, asciiDeviceList, bufferSize);
               printf("Device string: %s\n", asciiDeviceList);
           }
       } else {
           printf("BCS scan error: %d\n", result);
       }
   }
   
   // Scan for Kinetic devices
   if (g_BL_FindKineticDev != NULL) {
       printf("\nScanning for Kinetic devices...\n");
       memset(deviceList, 0, sizeof(deviceList));
       bufferSize = sizeof(deviceList);
       deviceCount = 0;
       
       result = BL_FindKineticDev(deviceList, &bufferSize, &deviceCount);
       
       if (result == 0) {
           printf("Found %d Kinetic device(s)\n", deviceCount);
           if (deviceCount > 0) {
               ConvertUnicodeToAscii(deviceList, asciiDeviceList, bufferSize);
               printf("Device string: %s\n", asciiDeviceList);
           }
       } else {
           printf("Kinetic scan error: %d\n", result);
       }
   }
   
   printf("\n=== Scan Complete ===\n");
   
   return 0;
}
