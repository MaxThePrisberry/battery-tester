/**
 * @file eclab_olecom_interface.h
 * @brief EC-Lab Custom COM Interface Definition (IEClabExe)
 *
 * This file defines the custom COM interface for EC-Lab automation.
 * Generated from OleView output: notes/ECLabCOM_EClabExeInterface.txt
 *
 * IMPORTANT: EC-Lab does NOT support IDispatch automation!
 * You must use this custom interface (IEClabExe) with direct vtable calls.
 *
 * @author Battery Exploder Team
 * @date 2025-01-04
 */

#ifndef ECLAB_OLECOM_INTERFACE_H
#define ECLAB_OLECOM_INTERFACE_H

#include <windows.h>
#include <ole2.h>

/**
 * Interface ID for IEClabExe
 * From interface.txt line 3: uuid(642C68D2-85BD-494B-93EB-583CCBB11794)
 */
static const IID IID_IEClabExe = {
    0x642C68D2, 0x85BD, 0x494B,
    {0x93, 0xEB, 0x58, 0x3C, 0xCB, 0xB1, 0x17, 0x94}
};

/**
 * Class ID for EClabExe coclass
 * From interface.txt line 169: uuid(77FE5C93-42EE-4127-944B-5BA14FD33447)
 */
static const CLSID CLSID_EClabExe = {
    0x77FE5C93, 0x42EE, 0x4127,
    {0x94, 0x4B, 0x5B, 0xA1, 0x4F, 0xD3, 0x34, 0x47}
};

/* Forward declaration */
typedef struct IEClabExe IEClabExe;

/**
 * @brief IEClabExe Virtual Table
 *
 * This structure defines all methods available in the IEClabExe interface.
 * Methods are listed in the exact order they appear in the vtable.
 *
 * WARNING: Order is critical! Do not rearrange methods.
 */
typedef struct IEClabExeVtbl {

    /* ===== IUnknown Methods (3 methods) ===== */

    HRESULT (STDMETHODCALLTYPE *QueryInterface)(
        IEClabExe *This,
        REFIID riid,
        void **ppvObject);

    ULONG (STDMETHODCALLTYPE *AddRef)(
        IEClabExe *This);

    ULONG (STDMETHODCALLTYPE *Release)(
        IEClabExe *This);


    /* ===== IEClabExe Non-_TS Methods (13 methods) ===== */
    /* These return int (1=success, 0=failure) */

    /**
     * @brief Connect to a device
     * @param DeviceNumber Device number in list (0-based)
     * @return 1 if connected, 0 if failed
     */
    int (STDMETHODCALLTYPE *ConnectDevice)(
        IEClabExe *This,
        int DeviceNumber);

    /**
     * @brief Disconnect from a device
     * @param DeviceNumber Device number in list (0-based)
     * @return 1 if disconnected, 0 if failed
     */
    int (STDMETHODCALLTYPE *DisconnectDevice)(
        IEClabExe *This,
        int DeviceNumber);

    /**
     * @brief Retrieve DC data point from .mpr file
     * @param FileName Absolute path to .mpr file
     * @param DataIndex Data point index (0-based)
     * @param Data Output VARIANT array: [time(s), voltage(V), current(A)]
     * @return 1 if success, 0 if failed
     */
    int (STDMETHODCALLTYPE *MeasureDcValue)(
        IEClabExe *This,
        BSTR FileName,
        int DataIndex,
        VARIANT *Data);

    /**
     * @brief Retrieve EIS data point from .mpr file
     * @param FileName Absolute path to .mpr file
     * @param DataIndex Data point index (0-based)
     * @param Data Output VARIANT array: [time(s), freq(Hz), Re(Z)(Ohm), -Im(Z)(Ohm)]
     * @return 1 if success, 0 if failed
     */
    int (STDMETHODCALLTYPE *MeasureEisValue)(
        IEClabExe *This,
        BSTR FileName,
        int DataIndex,
        VARIANT *Data);

    /**
     * @brief Get number of data points in .mpr file
     * @param FileName Absolute path to .mpr file
     * @return Number of points, or 0 if failed
     */
    int (STDMETHODCALLTYPE *MeasureNumberOfPoints)(
        IEClabExe *This,
        BSTR FileName);

    /**
     * @brief Get list of connected channels for a device
     * @param Device Device number (0-based)
     * @param ChannelArray Output VARIANT array of 128 bools (1=connected, 0=not)
     * @return 1 if success, 0 if failed
     */
    int (STDMETHODCALLTYPE *GetDeviceChannelList)(
        IEClabExe *This,
        int Device,
        VARIANT *ChannelArray);

    /**
     * @brief Load experiment settings from .mps file
     * @param Device Device number (0-based)
     * @param Channel Channel number (0-based)
     * @param FileName Absolute path to .mps settings file
     * @return 1 if loaded successfully, 0 if failed
     */
    int (STDMETHODCALLTYPE *LoadSettings)(
        IEClabExe *This,
        int Device,
        int Channel,
        BSTR FileName);

    /**
     * @brief Run experiment on channel
     * @param Device Device number (0-based)
     * @param Channel Channel number (0-based)
     * @param FileName Absolute path for output .mpr data file
     * @return 1 if started successfully, 0 if failed
     */
    int (STDMETHODCALLTYPE *RunChannel)(
        IEClabExe *This,
        int Device,
        int Channel,
        BSTR FileName);

    /**
     * @brief Stop running experiment on channel
     * @param Device Device number (0-based)
     * @param Channel Channel number (0-based)
     * @return 1 if stopped successfully, 0 if failed
     */
    int (STDMETHODCALLTYPE *StopChannel)(
        IEClabExe *This,
        int Device,
        int Channel);

    /**
     * @brief Get data filename for a technique
     * @param Device Device number (0-based)
     * @param Channel Channel number (0-based)
     * @param Technique Technique number (0-based)
     * @param FileName Output VARIANT containing .mpr filename
     * @return 1 if success, 0 if failed
     */
    int (STDMETHODCALLTYPE *GetDataFileName)(
        IEClabExe *This,
        int Device,
        int Channel,
        int Technique,
        VARIANT *FileName);

    /**
     * @brief Get current status of channel
     * @param Device Device number (0-based)
     * @param Channel Channel number (0-based)
     * @param CurrentValues Output VARIANT array of 32 values (see manual section 3.2.9)
     * @return 1 if success, 0 if failed
     */
    int (STDMETHODCALLTYPE *MeasureStatus)(
        IEClabExe *This,
        int Device,
        int Channel,
        VARIANT *CurrentValues);

    /**
     * @brief Test if device is connected
     * @param DeviceNumber Device number (0-based)
     * @return 1 if connected, 0 if not connected
     */
    int (STDMETHODCALLTYPE *TestConnection)(
        IEClabExe *This,
        int DeviceNumber);

    /**
     * @brief Connect to device by IP address
     * @param IPaddress IP address string
     * @param DeviceNumber Output device number assigned
     * @return 1 if connected, 0 if failed
     */
    int (STDMETHODCALLTYPE *ConnectDeviceByIP)(
        IEClabExe *This,
        BSTR IPaddress,
        int *DeviceNumber);


    /* ===== IEClabExe _TS Methods (TestStand variants - 13 methods) ===== */
    /* These return HRESULT and have FunctionResult output parameter */

    HRESULT (STDMETHODCALLTYPE *ConnectDevice_TS)(
        IEClabExe *This,
        int DeviceNumber,
        int *FunctionResult);

    HRESULT (STDMETHODCALLTYPE *DisconnectDevice_TS)(
        IEClabExe *This,
        int DeviceNumber,
        int *FunctionResult);

    HRESULT (STDMETHODCALLTYPE *MeasureDCValue_TS)(
        IEClabExe *This,
        BSTR FileName,
        int DataIndex,
        VARIANT *Data,
        int *FunctionResult);

    HRESULT (STDMETHODCALLTYPE *MeasureEisValue_TS)(
        IEClabExe *This,
        BSTR FileName,
        int DataIndex,
        VARIANT *Data,
        int *FunctionResult);

    HRESULT (STDMETHODCALLTYPE *MeasureNumberOfPoints_TS)(
        IEClabExe *This,
        BSTR FileName,
        int *FunctionResult);

    HRESULT (STDMETHODCALLTYPE *GetDeviceChannelList_TS)(
        IEClabExe *This,
        int Device,
        VARIANT *ChannelArray,
        int *FunctionResult);

    HRESULT (STDMETHODCALLTYPE *LoadSettings_TS)(
        IEClabExe *This,
        int Device,
        int Channel,
        BSTR FileName,
        int *FunctionResult);

    HRESULT (STDMETHODCALLTYPE *RunChannel_TS)(
        IEClabExe *This,
        int Device,
        int Channel,
        BSTR FileName,
        int *FunctionResult);

    HRESULT (STDMETHODCALLTYPE *StopChannel_TS)(
        IEClabExe *This,
        int Device,
        int Channel,
        int *FunctionResult);

    HRESULT (STDMETHODCALLTYPE *GetDataFileName_TS)(
        IEClabExe *This,
        int Device,
        int Channel,
        int Technique,
        VARIANT *FileName,
        int *FunctionResult);

    HRESULT (STDMETHODCALLTYPE *MeasureStatus_TS)(
        IEClabExe *This,
        int Device,
        int Channel,
        VARIANT *CurrentValues,
        int *FunctionResult);

    HRESULT (STDMETHODCALLTYPE *TestConnection_TS)(
        IEClabExe *This,
        int DeviceNumber,
        int *FunctionResult);

    HRESULT (STDMETHODCALLTYPE *ConnectDeviceByIP_TS)(
        IEClabExe *This,
        BSTR IPaddress,
        int *DeviceNumber,
        int *FunctionResult);


    /* ===== Additional Methods (14 methods) ===== */

    /**
     * @brief Get device serial number and module serial numbers
     * @param DeviceNumber Device number (0-based)
     * @param DeviceSN Output device serial number
     * @param ModuleSNArray Output VARIANT array of 16 module serial numbers
     * @param FunctionResult Output 1=success, 0=failure
     * @return HRESULT
     */
    HRESULT (STDMETHODCALLTYPE *GetDeviceSN)(
        IEClabExe *This,
        int DeviceNumber,
        int *DeviceSN,
        VARIANT *ModuleSNArray,
        int *FunctionResult);

    /**
     * @brief Select a device (make active in GUI)
     * @param Device Device number (0-based)
     * @param FunctionResult Output 1=success, 0=failure
     * @return HRESULT
     */
    HRESULT (STDMETHODCALLTYPE *SelectDevice)(
        IEClabExe *This,
        int Device,
        int *FunctionResult);

    /**
     * @brief Select a channel (make active in GUI)
     * @param Device Device number (0-based)
     * @param Channel Channel number (0-based)
     * @param FunctionResult Output 1=success, 0=failure
     * @return HRESULT
     */
    HRESULT (STDMETHODCALLTYPE *SelectChannel)(
        IEClabExe *This,
        int Device,
        int Channel,
        int *FunctionResult);

    /**
     * @brief Retrieve variable value from .mpr file by variable code
     * @param FileName Absolute path to .mpr file
     * @param VarCode Variable code (see manual annex 4.4)
     * @param DataIndex Data point index (0-based)
     * @param Data Output value
     * @param FunctionResult Output 1=success, 0=failure
     * @return HRESULT
     */
    HRESULT (STDMETHODCALLTYPE *MeasureValueByCode)(
        IEClabExe *This,
        BSTR FileName,
        int VarCode,
        int DataIndex,
        double *Data,
        int *FunctionResult);

    /**
     * @brief Retrieve variable value from .mpr file by variable name
     * @param FileName Absolute path to .mpr file
     * @param VarID Variable name (e.g., "time/s", "Ewe/V")
     * @param DataIndex Data point index (0-based)
     * @param Data Output value
     * @param FunctionResult Output 1=success, 0=failure
     * @return HRESULT
     */
    HRESULT (STDMETHODCALLTYPE *MeasureValueByID)(
        IEClabExe *This,
        BSTR FileName,
        BSTR VarID,
        int DataIndex,
        double *Data,
        int *FunctionResult);

    /**
     * @brief Group channels from multiple devices
     * @param NbChannels Number of channels to group
     * @param ChannelsArray VARIANT array of device/channel pairs
     * @param FunctionResult Output 1=success, 0=failure
     * @return HRESULT
     */
    HRESULT (STDMETHODCALLTYPE *GroupChannels)(
        IEClabExe *This,
        int NbChannels,
        VARIANT ChannelsArray,
        int *FunctionResult);

    /**
     * @brief Get device type (NOT IMPLEMENTED in EC-Lab)
     * @param DeviceNumber Device number (0-based)
     * @param DeviceType Output device type
     * @param FunctionResult Output 1=success, 0=failure
     * @return HRESULT
     */
    HRESULT (STDMETHODCALLTYPE *GetDeviceType)(
        IEClabExe *This,
        int DeviceNumber,
        VARIANT *DeviceType,
        int *FunctionResult);

    /**
     * @brief Get experiment information
     * @param DeviceNumber Device number (0-based)
     * @param ChannelNumber Channel number (0-based)
     * @param ExpStartTime Output start time string
     * @param ExpEndTime Output end time string
     * @param ExpPath Output experiment path
     * @param ExpDataFiles Output VARIANT array of data filenames
     * @param FunctionResult Output 1=success, 0=failure
     * @return HRESULT (S_OK=0, S_FALSE=1, E_FAIL=0x80004005)
     */
    HRESULT (STDMETHODCALLTYPE *GetExperimentInfos)(
        IEClabExe *This,
        int DeviceNumber,
        int ChannelNumber,
        VARIANT *ExpStartTime,
        VARIANT *ExpEndTime,
        VARIANT *ExpPath,
        VARIANT *ExpDataFiles,
        int *FunctionResult);

    /**
     * @brief Copy .mps to .mps (NOT IMPLEMENTED)
     * @param InFileName Input .mps file
     * @param OutFileName Output .mps file
     * @param FunctionResult Output 1=success, 0=failure
     * @return HRESULT
     */
    HRESULT (STDMETHODCALLTYPE *CopyMpsToMps)(
        IEClabExe *This,
        BSTR InFileName,
        BSTR *OutFileName,
        int *FunctionResult);

    /**
     * @brief Copy .mpr to .mps (NOT IMPLEMENTED)
     * @param InFileName Input .mpr file
     * @param OutFileName Output .mps file
     * @param FunctionResult Output 1=success, 0=failure
     * @return HRESULT
     */
    HRESULT (STDMETHODCALLTYPE *CopyMprToMps)(
        IEClabExe *This,
        BSTR InFileName,
        BSTR *OutFileName,
        int *FunctionResult);

    /**
     * @brief Copy .mpt to .mps (NOT IMPLEMENTED)
     * @param InFileName Input .mpt file
     * @param OutFileName Output .mps file
     * @param FunctionResult Output 1=success, 0=failure
     * @return HRESULT
     */
    HRESULT (STDMETHODCALLTYPE *CopyMptToMps)(
        IEClabExe *This,
        BSTR InFileName,
        BSTR *OutFileName,
        int *FunctionResult);

    /**
     * @brief Enable/disable EC-Lab popup messages during OLE COM session
     * @param EnabledWinMess 1=enable messages, 0=disable messages
     * @param FunctionResult Output 1=success, 0=failure
     * @return HRESULT
     */
    HRESULT (STDMETHODCALLTYPE *EnableMessagesWindows)(
        IEClabExe *This,
        int EnabledWinMess,
        int *FunctionResult);

    /**
     * @brief Get EC-Lab software version (NOT IMPLEMENTED)
     * @param SoftwareVersion Output version string
     * @param FunctionResult Output 1=success, 0=failure
     * @return HRESULT
     */
    HRESULT (STDMETHODCALLTYPE *GetSoftwareVersion)(
        IEClabExe *This,
        BSTR *SoftwareVersion,
        int *FunctionResult);

    /**
     * @brief Get channel information (amplifier, options, etc.)
     * @param Device Device number (0-based)
     * @param Channel Channel number (0-based)
     * @param ChannelInfos Output VARIANT array of 5 elements:
     *                     [0]=SN, [1]=amplifier ID, [2]=unused, [3]=option, [4]=unused
     * @param FunctionResult Output 1=success, 0=failure
     * @return HRESULT
     */
    HRESULT (STDMETHODCALLTYPE *GetChannelInfos)(
        IEClabExe *This,
        int Device,
        int Channel,
        VARIANT *ChannelInfos,
        int *FunctionResult);

} IEClabExeVtbl;

/**
 * @brief IEClabExe Interface Structure
 *
 * This is the actual interface pointer you'll use to call methods.
 *
 * Example usage:
 *   IEClabExe *pECLab;
 *   CoCreateInstance(&CLSID_EClabExe, ..., &IID_IEClabExe, (void**)&pECLab);
 *   int result = pECLab->lpVtbl->ConnectDevice(pECLab, 0);
 */
struct IEClabExe {
    struct IEClabExeVtbl *lpVtbl;
};

/* ===== Convenience Macros ===== */

/**
 * Macro to simplify method calls
 * Usage: IEClabExe_ConnectDevice(pInterface, deviceNum)
 */
#define IEClabExe_QueryInterface(This,riid,ppvObject) \
    (This)->lpVtbl->QueryInterface(This,riid,ppvObject)

#define IEClabExe_AddRef(This) \
    (This)->lpVtbl->AddRef(This)

#define IEClabExe_Release(This) \
    (This)->lpVtbl->Release(This)

#define IEClabExe_ConnectDevice(This,DeviceNumber) \
    (This)->lpVtbl->ConnectDevice(This,DeviceNumber)

#define IEClabExe_DisconnectDevice(This,DeviceNumber) \
    (This)->lpVtbl->DisconnectDevice(This,DeviceNumber)

#define IEClabExe_TestConnection(This,DeviceNumber) \
    (This)->lpVtbl->TestConnection(This,DeviceNumber)

#define IEClabExe_LoadSettings(This,Device,Channel,FileName) \
    (This)->lpVtbl->LoadSettings(This,Device,Channel,FileName)

#define IEClabExe_RunChannel(This,Device,Channel,FileName) \
    (This)->lpVtbl->RunChannel(This,Device,Channel,FileName)

#define IEClabExe_StopChannel(This,Device,Channel) \
    (This)->lpVtbl->StopChannel(This,Device,Channel)

#define IEClabExe_MeasureStatus(This,Device,Channel,CurrentValues) \
    (This)->lpVtbl->MeasureStatus(This,Device,Channel,CurrentValues)

#define IEClabExe_MeasureDcValue(This,FileName,DataIndex,Data) \
    (This)->lpVtbl->MeasureDcValue(This,FileName,DataIndex,Data)

#define IEClabExe_MeasureEisValue(This,FileName,DataIndex,Data) \
    (This)->lpVtbl->MeasureEisValue(This,FileName,DataIndex,Data)

#define IEClabExe_MeasureNumberOfPoints(This,FileName) \
    (This)->lpVtbl->MeasureNumberOfPoints(This,FileName)

#endif /* ECLAB_OLECOM_INTERFACE_H */
