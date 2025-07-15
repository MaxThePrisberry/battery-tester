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
} BL_RawDataBuffer;

// Converted data storage
typedef struct {
    int numPoints;
    int numVariables;
    char **variableNames;    // Array of variable names
    char **variableUnits;    // Array of units
    double **data;           // 2D array of converted values
    int techniqueID;
    int processIndex;
} BL_ConvertedData;

// Extended raw data buffer with optional converted data
typedef struct {
    // Original raw data
    BL_RawDataBuffer *rawData;
    
    // Converted data (if processData was true)
    BL_ConvertedData *convertedData;
} BL_TechniqueData;

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
} BL_TechniqueConfig;

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
    BL_TechniqueConfig config;
    
    // Data collection
    BL_RawDataBuffer rawData;
	BL_ConvertedData *convertedData;
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
    
} BL_TechniqueContext;

// ============================================================================
// Main BioLogic Library Management Functions
// ============================================================================
int InitializeBioLogic(void);
void CleanupBioLogic(void);
bool IsBioLogicInitialized(void);

// ============================================================================
// BLFind Library Management Functions
// ============================================================================
int InitializeBLFind(void);
void CleanupBLFind(void);
bool IsBLFindInitialized(void);

// ============================================================================
// Helper Functions
// ============================================================================
void ConvertUnicodeToAscii(const char* unicode, char* ascii, int unicodeLen);
const char* BL_GetErrorString(int errorCode);

// ============================================================================
// Device Scanning Functions (from blfind.dll)
// ============================================================================
int ScanForBioLogicDevices(void);
int BL_FindEChemDev(char* pLstDev, unsigned int* pSize, unsigned int* pNbrDevice);
int BL_FindEChemEthDev(char* pLstDev, unsigned int* pSize, unsigned int* pNbrDevice);
int BL_FindEChemUsbDev(char* pLstDev, unsigned int* pSize, unsigned int* pNbrDevice);
int BL_FindEChemBCSDev(char* pLstDev, unsigned int* pSize, unsigned int* pNbrDevice);
int BL_FindKineticDev(char* pLstDev, unsigned int* pSize, unsigned int* pNbrDevice);
int BL_FindKineticEthDev(char* pLstDev, unsigned int* pSize, unsigned int* pNbrDevice);
int BL_FindKineticUsbDev(char* pLstDev, unsigned int* pSize, unsigned int* pNbrDevice);
int BL_EChemBCSEthDEV(void* param1, void* param2);
int BL_Init_Path(const char* path);
int BL_SetConfig(char* pIp, char* pCfg);
int BL_SetMAC(char* mac);
int BLFind_GetErrorMsg(int errorcode, char* pmsg, unsigned int* psize);

// ============================================================================
// Connection Functions (from EClib.dll)
// ============================================================================
int BL_Connect(const char* address, uint8_t timeout, int* pID, TDeviceInfos_t* pInfos);
int BL_Disconnect(int ID);
int BL_TestConnection(int ID);
int BL_TestCommSpeed(int ID, uint8_t channel, int* spd_rcvt, int* spd_kernel);

// ============================================================================
// General Functions (from EClib.dll)
// ============================================================================
int BL_GetLibVersion(char* pVersion, unsigned int* psize);
unsigned int BL_GetVolumeSerialNumber(void);
int BL_GetErrorMsg(int errorcode, char* pmsg, unsigned int* psize);
int BL_GetUSBdeviceinfos(unsigned int USBindex, char* pcompany, unsigned int* pcompanysize, 
                         char* pdevice, unsigned int* pdevicesize, char* pSN, unsigned int* pSNsize);

// ============================================================================
// Firmware Functions (from EClib.dll)
// ============================================================================
int BL_LoadFirmware(int ID, uint8_t* pChannels, int* pResults, uint8_t Length, 
                    bool ShowGauge, bool ForceReload, const char* BinFile, const char* XlxFile);
int BL_LoadFlash(int ID, const char* pfname, bool ShowGauge);

// ============================================================================
// Channel Information Functions (from EClib.dll)
// ============================================================================
bool BL_IsChannelPlugged(int ID, uint8_t ch);
int BL_GetChannelsPlugged(int ID, uint8_t* pChPlugged, uint8_t Size);
int BL_GetChannelInfos(int ID, uint8_t ch, TChannelInfos_t* pInfos);
int BL_GetMessage(int ID, uint8_t ch, char* msg, unsigned int* size);
int BL_GetHardConf(int ID, uint8_t ch, THardwareConf_t* pHardConf);
int BL_SetHardConf(int ID, uint8_t ch, THardwareConf_t HardConf);
int BL_GetChannelBoardType(int ID, uint8_t Channel, uint32_t* pChannelType);
int BL_GetChannelFloatFormat(int ID, uint8_t channel, int* pFormat);
int BL_GetFPGAVer(int ID, uint8_t channel, uint32_t* pVersion);

// ============================================================================
// Module Functions (from EClib.dll)
// ============================================================================
bool BL_IsModulePlugged(int ID, uint8_t module);
int BL_GetModulesPlugged(int ID, uint8_t* pModPlugged, uint8_t Size);
int BL_GetModuleInfos(int ID, uint8_t module, void* pInfos);

// ============================================================================
// Technique Functions (from EClib.dll)
// ============================================================================
int BL_LoadTechnique(int ID, uint8_t channel, const char* pFName, TEccParams_t Params, 
                     bool FirstTechnique, bool LastTechnique, bool DisplayParams);
int BL_LoadTechnique_LV(int ID, uint8_t channel, const char* pFName, void* Params, 
                        bool FirstTechnique, bool LastTechnique, bool DisplayParams);
int BL_LoadTechnique_VEE(int ID, uint8_t channel, const char* pFName, void* Params, 
                         bool FirstTechnique, bool LastTechnique, bool DisplayParams);
int BL_DefineBoolParameter(const char* lbl, bool value, int index, TEccParam_t* pParam);
int BL_DefineSglParameter(const char* lbl, float value, int index, TEccParam_t* pParam);
int BL_DefineIntParameter(const char* lbl, int value, int index, TEccParam_t* pParam);
int BL_UpdateParameters(int ID, uint8_t channel, int TechIndx, TEccParams_t Params, const char* EccFileName);
int BL_UpdateParameters_LV(int ID, uint8_t channel, int TechIndx, void* Params, const char* EccFileName);
int BL_UpdateParameters_VEE(int ID, uint8_t channel, int TechIndx, void* Params, const char* EccFileName);
int BL_GetTechniqueInfos(int ID, uint8_t channel, int TechIndx, void* pInfos);
int BL_GetParamInfos(int ID, uint8_t channel, int TechIndx, int ParamIndx, void* pInfos);
int BL_ReadParameters(int ID, uint8_t channel, void* pParams);

// ============================================================================
// Start/Stop Functions (from EClib.dll)
// ============================================================================
int BL_StartChannel(int ID, uint8_t channel);
int BL_StartChannels(int ID, uint8_t* pChannels, int* pResults, uint8_t length);
int BL_StopChannel(int ID, uint8_t channel);
int BL_StopChannels(int ID, uint8_t* pChannels, int* pResults, uint8_t length);

// ============================================================================
// Data Acquisition Functions (from EClib.dll)
// ============================================================================
int BL_GetCurrentValues(int ID, uint8_t channel, TCurrentValues_t* pValues);
int BL_GetCurrentValuesBk(int ID, uint8_t channel, void* pValues);
int BL_GetData(int ID, uint8_t channel, TDataBuffer_t* pBuf, TDataInfos_t* pInfos, TCurrentValues_t* pValues);
int BL_GetDataBk(int ID, uint8_t channel, void* pBuf, void* pInfos, void* pValues);
int BL_GetData_LV(int ID, uint8_t channel, void* pBuf, void* pInfos, void* pValues);
int BL_GetData_VEE(int ID, uint8_t channel, void* pBuf, void* pInfos, void* pValues);
int BL_GetFCTData(int ID, uint8_t channel, TDataBuffer_t* pBuf, TDataInfos_t* pInfos, TCurrentValues_t* pValues);

// ============================================================================
// Data Conversion Functions (from EClib.dll)
// ============================================================================
int BL_ConvertNumericIntoSingle(unsigned int num, float* psgl);
int BL_ConvertNumericIntoFloat(unsigned int num, double* pdbl);
int BL_ConvertChannelNumericIntoSingle(uint32_t num, float* pRetFloat, uint32_t ChannelType);
int BL_ConvertTimeChannelNumericIntoSeconds(uint32_t* pnum, double* pRetTime, float Timebase, uint32_t ChannelType);
int BL_ConvertTimeChannelNumericIntoTimebases(uint32_t* pnum, double* pRetTime, float* pTimebases, uint32_t ChannelType);

// ============================================================================
// Experiment Functions (from EClib.dll)
// ============================================================================
int BL_SetExperimentInfos(int ID, uint8_t channel, TExperimentInfos_t TExpInfos);
int BL_GetExperimentInfos(int ID, uint8_t channel, TExperimentInfos_t* TExpInfos);

// ============================================================================
// Advanced Communication Functions (from EClib.dll)
// ============================================================================
int BL_SendMsg(int ID, uint8_t ch, void* pBuf, unsigned int* pLen);
int BL_SendMsgToRcvt(int ID, void* pBuf, unsigned int* pLen);
int BL_SendMsgToRcvt_g(int ID, uint8_t ch, void* pBuf, unsigned int* pLen);
int BL_SendEcalMsg(int ID, uint8_t ch, void* pBuf, unsigned int* pLen);
int BL_SendEcalMsgGroup(int ID, uint8_t* pChannels, uint8_t length, void* pBuf, unsigned int* pLen);
int BL_GetOptErr(int ID, uint8_t channel, int* pOptErr, int* pOptPos);

// ============================================================================
// High-Level Technique Functions
// ============================================================================

// Context management
BL_TechniqueContext* BL_CreateTechniqueContext(int ID, uint8_t channel, BioTechniqueType type);
void BL_FreeTechniqueContext(BL_TechniqueContext *context);

// Generic technique lifecycle
int BL_UpdateTechnique(BL_TechniqueContext *context);
bool BL_IsTechniqueComplete(BL_TechniqueContext *context);
int BL_StopTechnique(BL_TechniqueContext *context);
int BL_GetTechniqueRawData(BL_TechniqueContext *context, BL_RawDataBuffer **data);
int BL_GetTechniqueData(BL_TechniqueContext *context, BL_TechniqueData **data);

// Data processing function
int BL_ProcessTechniqueData(BL_RawDataBuffer *rawData, int techniqueID, int processIndex,
                           uint32_t channelType, float timebase,
                           BL_ConvertedData **convertedData);

// Helper to free converted data
void BL_FreeConvertedData(BL_ConvertedData *data);

// Helper to free technique data (raw + converted)
void BL_FreeTechniqueData(BL_TechniqueData *data);

BL_RawDataBuffer* BL_CopyRawDataBuffer(BL_RawDataBuffer *src);

// OCV (Open Circuit Voltage)
int BL_StartOCV(int ID, uint8_t channel,
                double duration_s,
                double sample_interval_s,
                double record_every_dE,     // mV
                double record_every_dT,     // seconds
                int e_range,                // 0=2.5V, 1=5V, 2=10V, 3=Auto
                bool processData,
                BL_TechniqueContext **context);

// PEIS (Potentio Electrochemical Impedance Spectroscopy)
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
                 BL_TechniqueContext **context);

// SPEIS (Staircase Potentio Electrochemical Impedance Spectroscopy)
int BL_StartSPEIS(int ID, uint8_t channel,
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
                  BL_TechniqueContext **context);

// GEIS (Galvano Electrochemical Impedance Spectroscopy)
int BL_StartGEIS(int ID, uint8_t channel,
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
                 BL_TechniqueContext **context);

// SGEIS (Staircase Galvano Electrochemical Impedance Spectroscopy)
int BL_StartSGEIS(int ID, uint8_t channel,
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
                  BL_TechniqueContext **context);

// ============================================================================
// Error Codes (for reference)
// ============================================================================
#define BL_SUCCESS                          0
#define BL_ERR_NOINSTRUMENTCONNECTED       -1
#define BL_ERR_CONNECTIONINPROGRESS        -2
#define BL_ERR_CHANNELNOTPLUGGED           -3
#define BL_ERR_INVALIDPARAMETERS           -4
#define BL_ERR_FILENOTEXISTS               -5
#define BL_ERR_FUNCTIONFAILED              -6
#define BL_ERR_NOCHANNELSELECTED           -7
#define BL_ERR_INVALIDCONFIGURATION        -8
#define BL_ERR_ECLABFIRMWARE               -9
#define BL_ERR_LIBRARYNOTLOADED           -10
#define BL_ERR_USBLIBRARYNOTLOADED        -11
#define BL_ERR_FUNCTIONINPROGRESS         -12
#define BL_ERR_CHANNELALREADYUSED         -13
#define BL_ERR_DEVICENOTALLOWED           -14
#define BL_ERR_INVALIDUPDATEPARAMETERS    -15

// Instrument errors (offset -100)
#define BL_ERR_INSTRUMENT_COMMFAILED      -101
#define BL_ERR_INSTRUMENT_TOOMANYDATA     -102
#define BL_ERR_INSTRUMENT_NOTPLUGGED      -103
#define BL_ERR_INSTRUMENT_INVALIDRESPONSE -104
#define BL_ERR_INSTRUMENT_INVALIDSIZE     -105

// Communication errors (offset -200)
#define BL_ERR_COMM_FAILED                -200
#define BL_ERR_COMM_CONNECTIONFAILED      -201
#define BL_ERR_COMM_WAITINGRESPONSE       -202
#define BL_ERR_COMM_INVALIDADDRESS        -203
#define BL_ERR_COMM_ALLOCMEMORY           -204
#define BL_ERR_COMM_LOADFIRMWARE          -205
#define BL_ERR_COMM_INCOMPATIBLE          -206
#define BL_ERR_COMM_MAXCONNECTIONS        -207

// Firmware errors (offset -300)
#define BL_ERR_FIRM_KERNELNOTFOUND        -300
#define BL_ERR_FIRM_KERNELREAD            -301
#define BL_ERR_FIRM_KERNELINVALID         -302
#define BL_ERR_FIRM_KERNELLOAD            -303
#define BL_ERR_FIRM_XLXNOTFOUND           -304
#define BL_ERR_FIRM_XLXREAD               -305
#define BL_ERR_FIRM_XLXINVALID            -306
#define BL_ERR_FIRM_XLXLOAD               -307
#define BL_ERR_FIRM_FIRMWARENOTLOADED     -308
#define BL_ERR_FIRM_INCOMPATIBLE          -309

// Technique errors (offset -400)
#define BL_ERR_TECH_ECCFILENOTFOUND       -400
#define BL_ERR_TECH_INCOMPATIBLE          -401
#define BL_ERR_TECH_ECCFILECORRUPTED      -402
#define BL_ERR_TECH_LOADTECHNIQUE         -403
#define BL_ERR_TECH_DATACORRUPTED         -404
#define BL_ERR_TECH_MEMFULL               -405

#endif // BIOLOGIC_H