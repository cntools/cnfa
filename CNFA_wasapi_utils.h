#ifndef _CNFA_WASAPI_UTILS_H
#define _CNFA_WASAPI_UTILS_H

//#include "ole2.h"

#ifndef REFPROPERTYKEY
#define REFPROPERTYKEY const PROPERTYKEY * __MIDL_CONST
#endif //REFPROPERTYKEY

typedef enum __MIDL___MIDL_itf_mmdeviceapi_0000_0000_0001
{
    eRender	= 0,
    eCapture	= ( eRender + 1 ) ,
    eAll	= ( eCapture + 1 ) ,
    EDataFlow_enum_count	= ( eAll + 1 ) 
} EDataFlow;

typedef enum __MIDL___MIDL_itf_mmdeviceapi_0000_0000_0002
{
    eConsole	= 0,
    eMultimedia	= ( eConsole + 1 ) ,
    eCommunications	= ( eMultimedia + 1 ) ,
    ERole_enum_count	= ( eCommunications + 1 ) 
} ERole;

typedef struct IMMDeviceEnumeratorVtbl
{
    BEGIN_INTERFACE
    
    HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
        IMMDeviceEnumerator * This,
        /* [in] */ REFIID riid,
        /* [annotation][iid_is][out] */ 
        _COM_Outptr_  void **ppvObject);
    
    ULONG ( STDMETHODCALLTYPE *AddRef )( 
        IMMDeviceEnumerator * This);
    
    ULONG ( STDMETHODCALLTYPE *Release )( 
        IMMDeviceEnumerator * This);
    
    /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE *EnumAudioEndpoints )( 
        IMMDeviceEnumerator * This,
        /* [annotation][in] */ 
        _In_  EDataFlow dataFlow,
        /* [annotation][in] */ 
        _In_  DWORD dwStateMask,
        /* [annotation][out] */ 
        _Out_  IMMDeviceCollection **ppDevices);
    
    /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE *GetDefaultAudioEndpoint )( 
        IMMDeviceEnumerator * This,
        /* [annotation][in] */ 
        _In_  EDataFlow dataFlow,
        /* [annotation][in] */ 
        _In_  ERole role,
        /* [annotation][out] */ 
        _Out_  IMMDevice **ppEndpoint);
    
    /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE *GetDevice )( 
        IMMDeviceEnumerator * This,
        /* [annotation][in] */ 
        _In_  LPCWSTR pwstrId,
        /* [annotation][out] */ 
        _Out_  IMMDevice **ppDevice);
    
    /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE *RegisterEndpointNotificationCallback )( 
        IMMDeviceEnumerator * This,
        /* [annotation][in] */ 
        _In_  IMMNotificationClient *pClient);
    
    /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE *UnregisterEndpointNotificationCallback )( 
        IMMDeviceEnumerator * This,
        /* [annotation][in] */ 
        _In_  IMMNotificationClient *pClient);
    
    END_INTERFACE
} IMMDeviceEnumeratorVtbl;

interface IMMDeviceEnumerator
{
    CONST_VTBL struct IMMDeviceEnumeratorVtbl *lpVtbl;
};

typedef struct IMMDeviceCollectionVtbl
{
    BEGIN_INTERFACE
    
    HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
        IMMDeviceCollection * This,
        /* [in] */ REFIID riid,
        /* [annotation][iid_is][out] */ 
        _COM_Outptr_  void **ppvObject);
    
    ULONG ( STDMETHODCALLTYPE *AddRef )( 
        IMMDeviceCollection * This);
    
    ULONG ( STDMETHODCALLTYPE *Release )( 
        IMMDeviceCollection * This);
    
    /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE *GetCount )( 
        IMMDeviceCollection * This,
        /* [annotation][out] */ 
        _Out_  UINT *pcDevices);
    
    /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE *Item )( 
        IMMDeviceCollection * This,
        /* [annotation][in] */ 
        _In_  UINT nDevice,
        /* [annotation][out] */ 
        _Out_  IMMDevice **ppDevice);
    
    END_INTERFACE
} IMMDeviceCollectionVtbl;

interface IMMDeviceCollection
{
    CONST_VTBL struct IMMDeviceCollectionVtbl *lpVtbl;
};

typedef struct IMMDeviceVtbl
{
    BEGIN_INTERFACE
    
    HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
        IMMDevice * This,
        /* [in] */ REFIID riid,
        /* [annotation][iid_is][out] */ 
        _COM_Outptr_  void **ppvObject);
    
    ULONG ( STDMETHODCALLTYPE *AddRef )( 
        IMMDevice * This);
    
    ULONG ( STDMETHODCALLTYPE *Release )( 
        IMMDevice * This);
    
    /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE *Activate )( 
        IMMDevice * This,
        /* [annotation][in] */ 
        _In_  REFIID iid,
        /* [annotation][in] */ 
        _In_  DWORD dwClsCtx,
        /* [annotation][unique][in] */ 
        _In_opt_  PROPVARIANT *pActivationParams,
        /* [annotation][iid_is][out] */ 
        _Out_  void **ppInterface);
    
    /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE *OpenPropertyStore )( 
        IMMDevice * This,
        /* [annotation][in] */ 
        _In_  DWORD stgmAccess,
        /* [annotation][out] */ 
        _Out_  IPropertyStore **ppProperties);
    
    /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE *GetId )( 
        IMMDevice * This,
        /* [annotation][out] */ 
        _Outptr_  LPWSTR *ppstrId);
    
    /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE *GetState )( 
        IMMDevice * This,
        /* [annotation][out] */ 
        _Out_  DWORD *pdwState);
    
    END_INTERFACE
} IMMDeviceVtbl;

interface IMMDevice
{
    CONST_VTBL struct IMMDeviceVtbl *lpVtbl;
};

typedef struct IMMNotificationClientVtbl
{
    BEGIN_INTERFACE
    
    HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
        IMMNotificationClient * This,
        /* [in] */ REFIID riid,
        /* [annotation][iid_is][out] */ 
        _COM_Outptr_  void **ppvObject);
    
    ULONG ( STDMETHODCALLTYPE *AddRef )( 
        IMMNotificationClient * This);
    
    ULONG ( STDMETHODCALLTYPE *Release )( 
        IMMNotificationClient * This);
    
    /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE *OnDeviceStateChanged )( 
        IMMNotificationClient * This,
        /* [annotation][in] */ 
        _In_  LPCWSTR pwstrDeviceId,
        /* [annotation][in] */ 
        _In_  DWORD dwNewState);
    
    /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE *OnDeviceAdded )( 
        IMMNotificationClient * This,
        /* [annotation][in] */ 
        _In_  LPCWSTR pwstrDeviceId);
    
    /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE *OnDeviceRemoved )( 
        IMMNotificationClient * This,
        /* [annotation][in] */ 
        _In_  LPCWSTR pwstrDeviceId);
    
    /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE *OnDefaultDeviceChanged )( 
        IMMNotificationClient * This,
        /* [annotation][in] */ 
        _In_  EDataFlow flow,
        /* [annotation][in] */ 
        _In_  ERole role,
        /* [annotation][in] */ 
        _In_  LPCWSTR pwstrDefaultDeviceId);
    
    /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE *OnPropertyValueChanged )( 
        IMMNotificationClient * This,
        /* [annotation][in] */ 
        _In_  LPCWSTR pwstrDeviceId,
        /* [annotation][in] */ 
        _In_  const PROPERTYKEY key);
    
    END_INTERFACE
} IMMNotificationClientVtbl;

interface IMMNotificationClient
{
    CONST_VTBL struct IMMNotificationClientVtbl *lpVtbl;
};

typedef struct IPropertyStoreVtbl
{
    BEGIN_INTERFACE
    
    HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
        __RPC__in IPropertyStore * This,
        /* [in] */ __RPC__in REFIID riid,
        /* [annotation][iid_is][out] */ 
        _COM_Outptr_  void **ppvObject);
    
    ULONG ( STDMETHODCALLTYPE *AddRef )( 
        __RPC__in IPropertyStore * This);
    
    ULONG ( STDMETHODCALLTYPE *Release )( 
        __RPC__in IPropertyStore * This);
    
    HRESULT ( STDMETHODCALLTYPE *GetCount )( 
        __RPC__in IPropertyStore * This,
        /* [out] */ __RPC__out DWORD *cProps);
    
    HRESULT ( STDMETHODCALLTYPE *GetAt )( 
        __RPC__in IPropertyStore * This,
        /* [in] */ DWORD iProp,
        /* [out] */ __RPC__out PROPERTYKEY *pkey);
    
    HRESULT ( STDMETHODCALLTYPE *GetValue )( 
        __RPC__in IPropertyStore * This,
        /* [in] */ __RPC__in REFPROPERTYKEY key,
        /* [out] */ __RPC__out PROPVARIANT *pv);
    
    HRESULT ( STDMETHODCALLTYPE *SetValue )( 
        __RPC__in IPropertyStore * This,
        /* [in] */ __RPC__in REFPROPERTYKEY key,
        /* [in] */ __RPC__in REFPROPVARIANT propvar);
    
    HRESULT ( STDMETHODCALLTYPE *Commit )( 
        __RPC__in IPropertyStore * This);
    
    END_INTERFACE
} IPropertyStoreVtbl;

interface IPropertyStore
{
    CONST_VTBL struct IPropertyStoreVtbl *lpVtbl;
};

// ----- audioclient.h -----

typedef enum _AUDCLNT_SHAREMODE
{ 
    AUDCLNT_SHAREMODE_SHARED, 
    AUDCLNT_SHAREMODE_EXCLUSIVE 
} AUDCLNT_SHAREMODE;

typedef LONGLONG REFERENCE_TIME;

typedef struct IAudioClientVtbl
{
    BEGIN_INTERFACE
    
    HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
        IAudioClient * This,
        /* [in] */ REFIID riid,
        /* [annotation][iid_is][out] */ 
        _COM_Outptr_  void **ppvObject);
    
    ULONG ( STDMETHODCALLTYPE *AddRef )( 
        IAudioClient * This);
    
    ULONG ( STDMETHODCALLTYPE *Release )( 
        IAudioClient * This);
    
    HRESULT ( STDMETHODCALLTYPE *Initialize )( 
        IAudioClient * This,
        /* [annotation][in] */ 
        _In_  AUDCLNT_SHAREMODE ShareMode,
        /* [annotation][in] */ 
        _In_  DWORD StreamFlags,
        /* [annotation][in] */ 
        _In_  REFERENCE_TIME hnsBufferDuration,
        /* [annotation][in] */ 
        _In_  REFERENCE_TIME hnsPeriodicity,
        /* [annotation][in] */ 
        _In_  const WAVEFORMATEX *pFormat,
        /* [annotation][in] */ 
        _In_opt_  LPCGUID AudioSessionGuid);
    
    HRESULT ( STDMETHODCALLTYPE *GetBufferSize )( 
        IAudioClient * This,
        /* [annotation][out] */ 
        _Out_  UINT32 *pNumBufferFrames);
    
    HRESULT ( STDMETHODCALLTYPE *GetStreamLatency )( 
        IAudioClient * This,
        /* [annotation][out] */ 
        _Out_  REFERENCE_TIME *phnsLatency);
    
    HRESULT ( STDMETHODCALLTYPE *GetCurrentPadding )( 
        IAudioClient * This,
        /* [annotation][out] */ 
        _Out_  UINT32 *pNumPaddingFrames);
    
    HRESULT ( STDMETHODCALLTYPE *IsFormatSupported )( 
        IAudioClient * This,
        /* [annotation][in] */ 
        _In_  AUDCLNT_SHAREMODE ShareMode,
        /* [annotation][in] */ 
        _In_  const WAVEFORMATEX *pFormat,
        /* [unique][annotation][out] */ 
        _Out_opt_  WAVEFORMATEX **ppClosestMatch);
    
    HRESULT ( STDMETHODCALLTYPE *GetMixFormat )( 
        IAudioClient * This,
        /* [annotation][out] */ 
        _Out_  WAVEFORMATEX **ppDeviceFormat);
    
    HRESULT ( STDMETHODCALLTYPE *GetDevicePeriod )( 
        IAudioClient * This,
        /* [annotation][out] */ 
        _Out_opt_  REFERENCE_TIME *phnsDefaultDevicePeriod,
        /* [annotation][out] */ 
        _Out_opt_  REFERENCE_TIME *phnsMinimumDevicePeriod);
    
    HRESULT ( STDMETHODCALLTYPE *Start )( 
        IAudioClient * This);
    
    HRESULT ( STDMETHODCALLTYPE *Stop )( 
        IAudioClient * This);
    
    HRESULT ( STDMETHODCALLTYPE *Reset )( 
        IAudioClient * This);
    
    HRESULT ( STDMETHODCALLTYPE *SetEventHandle )( 
        IAudioClient * This,
        /* [in] */ HANDLE eventHandle);
    
    HRESULT ( STDMETHODCALLTYPE *GetService )( 
        IAudioClient * This,
        /* [annotation][in] */ 
        _In_  REFIID riid,
        /* [annotation][iid_is][out] */ 
        _Out_  void **ppv);
    
    END_INTERFACE
} IAudioClientVtbl;

interface IAudioClient
{
    CONST_VTBL struct IAudioClientVtbl *lpVtbl;
};

typedef struct IAudioCaptureClientVtbl
{
    BEGIN_INTERFACE
    
    HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
        IAudioCaptureClient * This,
        /* [in] */ REFIID riid,
        /* [annotation][iid_is][out] */ 
        _COM_Outptr_  void **ppvObject);
    
    ULONG ( STDMETHODCALLTYPE *AddRef )( 
        IAudioCaptureClient * This);
    
    ULONG ( STDMETHODCALLTYPE *Release )( 
        IAudioCaptureClient * This);
    
    HRESULT ( STDMETHODCALLTYPE *GetBuffer )( 
        IAudioCaptureClient * This,
        /* [annotation][out] */ 
        _Outptr_result_buffer_(_Inexpressible_("*pNumFramesToRead * pFormat->nBlockAlign"))  BYTE **ppData,
        /* [annotation][out] */ 
        _Out_  UINT32 *pNumFramesToRead,
        /* [annotation][out] */ 
        _Out_  DWORD *pdwFlags,
        /* [annotation][unique][out] */ 
        _Out_opt_  UINT64 *pu64DevicePosition,
        /* [annotation][unique][out] */ 
        _Out_opt_  UINT64 *pu64QPCPosition);
    
    HRESULT ( STDMETHODCALLTYPE *ReleaseBuffer )( 
        IAudioCaptureClient * This,
        /* [annotation][in] */ 
        _In_  UINT32 NumFramesRead);
    
    HRESULT ( STDMETHODCALLTYPE *GetNextPacketSize )( 
        IAudioCaptureClient * This,
        /* [annotation][out] */ 
        _Out_  UINT32 *pNumFramesInNextPacket);
    
    END_INTERFACE
} IAudioCaptureClientVtbl;

interface IAudioCaptureClient
{
    CONST_VTBL struct IAudioCaptureClientVtbl *lpVtbl;
};

#endif // _CNFA_WASAPI_UTILS_H