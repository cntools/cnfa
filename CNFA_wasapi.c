#include "CNFA.h"

//Needed libraries:  -lmmdevapi -lole32
//Or DLLs: C:/windows/system32/ole32.dll

#ifdef TCC
#define NO_WIN_HEADERS
#endif

#ifdef  NO_WIN_HEADERS
#include "CNFA_wasapi_utils.h"
#else
#include <InitGuid.h>
#include <audioclient.h> // Render and capturing audio
#include <audiopolicy.h> // Setting name of session
#include <mmdeviceapi.h> // Audio device handling
#include <Functiondiscoverykeys_devpkey.h> // Property keys for audio devices
#include "windows.h"
#endif

#include "stdio.h"
#include "os_generic.h"

#if defined(WIN32) && !defined( TCC )
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"mmdevapi.lib")
#endif

#define WASAPIPRINT(message) (printf("[CNFA][WASAPI]: %s\n", message))
#define WASAPIERROR(error, message) (printf("[CNFA][WASAPI][ERR]: %s HRESULT: 0x%lX\n", message, error))
#define PRINTGUID(guid) (printf("{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", guid.Data1, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]))

#define WASAPI_EXTRA_DEBUG FALSE

// Forward declarations
void CloseCNFAWASAPI(void* stateObj);
int CNFAStateWASAPI(void* object);
static struct CNFADriverWASAPI* StartWASAPIDriver(struct CNFADriverWASAPI* initState);
static BYTE FindInDevice(void);
static void FindOutDevice(void);
static IMMDevice* WASAPIGetDefaultDevice(BOOL isCapture, BOOL isMultimedia);
static void WASAPIPrintAllDeviceLists(void);
static void WASAPIPrintDeviceList(EDataFlow dataFlow);
static void StartClient(BOOL isIn, UINT32 streamFlags);
void* ProcessEventAudioIn(void* stateObj);
void* ProcessEventAudioOut(void* stateObj);
void* InitCNFAWASAPIDriver(
	CNFACBType callback, const char *session_name,
	int reqSampleRateOut, int reqSampleRateIn,
	int reqChannelsOut, int reqChannelsIn, int sugBufferSize,
	const char * inputDevice, const char * outputDevice,
	void * opaque
);

DEFINE_GUID(CLSID_MMDeviceEnumerator,    0xBCDE0395L, 0xE52F, 0x467C, 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E);
DEFINE_GUID(IID_IMMDeviceEnumerator,     0xA95664D2L, 0x9614, 0x4F35, 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6);
DEFINE_GUID(IID_IMMEndpoint,             0x1BE09788L, 0x6894, 0x4089, 0x85, 0x86, 0x9A, 0x2A, 0x6C, 0x26, 0x5A, 0xC5);
DEFINE_GUID(IID_IAudioClient,            0x1CB9AD4CL, 0xDBFA, 0x4c32, 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2);
DEFINE_GUID(IID_IAudioCaptureClient,     0xC8ADBD64L, 0xE71E, 0x48a0, 0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17);
DEFINE_GUID(IID_IAudioRenderClient,      0xF294ACFCL, 0x3146, 0x4483, 0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2);
DEFINE_GUID(IID_IAudioSessionControl,    0xF4B1A599L, 0x7266, 0x4319, 0xA8, 0xCA, 0xE7, 0x0A, 0xCB, 0x11, 0xE8, 0xCD);

// This is a fallback if the client application does not provide a GUID.
DEFINE_GUID(CNFA_GUID, 0x899081C7L, 0x9428, 0x4103, 0x87, 0x93, 0x26, 0x47, 0xE5, 0xEA, 0xA2, 0xB4);

struct CNFADriverWASAPI
{
	// Common CNFA items
    void (*CloseFn)(void* object);
	int (*StateFn)(void* object);
	CNFACBType Callback;
	short ChannelCountOut;
	short ChannelCountIn;
	int SampleRateOut;
	int SampleRateIn;
	void* Opaque; // Not relevant to us
	const char* SessionName; // The name to give our audio sessions. Otherwise, defaults to using embedded EXE name, Window title, or EXE file name directly.

	// Adjustable WASAPI-specific items
	const GUID* SessionID; // In order to have different CNFA-based applications individually controllable from the volume mixer, this should be set differently for every client program, but constant across all runs/builds of that application.

	// Everything below here is for internal use only. Do not attempt to interact with these items.
	const char* OutputDeviceID; // The device to use for sending output to. Can only be a render device.
	const char* InputDeviceID; // The device to use for getting input from. Can be a render device (operating in loopback), or a capture device.
	IMMDeviceEnumerator* DeviceEnumerator; // The base object that allows us to look through the system's devices, and from there get everything else.
	IMMDevice* DeviceOut; // The device we are sending output to.
	IMMDevice* DeviceIn; // The device we are taking input from.
	IAudioClient* ClientOut; // The base client we use for sending output.
	IAudioClient* ClientIn; // The base client we use for getting input.
	IAudioRenderClient* RenderClient; // The specific client we use for sending output.
	IAudioCaptureClient* CaptureClient; // The specific client we use for getting input.
	BOOL StreamOutReady; // Whether the output stream is ready for data submission.
	BOOL StreamInReady; // Whether the input stream is ready for data retrieval.
	BOOL KeepGoing; // Whether to continue interacting with the streams, or shutdown the driver.
	og_thread_t ThreadOut; // The thread used to send output data.
	og_thread_t ThreadIn; // The thread used to grab input data.
	HANDLE EventHandleOut; // The handle used to wait for the system to be ready for more output data in the output thread.
	HANDLE EventHandleIn; // The handle used to wait for more input data to be ready in the input thread.
};

// This is where the driver's current state is stored.
static struct CNFADriverWASAPI* WASAPIState;

// Stops streams, ends threads, and cleans up all resources used by the driver.
void CloseCNFAWASAPI(void* stateObj)
{
	struct CNFADriverWASAPI* state = (struct CNFADriverWASAPI*)stateObj;
	if (state != NULL)
	{
		state->KeepGoing = FALSE;
		if (state->ThreadOut != NULL) { OGJoinThread(state->ThreadOut); }
		if (state->ThreadIn != NULL) { OGJoinThread(state->ThreadIn); }
		if (state->EventHandleOut != NULL) { CloseHandle(state->EventHandleOut); }
		if (state->EventHandleIn != NULL) { CloseHandle(state->EventHandleIn); }
		if (state->RenderClient != NULL) { state->RenderClient->lpVtbl->Release(state->RenderClient); }
		if (state->CaptureClient != NULL) { state->CaptureClient->lpVtbl->Release(state->CaptureClient); }
		if (state->ClientOut != NULL) { state->ClientOut->lpVtbl->Release(state->ClientOut); }
		if (state->ClientIn != NULL) { state->ClientIn->lpVtbl->Release(state->ClientIn); }
		if (state->DeviceOut != NULL) { state->DeviceIn->lpVtbl->Release(state->DeviceOut); }
		if (state->DeviceIn != NULL) { state->DeviceIn->lpVtbl->Release(state->DeviceIn); }
		if (state->DeviceEnumerator != NULL) { state->DeviceEnumerator->lpVtbl->Release(state->DeviceEnumerator); }
		free(stateObj);

		#ifndef BUILD_DLL
		CoUninitialize();
		#endif

		puts("[CNFA][WASAPI]: Cleanup completed. Goodbye.\n");
	}
}

// Gets the current state of the driver.
// 0 = No streams active
// 1 = Input stream active
// 2 = Output stream active
// 3 = Both streams active
int CNFAStateWASAPI(void* stateObj)
{
	struct CNFADriverWASAPI* state = (struct CNFADriverWASAPI*)stateObj;
	if (state != NULL) { return ((state->StreamInReady) ? 1 : 0) | ((state->StreamOutReady) ? 2 : 0); }
	return 0;
}

// Reads the desired configuration, interfaces with WASAPI to get the current system information, and starts the input stream.
static struct CNFADriverWASAPI* StartWASAPIDriver(struct CNFADriverWASAPI* initState)
{
	WASAPIState = initState;
	WASAPIState->StreamInReady = FALSE;
	WASAPIState->StreamOutReady = FALSE;
	WASAPIState->SessionID = &CNFA_GUID;

	HRESULT ErrorCode;
	#ifndef BUILD_DLL
	// A library should never call CoInitialize, as it needs to be done from the host program according to its threading model needs.
	// NOTE: If you are getting errors, and you are using CNFA as a DLL, you need to call CoInitialize yourself with an appropriate threading model for your needs!
	// When the host program is something like ColorChord on the other hand, it cannot be expected to call CoInitialize itself, so we do it on its behalf.
	//   This restricts the threading model of direct consumers of CNFA, but we can address that if it does ever become an issue.
	ErrorCode = CoInitialize(NULL);
	if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "COM INIT FAILED!"); return WASAPIState; }
	#endif

	if (WASAPI_EXTRA_DEBUG)
	{
		puts("[CNFA][WASAPI]: CLSID for MMDeviceEnumerator: ");
		PRINTGUID(CLSID_MMDeviceEnumerator);
		puts("\n[CNFA][WASAPI]: IID for IMMDeviceEnumerator: ");
		PRINTGUID(IID_IMMDeviceEnumerator);
		puts("\n[CNFA][WASAPI]: IID for IAudioClient: ");
		PRINTGUID(IID_IAudioClient);
		puts("\n[CNFA][WASAPI]: IID for IAudioCaptureClient: ");
		PRINTGUID(IID_IAudioCaptureClient);
		puts("\n[CNFA][WASAPI]: IID for IAudioRenderClient: ");
		PRINTGUID(IID_IAudioRenderClient);
		puts("\n[CNFA][WASAPI]: IID for IAudioSessionControl: ");
		PRINTGUID(IID_IAudioSessionControl);
		puts("\n[CNFA][WASAPI]: IID for IMMEndpoint: ");
		PRINTGUID(IID_IMMEndpoint);
		puts("\n");
	}

	ErrorCode = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void**)&(WASAPIState->DeviceEnumerator));
	if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get device enumerator. "); return WASAPIState; }

	WASAPIPrintAllDeviceLists();

	BYTE DeviceDirectionIn = FindInDevice(); // This populates WASAPIState->DeviceIn
	char* DeviceDirectionDesc = (DeviceDirectionIn == 0) ? "render" : ((DeviceDirectionIn == 1) ? "capture" : "UNKNOWN");

	LPWSTR DeviceID;
	ErrorCode = WASAPIState->DeviceIn->lpVtbl->GetId(WASAPIState->DeviceIn, &DeviceID);
	if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get input audio device ID."); return WASAPIState; }
	else { printf("[CNFA][WASAPI]: Using device ID \"%ls\" for input, which is a %s device.\n", DeviceID, DeviceDirectionDesc); }

	FindOutDevice(); // This populates WASAPIState->DeviceOut

	// Start audio clients
	if (WASAPIState->ChannelCountIn > 0)
	{
		if (DeviceDirectionIn == 2) { WASAPIPRINT("[ERR] Device type was not determined!"); return WASAPIState; }

		UINT32 StreamFlagsIn = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
		if (DeviceDirectionIn == 0) { StreamFlagsIn |= AUDCLNT_STREAMFLAGS_LOOPBACK; }
		StartClient(TRUE, StreamFlagsIn);
		WASAPIState->StreamInReady = TRUE;
	}
	if (WASAPIState->ChannelCountOut > 0)
	{
		StartClient(FALSE, (AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY));
		WASAPIState->StreamOutReady = TRUE;
	}

	WASAPIState->KeepGoing = TRUE;
	if (WASAPIState->ChannelCountIn > 0) { WASAPIState->ThreadIn = OGCreateThread(ProcessEventAudioIn, WASAPIState); }
	if (WASAPIState->ChannelCountOut > 0) { WASAPIState->ThreadOut = OGCreateThread(ProcessEventAudioOut, WASAPIState); }

	return WASAPIState;
}

static BYTE FindInDevice(void)
{
	// We need to find the appropriate device to use.
	BYTE DeviceDirectionIn = 2; // 0 = Render, 1 = Capture, 2 = Unknown

	HRESULT ErrorCode;
	if (WASAPIState->InputDeviceID == NULL)
	{
		WASAPIPRINT("No input device specified, attempting to use system default multimedia capture device as input.");
		WASAPIState->DeviceIn = WASAPIGetDefaultDevice(TRUE, TRUE);
		DeviceDirectionIn = 1;
	}
	else if (strcmp(WASAPIState->InputDeviceID, "defaultRender") == 0)
	{
		WASAPIPRINT("Attempting to use system default render device as input.");
		WASAPIState->DeviceIn = WASAPIGetDefaultDevice(FALSE, TRUE);
		DeviceDirectionIn = 0;
	}
	else if (strncmp("defaultCapture", WASAPIState->InputDeviceID, strlen("defaultCapture")) == 0)
	{
		BOOL IsMultimedia = TRUE;
		if (strstr(WASAPIState->InputDeviceID, "Comm") != NULL) { IsMultimedia = FALSE; }
		printf("[CNFA][WASAPI]: Attempting to use system default %s capture device as input.\n", (IsMultimedia ? "multimedia" : "communications"));
		WASAPIState->DeviceIn = WASAPIGetDefaultDevice(TRUE, IsMultimedia);
		DeviceDirectionIn = 1;
	}
	else // A specific device was selected by ID.
	{
		LPWSTR DeviceIDasLPWSTR;
		size_t DeviceIDasLPWSTR_LenWords = strlen(WASAPIState->InputDeviceID) + 1;
		DeviceIDasLPWSTR = malloc(DeviceIDasLPWSTR_LenWords * sizeof(WCHAR));
		size_t CharsConverted;
		mbstowcs_s(&CharsConverted, DeviceIDasLPWSTR, DeviceIDasLPWSTR_LenWords, WASAPIState->InputDeviceID, DeviceIDasLPWSTR_LenWords - 1);
		printf("[CNFA][WASAPI]: Attempting to find specified device \"%ls\".\n", DeviceIDasLPWSTR);

		ErrorCode = WASAPIState->DeviceEnumerator->lpVtbl->GetDevice(WASAPIState->DeviceEnumerator, DeviceIDasLPWSTR, &(WASAPIState->DeviceIn));
		if (FAILED(ErrorCode))
		{
			WASAPIERROR(ErrorCode, "Failed to get audio device from the given ID. Using default multimedia capture device instead.");
			WASAPIState->DeviceIn = WASAPIGetDefaultDevice(TRUE, TRUE);
			DeviceDirectionIn = 1;
		}
		else
		{
			puts("[CNFA][WASAPI]: Found specified input device.\n");
			DWORD DeviceState;
			ErrorCode = WASAPIState->DeviceIn->lpVtbl->GetState(WASAPIState->DeviceIn, &DeviceState);
			if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get device state."); }

			if ((DeviceState & DEVICE_STATE_DISABLED) == DEVICE_STATE_DISABLED) { WASAPIERROR(E_FAIL, "The specified device is currently disabled."); }
			if ((DeviceState & DEVICE_STATE_NOTPRESENT) == DEVICE_STATE_NOTPRESENT) { WASAPIERROR(E_FAIL, "The specified device is not currently present."); }
			if ((DeviceState & DEVICE_STATE_UNPLUGGED) == DEVICE_STATE_UNPLUGGED) { WASAPIERROR(E_FAIL, "The specified device is currently unplugged."); }
		}
	}

	if (DeviceDirectionIn == 2) // We still don't know what type of device we are trying to use. Query the endpoint to find out.
	{
		IMMEndpoint* Endpoint;
		ErrorCode = WASAPIState->DeviceIn->lpVtbl->QueryInterface(WASAPIState->DeviceIn, &IID_IMMEndpoint, (void**)&Endpoint);
		if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get endpoint of device."); }

		EDataFlow DataFlow;
		ErrorCode = Endpoint->lpVtbl->GetDataFlow(Endpoint, &DataFlow);
		if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Could not determine endpoint type."); }

		DeviceDirectionIn = (DataFlow == eRender) ? 0 : 1;

		if (Endpoint != NULL) { Endpoint->lpVtbl->Release(Endpoint); }
	}

	return DeviceDirectionIn;
}

static void FindOutDevice(void)
{
	HRESULT ErrorCode;
	if (WASAPIState->OutputDeviceID == NULL)
	{
		WASAPIPRINT("No output device specified, attempting to use system default multimedia render device as output.");
		WASAPIState->DeviceOut = WASAPIGetDefaultDevice(FALSE, TRUE);
	}
	else if (strncmp("defaultRender", WASAPIState->OutputDeviceID, strlen("defaultRender")) == 0)
	{
		BOOL IsMultimedia = TRUE;
		if (strstr(WASAPIState->OutputDeviceID, "Comm") != NULL) { IsMultimedia = FALSE; }
		printf("[CNFA][WASAPI]: Attempting to use system default %s render device as output.\n", (IsMultimedia ? "multimedia" : "communications"));
		WASAPIState->DeviceOut = WASAPIGetDefaultDevice(FALSE, IsMultimedia);
	}
	else // A specific device was selected by ID.
	{
		LPWSTR DeviceIDasLPWSTR;
		size_t DeviceIDasLPWSTR_LenWords = strlen(WASAPIState->OutputDeviceID) + 1;
		DeviceIDasLPWSTR = malloc(DeviceIDasLPWSTR_LenWords * sizeof(WCHAR));
		size_t CharsConverted;
		mbstowcs_s(&CharsConverted, DeviceIDasLPWSTR, DeviceIDasLPWSTR_LenWords, WASAPIState->OutputDeviceID, DeviceIDasLPWSTR_LenWords - 1);
		printf("[CNFA][WASAPI]: Attempting to find specified device \"%ls\".\n", DeviceIDasLPWSTR);

		ErrorCode = WASAPIState->DeviceEnumerator->lpVtbl->GetDevice(WASAPIState->DeviceEnumerator, DeviceIDasLPWSTR, &(WASAPIState->DeviceOut));
		if (FAILED(ErrorCode))
		{
			WASAPIERROR(ErrorCode, "Failed to get audio device from the given ID. Using default multimedia render device instead.");
			WASAPIState->DeviceOut = WASAPIGetDefaultDevice(FALSE, TRUE);
		}
		else
		{
			puts("[CNFA][WASAPI]: Found specified output device.\n");
			DWORD DeviceState;
			ErrorCode = WASAPIState->DeviceOut->lpVtbl->GetState(WASAPIState->DeviceOut, &DeviceState);
			if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get device state."); }

			if ((DeviceState & DEVICE_STATE_DISABLED) == DEVICE_STATE_DISABLED) { WASAPIERROR(E_FAIL, "The specified device is currently disabled."); }
			if ((DeviceState & DEVICE_STATE_NOTPRESENT) == DEVICE_STATE_NOTPRESENT) { WASAPIERROR(E_FAIL, "The specified device is not currently present."); }
			if ((DeviceState & DEVICE_STATE_UNPLUGGED) == DEVICE_STATE_UNPLUGGED) { WASAPIERROR(E_FAIL, "The specified device is currently unplugged."); }
		}
	}
}

static void StartClient(BOOL isIn, UINT32 streamFlags)
{
	IMMDevice* Device = isIn ? WASAPIState->DeviceIn : WASAPIState->DeviceOut;
	IAudioClient* Client;

	HRESULT ErrorCode;
	ErrorCode = Device->lpVtbl->Activate(Device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&(Client));
	if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get audio client. "); return; }

	// We'll request a PCM, 16b-sample data interface with the system. It should be able to do any conversion for us, as long as we are not in exclusive mode.
	short ChannelCount = (isIn ? WASAPIState->ChannelCountIn : WASAPIState->ChannelCountOut);
	int SampleRate = (isIn ? WASAPIState->SampleRateIn : WASAPIState->SampleRateOut);
	WAVEFORMATEX Format =
	{
		.wFormatTag = WAVE_FORMAT_PCM,
		.wBitsPerSample = 16,
		.nBlockAlign = ChannelCount * 2,
		.nAvgBytesPerSec = SampleRate * ChannelCount * 2,
		.nChannels = ChannelCount,
		.nSamplesPerSec = SampleRate,
		.cbSize = 0
	};

	REFERENCE_TIME DefaultInterval, MinimumInterval;
	ErrorCode = Client->lpVtbl->GetDevicePeriod(Client, &DefaultInterval, &MinimumInterval);
	if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get device timing info. "); return; }
	printf("[CNFA][WASAPI]: Default transaction period is %lld ticks, minimum is %lld ticks.\n", DefaultInterval, MinimumInterval);

	// Configure a capture client.
	// TODO: Allow the target application to influence the interval we choose. Super realtime apps may require MinimumInterval.
	ErrorCode = Client->lpVtbl->Initialize(Client, AUDCLNT_SHAREMODE_SHARED, streamFlags, DefaultInterval, 0, &Format, isIn ? NULL : WASAPIState->SessionID);
	if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Could not init audio client."); return; }

	HANDLE EventHandle = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (EventHandle == NULL) { WASAPIERROR(E_FAIL, "Failed to make event handle."); return; }

	ErrorCode = Client->lpVtbl->SetEventHandle(Client, EventHandle);
	if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to set event handler."); return; }

	UINT32 BufferFrameCount;
	ErrorCode = Client->lpVtbl->GetBufferSize(Client, &BufferFrameCount);
	if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Could not get audio client buffer size."); return; }

	if (isIn)
	{
		WASAPIState->ClientIn = Client;
		WASAPIState->EventHandleIn = EventHandle;

		ErrorCode = Client->lpVtbl->GetService(Client, &IID_IAudioCaptureClient, (void**)&(WASAPIState->CaptureClient));
		if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Could not get audio capture client."); return; }
	}
	else
	{
		WASAPIState->ClientOut = Client;
		WASAPIState->EventHandleOut = EventHandle;

		ErrorCode = Client->lpVtbl->GetService(Client, &IID_IAudioRenderClient, (void**)&(WASAPIState->RenderClient));
		if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Could not get audio render client."); return; }

		IAudioSessionControl* Session = NULL;
		ErrorCode = Client->lpVtbl->GetService(Client, &IID_IAudioSessionControl, (void**)&Session);
		if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Could not get audio session control."); }
		else
		{
			LPWSTR SessionNameasLPWSTR;
			size_t SessionNameasLPWSTR_LenWords = strlen(WASAPIState->SessionName) + 1;
			SessionNameasLPWSTR = malloc(SessionNameasLPWSTR_LenWords * sizeof(WCHAR));
			size_t CharsConverted;
			mbstowcs_s(&CharsConverted, SessionNameasLPWSTR, SessionNameasLPWSTR_LenWords, WASAPIState->SessionName, SessionNameasLPWSTR_LenWords - 1);

			ErrorCode = Session->lpVtbl->SetDisplayName(Session, SessionNameasLPWSTR, NULL);
			if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Could not set audio session name."); }
		}
		if (Session != NULL) { Session->lpVtbl->Release(Session); }
	}

	// Begin capturing/sending audio. This is handled on a separate thread.
	ErrorCode = Client->lpVtbl->Start(Client);
	if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Could not start audio client."); return; }
}

// Gets the default render or capture device.
// isCapture: If true, gets the default capture device, otherwise gets the default render device.
// isMultimedia: If true, gets the system default devide for "multimedia" use, otheriwse for "communication" use.
static IMMDevice* WASAPIGetDefaultDevice(BOOL isCapture, BOOL isMultimedia)
{
	HRESULT ErrorCode;
	IMMDevice* Device;
	ErrorCode = WASAPIState->DeviceEnumerator->lpVtbl->GetDefaultAudioEndpoint(WASAPIState->DeviceEnumerator, isCapture ? eCapture : eRender, isMultimedia ? eMultimedia : eCommunications, &Device);
	if (FAILED(ErrorCode))
	{
		WASAPIERROR(ErrorCode, "Failed to get default device.");
		return NULL;
	}
	return Device;
}

// Prints all available devices to the console.
static void WASAPIPrintAllDeviceLists(void)
{
	WASAPIPrintDeviceList(eRender);
	WASAPIPrintDeviceList(eCapture);
}

// Prints a list of all available devices of a specified data flow direction to the console.
static void WASAPIPrintDeviceList(EDataFlow dataFlow)
{
	printf("[CNFA][WASAPI]: %s Devices:\n", (dataFlow == eCapture ? "Capture" : "Render"));
	IMMDeviceCollection* Devices;
	HRESULT ErrorCode = WASAPIState->DeviceEnumerator->lpVtbl->EnumAudioEndpoints(WASAPIState->DeviceEnumerator, dataFlow, (WASAPI_EXTRA_DEBUG ? DEVICE_STATEMASK_ALL : DEVICE_STATE_ACTIVE), &Devices);
	if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get audio endpoints."); return; }

	UINT32 DeviceCount;
	ErrorCode = Devices->lpVtbl->GetCount(Devices, &DeviceCount);
	if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get audio endpoint count."); return; }

	for (UINT32 DeviceIndex = 0; DeviceIndex < DeviceCount; DeviceIndex++)
	{
		IMMDevice* Device;
		ErrorCode = Devices->lpVtbl->Item(Devices, DeviceIndex, &Device);
		if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get audio device."); continue; }

		LPWSTR DeviceID;
		ErrorCode = Device->lpVtbl->GetId(Device, &DeviceID);
		if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get audio device ID."); continue; }

		IPropertyStore* Properties;
		ErrorCode = Device->lpVtbl->OpenPropertyStore(Device, STGM_READ, &Properties);
		if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get device properties."); continue; }
		
		PROPVARIANT Variant;
		PropVariantInit(&Variant);

		ErrorCode = Properties->lpVtbl->GetValue(Properties, &PKEY_Device_FriendlyName, &Variant);
		if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get device friendly name."); }

		LPWSTR DeviceFriendlyName = L"[Name Retrieval Failed]";
		if (Variant.pwszVal != NULL) { DeviceFriendlyName = Variant.pwszVal; }

		wprintf(L"[CNFA][WASAPI]: [%d]: \"%ls\" = \"%ls\"\n", DeviceIndex, DeviceFriendlyName, DeviceID); // TODO: This doesn't print non-ASCII device names correctly
		
		CoTaskMemFree(DeviceID);
		DeviceID = NULL;
		PropVariantClear(&Variant);
		if (Properties != NULL) { Properties->lpVtbl->Release(Properties); }
		if (Device != NULL) { Device->lpVtbl->Release(Device); }
	}

	if (Devices != NULL) { Devices->lpVtbl->Release(Devices); }
}

// Runs on a thread. Waits for audio data to be ready from the system, then forwards it to the registered callback.
void* ProcessEventAudioIn(void* stateObj)
{
	struct CNFADriverWASAPI* state = (struct CNFADriverWASAPI*)stateObj;
	HRESULT ErrorCode;
	UINT32 PacketLength;

	INT16* SilenceBuffer = NULL;
	UINT32 SilenceBufferLen = 0;

	while (state->KeepGoing)
	{
		// Waits up to 500ms to get the next audio buffer from the system.
		// The timeout is used because if no audio sessions are active, WASAPI stops sending buffers after a few that indicate silence.
		// This means that if the client tries to exit, this loop would not complete, and therefore the thread would not exit, until the next buffer is received.
		// This is mostly an issue in loopback mode, where true silence is common, not so much on microphones.
		DWORD WaitResult = WaitForSingleObject(state->EventHandleIn, 500);
		if (WaitResult == WAIT_TIMEOUT) { continue; } // We are in a period of silence. Keep waiting for audio.
		else if (WaitResult != WAIT_OBJECT_0) { WASAPIERROR(E_FAIL, "Something went wrong while waiting for an audio event."); continue; }

		ErrorCode = state->CaptureClient->lpVtbl->GetNextPacketSize(state->CaptureClient, &PacketLength);
		if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get audio packet size."); continue; }

		BYTE* DataBuffer;
		UINT32 FramesAvailable;
		DWORD BufferStatus;
		ErrorCode = state->CaptureClient->lpVtbl->GetBuffer(state->CaptureClient, &DataBuffer, &FramesAvailable, &BufferStatus, NULL, NULL);
		if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get audio buffer."); continue; }

		if ((BufferStatus & AUDCLNT_BUFFERFLAGS_SILENT) == AUDCLNT_BUFFERFLAGS_SILENT)
		{
			UINT32 Length = FramesAvailable * state->ChannelCountIn;
			if (Length == 0) { Length = state->ChannelCountIn; }
			if (Length != SilenceBufferLen)
			{
				SilenceBuffer = malloc(Length * sizeof(INT16));
				for (UINT32 i = 0; i < Length; i++) { SilenceBuffer[i] = 0; }
				SilenceBufferLen = Length;
			}

			ErrorCode = state->CaptureClient->lpVtbl->ReleaseBuffer(state->CaptureClient, FramesAvailable);
			if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to release audio buffer."); }

			if (WASAPI_EXTRA_DEBUG) { printf("[CNFA][WASAPI]: SILENCE buffer received. Passing on %d samples.\n", Length); }

			WASAPIState->Callback((struct CNFADriver*)WASAPIState, 0, SilenceBuffer, 0, SilenceBufferLen / state->ChannelCountIn);
		}
		else
		{
			ErrorCode = state->CaptureClient->lpVtbl->ReleaseBuffer(state->CaptureClient, FramesAvailable);
			if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to release audio buffer."); }

			if (WASAPI_EXTRA_DEBUG) { printf("[CNFA][WASAPI]: Got %d frames of audio data. Fowarding to %p.\n", FramesAvailable, (void*) WASAPIState->Callback); }

			WASAPIState->Callback((struct CNFADriver*)WASAPIState, 0, (short*)DataBuffer, 0, FramesAvailable );
		}
	}

	ErrorCode = state->ClientIn->lpVtbl->Stop(state->ClientIn);
	if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to stop audio client."); }
	free(SilenceBuffer);

	state->StreamInReady = FALSE;
	return 0;
}

// Runs on a thread. Waits for the system to be ready for audio data, then gets it from the registered callback.
void* ProcessEventAudioOut(void* stateObj)
{
	struct CNFADriverWASAPI* state = (struct CNFADriverWASAPI*)stateObj;
	HRESULT ErrorCode;
	UINT32 BufferSize;
	UINT32 CurrentPadding;

	ErrorCode = state->ClientOut->lpVtbl->GetBufferSize(state->ClientOut, &BufferSize);
	if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get audio buffer size."); return NULL; }

	while (state->KeepGoing)
	{
		DWORD WaitResult = WaitForSingleObject(state->EventHandleOut, 500);
		if (WaitResult == WAIT_TIMEOUT) { continue; } // Keep waiting for the system to be ready.
		else if (WaitResult != WAIT_OBJECT_0) { WASAPIERROR(E_FAIL, "Something went wrong while waiting for an audio event."); continue; }

		ErrorCode = state->ClientOut->lpVtbl->GetCurrentPadding(state->ClientOut, &CurrentPadding);
		if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get current padding."); continue; }

		UINT32 FramesToWrite = BufferSize - CurrentPadding;
		BYTE* DataBuffer;
		ErrorCode = state->RenderClient->lpVtbl->GetBuffer(state->RenderClient, FramesToWrite, &DataBuffer);
		if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get audio buffer."); continue; }

		WASAPIState->Callback((struct CNFADriver*)WASAPIState, (short*)DataBuffer, NULL, FramesToWrite, 0);

		ErrorCode = state->RenderClient->lpVtbl->ReleaseBuffer(state->RenderClient, FramesToWrite, 0);
		if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to submit audio buffer."); continue; }
	}

	ErrorCode = state->ClientOut->lpVtbl->Stop(state->ClientOut);
	if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to stop audio client."); }

	state->StreamOutReady = FALSE;
	return NULL;
}

// Begins preparation of the WASAPI driver.
// callback: The user application's function where audio data is placed when received from the system and/or audio data is retrieved from to give to the system.
// sessionName: How your session will appear to the end user if you play audio.
// reqSampleRateIn/Out: Sample rate you'd like to request.
// reqChannelsIn/Out: Channel count you'd like to request. Set to 0 to disable that flow direction.
// sugBufferSize: Buffer size you'd like to request. Ignored, as this is determined by the system.
// inputDevice: The device you want to receive audio from. Loopback is supported, so this can be either a capture or render device.
//              To get the default multimedia capture device, specify "defaultCapture" or NULL.
//              To get the default communications capture device, specify "defaultCaptureComm"
//              To get the default render device, specify "defaultRender"
//              A device ID as presented by WASAPI can be specified, regardless of what type it is. If it is invalid, the default capture device is used as fallback.
// outputDevice: The device you want to output audio to. Only render devices are supported.
//               To get the default multimedia render device, specify "defaultRender" or NULL.
//               To get the default communications render device, specify "defaultRenderComm"
//               A device ID as presented by WASAPI can be specified. If it is invalid, the default render device is used as fallback.
void* InitCNFAWASAPIDriver(
	CNFACBType callback, const char *sessionName,
	int reqSampleRateOut, int reqSampleRateIn,
	int reqChannelsOut, int reqChannelsIn, int sugBufferSize,
	const char * outputDevice, const char * inputDevice,
	void * opaque)
{
	struct CNFADriverWASAPI * InitState = malloc(sizeof(struct CNFADriverWASAPI));
	memset(InitState, 0, sizeof(*InitState));
	InitState->CloseFn = CloseCNFAWASAPI;
	InitState->StateFn = CNFAStateWASAPI;
	InitState->Callback = callback;
	InitState->Opaque = opaque;
	InitState->SampleRateIn = reqSampleRateIn;
	InitState->SampleRateOut = reqSampleRateOut;
	InitState->ChannelCountIn = reqChannelsIn;
	InitState->ChannelCountOut = reqChannelsOut;
	InitState->InputDeviceID = inputDevice;
	InitState->OutputDeviceID = outputDevice;

	InitState->SessionName = sessionName;

	WASAPIPRINT("WASAPI Init");

	return StartWASAPIDriver(InitState);
}

REGISTER_CNFA(cnfa_wasapi, 20, "WASAPI", InitCNFAWASAPIDriver)
