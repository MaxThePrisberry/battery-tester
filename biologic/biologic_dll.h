#ifndef BIOLOGIC_H
#define BIOLOGIC_H

#include <windows.h>
#include <stdint.h>
#include <stdbool.h>

// Include common definitions
#include "common.h"

// Include the ECLab headers for structure definitions
#include "BLStructs.h"

// Configuration
#define TIMEOUT  5       // Connection timeout in seconds

// ============================================================================
// High-Level Technique Types and Structures
// ============================================================================

// Technique types
typedef enum {
    BIO_TECHNIQUE_NONE = 0,
    BIO_TECHNIQUE_OCV,
    BIO_TECHNIQUE_PEIS,
    BIO_TECHNIQUE_SPEIS,
    BIO_TECHNIQUE_GEIS,
    BIO_TECHNIQUE_SGEIS,
} BioTechniqueType;

// Technique state
typedef enum {
    BIO_TECH_STATE_IDLE = 0,
    BIO_TECH_STATE_LOADING,
    BIO_TECH_STATE_RUNNING,
    BIO_TECH_STATE_COMPLETED,
    BIO_TECH_STATE_ERROR,
    BIO_TECH_STATE_CANCELLED
} BioTechniqueState;

// Raw data storage
typedef struct {
    unsigned int *rawData;      // Copy of device buffer
    int bufferSize;             // Size in integers
    int numPoints;              // Number of data points (rows)
    int numVariables;           // Variables per point (columns)
    int techniqueID;            // From TDataInfos
    int processIndex;           // From TDataInfos
} BIO_RawDataBuffer;

// Converted data storage
typedef struct {
    int numPoints;
    int numVariables;
    char **variableNames;    // Array of variable names
    char **variableUnits;    // Array of units
    double **data;           // 2D array of converted values
    int techniqueID;
    int processIndex;
} BIO_ConvertedData;

// Extended raw data buffer with optional converted data
typedef struct {
    // Original raw data
    BIO_RawDataBuffer *rawData;
    
    // Converted data (if processData was true)
    BIO_ConvertedData *convertedData;
} BIO_TechniqueData;

// Technique configuration
typedef struct {
    // Original parameters for reference
    TEccParams_t originalParams;
    TEccParam_t *paramsCopy;    // Deep copy of params array
    
    // Parsed key parameters for state machine
    struct {
        double duration_s;      // For timeout (OCV, CA, CP)
        int cycles;            // For completion (CV)
        double freqStart;      // For progress (PEIS)
        double freqEnd;
        double sampleInterval_s;
        double recordEvery_dE;
        double recordEvery_dT;
        int eRange;
        // Add more as techniques are implemented
    } key;
    
    // Technique info
    BioTechniqueType type;
    char eccFile[MAX_PATH_LENGTH];
} BIO_TechniqueConfig;

// Technique context for state machine
typedef struct {
    // Device info
    int deviceID;
    uint8_t channel;
    
    // State machine
    BioTechniqueState state;
    double startTime;
    double lastUpdateTime;
    int updateCount;
    
    // Configuration
    BIO_TechniqueConfig config;
    
    // Data collection
    BIO_RawDataBuffer rawData;
    BIO_ConvertedData *convertedData;
    bool processData;
    
    TCurrentValues_t lastCurrentValues;
    int memFilledAtStart;
    
    // Error info
    int lastError;
    char errorMessage[256];
    
    // Callbacks (optional)
    void (*progressCallback)(double elapsed, int memFilled, void *userData);
    void (*dataCallback)(TDataInfos_t *info, void *userData);
    void *userData;
    
} BIO_TechniqueContext;

// ============================================================================
// Main BioLogic Library Management Functions
// ============================================================================
int InitializeBioLogic(void);
void CleanupBioLogic(void);
bool IsBioLogicInitialized(void);

// ============================================================================
// BIOFind Library Management Functions
// ============================================================================
int InitializeBIOFind(void);
void CleanupBIOFind(void);
bool IsBIOFindInitialized(void);

// ============================================================================
// Helper Functions
// ============================================================================
void ConvertUnicodeToAscii(const char* unicode, char* ascii, int unicodeLen);
const char* BIO_GetErrorString(int errorCode);

// ============================================================================
// Device Scanning Functions (from blfind.dll)
// ============================================================================
int ScanForBioLogicDevices(void);
int BIO_FindEChemDev(char* pLstDev, unsigned int* pSize, unsigned int* pNbrDevice);
int BIO_FindEChemEthDev(char* pLstDev, unsigned int* pSize, unsigned int* pNbrDevice);
int BIO_FindEChemUsbDev(char* pLstDev, unsigned int* pSize, unsigned int* pNbrDevice);
int BIO_FindEChemBCSDev(char* pLstDev, unsigned int* pSize, unsigned int* pNbrDevice);
int BIO_FindKineticDev(char* pLstDev, unsigned int* pSize, unsigned int* pNbrDevice);
int BIO_FindKineticEthDev(char* pLstDev, unsigned int* pSize, unsigned int* pNbrDevice);
int BIO_FindKineticUsbDev(char* pLstDev, unsigned int* pSize, unsigned int* pNbrDevice);
int BIO_EChemBCSEthDEV(void* param1, void* param2);
int BIO_Init_Path(const char* path);
int BIO_SetConfig(char* pIp, char* pCfg);
int BIO_SetMAC(char* mac);
int BIOFind_GetErrorMsg(int errorcode, char* pmsg, unsigned int* psize);

// ============================================================================
// Connection Functions (from EClib.dll)
// ============================================================================
int BIO_Connect(const char* address, uint8_t timeout, int* pID, TDeviceInfos_t* pInfos);
int BIO_Disconnect(int ID);
int BIO_TestConnection(int ID);
int BIO_TestCommSpeed(int ID, uint8_t channel, int* spd_rcvt, int* spd_kernel);

// ============================================================================
// General Functions (from EClib.dll)
// ============================================================================
int BIO_GetLibVersion(char* pVersion, unsigned int* psize);
unsigned int BIO_GetVolumeSerialNumber(void);
int BIO_GetErrorMsg(int errorcode, char* pmsg, unsigned int* psize);
int BIO_GetUSBdeviceinfos(unsigned int USBindex, char* pcompany, unsigned int* pcompanysize, 
                         char* pdevice, unsigned int* pdevicesize, char* pSN, unsigned int* pSNsize);

// ============================================================================
// Firmware Functions (from EClib.dll)
// ============================================================================
int BIO_LoadFirmware(int ID, uint8_t* pChannels, int* pResults, uint8_t Length, 
                    bool ShowGauge, bool ForceReload, const char* BinFile, const char* XlxFile);
int BIO_LoadFlash(int ID, const char* pfname, bool ShowGauge);

// ============================================================================
// Channel Information Functions (from EClib.dll)
// ============================================================================
bool BIO_IsChannelPlugged(int ID, uint8_t ch);
int BIO_GetChannelsPlugged(int ID, uint8_t* pChPlugged, uint8_t Size);
int BIO_GetChannelInfos(int ID, uint8_t ch, TChannelInfos_t* pInfos);
int BIO_GetMessage(int ID, uint8_t ch, char* msg, unsigned int* size);
int BIO_GetHardConf(int ID, uint8_t ch, THardwareConf_t* pHardConf);
int BIO_SetHardConf(int ID, uint8_t ch, THardwareConf_t HardConf);
int BIO_GetChannelBoardType(int ID, uint8_t Channel, uint32_t* pChannelType);
int BIO_GetChannelFloatFormat(int ID, uint8_t channel, int* pFormat);
int BIO_GetFPGAVer(int ID, uint8_t channel, uint32_t* pVersion);

// ============================================================================
// Module Functions (from EClib.dll)
// ============================================================================
bool BIO_IsModulePlugged(int ID, uint8_t module);
int BIO_GetModulesPlugged(int ID, uint8_t* pModPlugged, uint8_t Size);
int BIO_GetModuleInfos(int ID, uint8_t module, void* pInfos);

// ============================================================================
// Technique Functions (from EClib.dll)
// ============================================================================
int BIO_LoadTechnique(int ID, uint8_t channel, const char* pFName, TEccParams_t Params, 
                     bool FirstTechnique, bool LastTechnique, bool DisplayParams);
int BIO_LoadTechnique_LV(int ID, uint8_t channel, const char* pFName, void* Params, 
                        bool FirstTechnique, bool LastTechnique, bool DisplayParams);
int BIO_LoadTechnique_VEE(int ID, uint8_t channel, const char* pFName, void* Params, 
                         bool FirstTechnique, bool LastTechnique, bool DisplayParams);
int BIO_DefineBoolParameter(const char* lbl, bool value, int index, TEccParam_t* pParam);
int BIO_DefineSglParameter(const char* lbl, float value, int index, TEccParam_t* pParam);
int BIO_DefineIntParameter(const char* lbl, int value, int index, TEccParam_t* pParam);
int BIO_UpdateParameters(int ID, uint8_t channel, int TechIndx, TEccParams_t Params, const char* EccFileName);
int BIO_UpdateParameters_LV(int ID, uint8_t channel, int TechIndx, void* Params, const char* EccFileName);
int BIO_UpdateParameters_VEE(int ID, uint8_t channel, int TechIndx, void* Params, const char* EccFileName);
int BIO_GetTechniqueInfos(int ID, uint8_t channel, int TechIndx, void* pInfos);
int BIO_GetParamInfos(int ID, uint8_t channel, int TechIndx, int ParamIndx, void* pInfos);
int BIO_ReadParameters(int ID, uint8_t channel, void* pParams);

// ============================================================================
// Start/Stop Functions (from EClib.dll)
// ============================================================================
int BIO_StartChannel(int ID, uint8_t channel);
int BIO_StartChannels(int ID, uint8_t* pChannels, int* pResults, uint8_t length);
int BIO_StopChannel(int ID, uint8_t channel);
int BIO_StopChannels(int ID, uint8_t* pChannels, int* pResults, uint8_t length);

// ============================================================================
// Data Acquisition Functions (from EClib.dll)
// ============================================================================
int BIO_GetCurrentValues(int ID, uint8_t channel, TCurrentValues_t* pValues);
int BIO_GetCurrentValuesBk(int ID, uint8_t channel, void* pValues);
int BIO_GetData(int ID, uint8_t channel, TDataBuffer_t* pBuf, TDataInfos_t* pInfos, TCurrentValues_t* pValues);
int BIO_GetDataBk(int ID, uint8_t channel, void* pBuf, void* pInfos, void* pValues);
int BIO_GetData_LV(int ID, uint8_t channel, void* pBuf, void* pInfos, void* pValues);
int BIO_GetData_VEE(int ID, uint8_t channel, void* pBuf, void* pInfos, void* pValues);
int BIO_GetFCTData(int ID, uint8_t channel, TDataBuffer_t* pBuf, TDataInfos_t* pInfos, TCurrentValues_t* pValues);

// ============================================================================
// Data Conversion Functions (from EClib.dll)
// ============================================================================
int BIO_ConvertNumericIntoSingle(unsigned int num, float* psgl);
int BIO_ConvertNumericIntoFloat(unsigned int num, double* pdbl);
int BIO_ConvertChannelNumericIntoSingle(uint32_t num, float* pRetFloat, uint32_t ChannelType);
int BIO_ConvertTimeChannelNumericIntoSeconds(uint32_t* pnum, double* pRetTime, float Timebase, uint32_t ChannelType);
int BIO_ConvertTimeChannelNumericIntoTimebases(uint32_t* pnum, double* pRetTime, float* pTimebases, uint32_t ChannelType);

// ============================================================================
// Experiment Functions (from EClib.dll)
// ============================================================================
int BIO_SetExperimentInfos(int ID, uint8_t channel, TExperimentInfos_t TExpInfos);
int BIO_GetExperimentInfos(int ID, uint8_t channel, TExperimentInfos_t* TExpInfos);

// ============================================================================
// Advanced Communication Functions (from EClib.dll)
// ============================================================================
int BIO_SendMsg(int ID, uint8_t ch, void* pBuf, unsigned int* pLen);
int BIO_SendMsgToRcvt(int ID, void* pBuf, unsigned int* pLen);
int BIO_SendMsgToRcvt_g(int ID, uint8_t ch, void* pBuf, unsigned int* pLen);
int BIO_SendEcalMsg(int ID, uint8_t ch, void* pBuf, unsigned int* pLen);
int BIO_SendEcalMsgGroup(int ID, uint8_t* pChannels, uint8_t length, void* pBuf, unsigned int* pLen);
int BIO_GetOptErr(int ID, uint8_t channel, int* pOptErr, int* pOptPos);

// ============================================================================
// High-Level Technique Functions
// ============================================================================

// Context management
BIO_TechniqueContext* BIO_CreateTechniqueContext(int ID, uint8_t channel, BioTechniqueType type);
void BIO_FreeTechniqueContext(BIO_TechniqueContext *context);

// Generic technique lifecycle
int BIO_UpdateTechnique(BIO_TechniqueContext *context);
bool BIO_IsTechniqueComplete(BIO_TechniqueContext *context);
int BIO_StopTechnique(BIO_TechniqueContext *context);
int BIO_GetTechniqueRawData(BIO_TechniqueContext *context, BIO_RawDataBuffer **data);
int BIO_GetTechniqueData(BIO_TechniqueContext *context, BIO_TechniqueData **data);

// Data processing function
int BIO_ProcessTechniqueData(BIO_RawDataBuffer *rawData, int techniqueID, int processIndex,
                           uint32_t channelType, float timebase,
                           BIO_ConvertedData **convertedData);

// Helper to free converted data
void BIO_FreeConvertedData(BIO_ConvertedData *data);

// Helper to free technique data (raw + converted)
void BIO_FreeTechniqueData(BIO_TechniqueData *data);

BIO_RawDataBuffer* BIO_CopyRawDataBuffer(BIO_RawDataBuffer *src);

// OCV (Open Circuit Voltage)
int BIO_StartOCV(int ID, uint8_t channel,
                double duration_s,
                double sample_interval_s,
                double record_every_dE,     // mV
                double record_every_dT,     // seconds
                int e_range,                // 0=2.5V, 1=5V, 2=10V, 3=Auto
                bool processData,
                BIO_TechniqueContext **context);

// PEIS (Potentio Electrochemical Impedance Spectroscopy)
int BIO_StartPEIS(int ID, uint8_t channel,
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
                 BIO_TechniqueContext **context);

// SPEIS (Staircase Potentio Electrochemical Impedance Spectroscopy)
int BIO_StartSPEIS(int ID, uint8_t channel,
                  bool vs_initial,              // Voltage step vs initial
                  bool vs_final,                // Voltage step vs final
                  double initial_voltage_step,  // Initial voltage step (V)
                  double final_voltage_step,    // Final voltage step (V)
                  double duration_step,         // Step duration (s)
                  int step_number,              // Number of voltage steps [0..98]
                  double record_every_dT,       // Record every dt (s)
                  double record_every_dI,       // Record every dI (A)
                  double initial_freq,          // Initial frequency (Hz)
                  double final_freq,            // Final frequency (Hz)
                  bool sweep_linear,            // TRUE for linear, FALSE for logarithmic
                  double amplitude_voltage,     // Sine amplitude (V)
                  int frequency_number,         // Number of frequencies
                  int average_n_times,          // Number of repeat times
                  bool correction,              // Non-stationary correction
                  double wait_for_steady,       // Number of periods to wait
                  bool processData,
                  BIO_TechniqueContext **context);

// GEIS (Galvano Electrochemical Impedance Spectroscopy)
int BIO_StartGEIS(int ID, uint8_t channel,
                 bool vs_initial,               // Current step vs initial
                 double initial_current_step,   // Initial current step (A)
                 double duration_step,          // Step duration (s)
                 double record_every_dT,        // Record every dt (s)
                 double record_every_dE,        // Record every dE (V)
                 double initial_freq,           // Initial frequency (Hz)
                 double final_freq,             // Final frequency (Hz)
                 bool sweep_linear,             // TRUE for linear, FALSE for logarithmic
                 double amplitude_current,      // Sine amplitude (A)
                 int frequency_number,          // Number of frequencies
                 int average_n_times,           // Number of repeat times
                 bool correction,               // Non-stationary correction
                 double wait_for_steady,        // Number of periods to wait
                 int i_range,                   // Current range (cannot be auto)
                 bool processData,
                 BIO_TechniqueContext **context);

// SGEIS (Staircase Galvano Electrochemical Impedance Spectroscopy)
int BIO_StartSGEIS(int ID, uint8_t channel,
                  bool vs_initial,              // Current step vs initial
                  bool vs_final,                // Current step vs final
                  double initial_current_step,  // Initial current step (A)
                  double final_current_step,    // Final current step (A)
                  double duration_step,         // Step duration (s)
                  int step_number,              // Number of current steps [0..98]
                  double record_every_dT,       // Record every dt (s)
                  double record_every_dE,       // Record every dE (V)
                  double initial_freq,          // Initial frequency (Hz)
                  double final_freq,            // Final frequency (Hz)
                  bool sweep_linear,            // TRUE for linear, FALSE for logarithmic
                  double amplitude_current,     // Sine amplitude (A)
                  int frequency_number,         // Number of frequencies
                  int average_n_times,          // Number of repeat times
                  bool correction,              // Non-stationary correction
                  double wait_for_steady,       // Number of periods to wait
                  int i_range,                  // Current range (cannot be auto)
                  bool processData,
                  BIO_TechniqueContext **context);

// ============================================================================
// Error Codes
// ============================================================================

// Success
#define BIO_SUCCESS                          0

// Raw Device Error Codes (returned directly by BioLogic device - DO NOT OFFSET)
// These are the actual codes returned by the EClib.dll functions
#define BIO_DEV_NOINSTRUMENTCONNECTED       -1
#define BIO_DEV_CONNECTIONINPROGRESS        -2
#define BIO_DEV_CHANNELNOTPLUGGED           -3
#define BIO_DEV_INVALIDPARAMETERS           -4
#define BIO_DEV_FILENOTEXISTS               -5
#define BIO_DEV_FUNCTIONFAILED              -6
#define BIO_DEV_NOCHANNELSELECTED           -7
#define BIO_DEV_INVALIDCONFIGURATION        -8
#define BIO_DEV_ECLABFIRMWARE               -9
#define BIO_DEV_LIBRARYNOTLOADED           -10
#define BIO_DEV_USBLIBRARYNOTLOADED        -11
#define BIO_DEV_FUNCTIONINPROGRESS         -12
#define BIO_DEV_CHANNELALREADYUSED         -13
#define BIO_DEV_DEVICENOTALLOWED           -14
#define BIO_DEV_INVALIDUPDATEPARAMETERS    -15

// Device instrument errors (offset -100)
#define BIO_DEV_INSTRUMENT_COMMFAILED      -101
#define BIO_DEV_INSTRUMENT_TOOMANYDATA     -102
#define BIO_DEV_INSTRUMENT_NOTPLUGGED      -103
#define BIO_DEV_INSTRUMENT_INVALIDRESPONSE -104
#define BIO_DEV_INSTRUMENT_INVALIDSIZE     -105

// Device communication errors (offset -200)
#define BIO_DEV_COMM_FAILED                -200
#define BIO_DEV_COMM_CONNECTIONFAILED      -201
#define BIO_DEV_COMM_WAITINGRESPONSE       -202
#define BIO_DEV_COMM_INVALIDADDRESS        -203
#define BIO_DEV_COMM_ALLOCMEMORY           -204
#define BIO_DEV_COMM_LOADFIRMWARE          -205
#define BIO_DEV_COMM_INCOMPATIBLE          -206
#define BIO_DEV_COMM_MAXCONNECTIONS        -207

// Device firmware errors (offset -300)
#define BIO_DEV_FIRM_KERNELNOTFOUND        -300
#define BIO_DEV_FIRM_KERNELREAD            -301
#define BIO_DEV_FIRM_KERNELINVALID         -302
#define BIO_DEV_FIRM_KERNELLOAD            -303
#define BIO_DEV_FIRM_XLXNOTFOUND           -304
#define BIO_DEV_FIRM_XLXREAD               -305
#define BIO_DEV_FIRM_XLXINVALID            -306
#define BIO_DEV_FIRM_XLXLOAD               -307
#define BIO_DEV_FIRM_FIRMWARENOTLOADED     -308
#define BIO_DEV_FIRM_INCOMPATIBLE          -309

// Device technique errors (offset -400)
#define BIO_DEV_TECH_ECCFILENOTFOUND       -400
#define BIO_DEV_TECH_INCOMPATIBLE          -401
#define BIO_DEV_TECH_ECCFILECORRUPTED      -402
#define BIO_DEV_TECH_LOADTECHNIQUE         -403
#define BIO_DEV_TECH_DATACORRUPTED         -404
#define BIO_DEV_TECH_MEMFULL               -405

// Program-Specific Error Codes (using ERR_BASE_BIOLOGIC offset from common.h)
// Data retrieval errors
#define BIO_ERR_NO_DATA_RETRIEVED          (ERR_BASE_BIOLOGIC - 1)
#define BIO_ERR_NO_MEMORY_FILLED           (ERR_BASE_BIOLOGIC - 2)
#define BIO_ERR_WRONG_PROCESS_INDEX        (ERR_BASE_BIOLOGIC - 3)
#define BIO_ERR_DATA_COPY_FAILED           (ERR_BASE_BIOLOGIC - 4)
#define BIO_ERR_INVALID_DATA_BUFFER        (ERR_BASE_BIOLOGIC - 5)

// Technique state machine errors
#define BIO_ERR_TECHNIQUE_NOT_COMPLETE     (ERR_BASE_BIOLOGIC - 10)
#define BIO_ERR_TECHNIQUE_TIMEOUT          (ERR_BASE_BIOLOGIC - 11)
#define BIO_ERR_TECHNIQUE_CANCELLED        (ERR_BASE_BIOLOGIC - 12)
#define BIO_ERR_INVALID_TECHNIQUE_STATE    (ERR_BASE_BIOLOGIC - 13)

// Data processing errors
#define BIO_ERR_DATA_CONVERSION_FAILED     (ERR_BASE_BIOLOGIC - 20)
#define BIO_ERR_UNKNOWN_TECHNIQUE_ID       (ERR_BASE_BIOLOGIC - 21)
#define BIO_ERR_INVALID_VARIABLE_COUNT     (ERR_BASE_BIOLOGIC - 22)
#define BIO_ERR_MEMORY_ALLOCATION_FAILED   (ERR_BASE_BIOLOGIC - 23)

// Context and parameter errors
#define BIO_ERR_INVALID_CONTEXT            (ERR_BASE_BIOLOGIC - 30)
#define BIO_ERR_CONTEXT_NOT_INITIALIZED    (ERR_BASE_BIOLOGIC - 31)
#define BIO_ERR_INVALID_CHANNEL_TYPE       (ERR_BASE_BIOLOGIC - 32)

// Partial data (special case)
#define BIO_ERR_PARTIAL_DATA               (ERR_BASE_BIOLOGIC - 50)

#endif // BIOLOGIC_H