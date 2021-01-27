#include "CNFA.h"

//Needed libraries:  -lmmdevapi -lavrt -lole32
//Or DLLs: C:/windows/system32/avrt.dll C:/windows/system32/ole32.dll

#ifdef TCC
#define NO_WIN_HEADERS
#endif

#ifdef  NO_WIN_HEADERS
#include "CNFA_wasapi_utils.h"
#else
#include <InitGuid.h>
#include <audioclient.h> // Render and capturing audio
#include <mmdeviceapi.h> // Audio device handling
#include <Functiondiscoverykeys_devpkey.h> // Property keys for audio devices
#include <avrt.h> // Thread management
#include "windows.h"
#endif

#include "os_generic.h"

#if defined(WIN32) && !defined( TCC )
#pragma comment(lib,"avrt.lib")
#pragma comment(lib,"ole32.lib")
//And maybe mmdevapi.lib
#endif

#define WASAPIPRINT(message) (printf("[WASAPI] %s\n", message))
#define WASAPIERROR(error, message) (printf("[WASAPI][ERR] %s HRESULT: 0x%lX\n", message, error))
#define PRINTGUID(guid) (printf("{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", guid.Data1, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]))

#define WASAPI_EXTRA_DEBUG FALSE

// Forward declarations
void CloseCNFAWASAPI(void* stateObj);
int CNFAStateWASAPI(void* object);
static struct CNFADriverWASAPI* StartWASAPIDriver(struct CNFADriverWASAPI* initState);
static IMMDevice* WASAPIGetDefaultDevice(BOOL isCapture, BOOL isMultimedia);
static void WASAPIPrintAllDeviceLists();
static void WASAPIPrintDeviceList(EDataFlow dataFlow);
void* ProcessEventAudioIn(void* stateObj);
void* InitCNFAWASAPIDriver(
	CNFACBType callback, const char *session_name,
	int reqSampleRateOut, int reqSampleRateIn,
	int reqChannelsOut, int reqChannelsIn, int sugBufferSize,
	const char * inputDevice, const char * outputDevice,
	void * opaque
);

DEFINE_GUID(CLSID_MMDeviceEnumerator, 0xBCDE0395L, 0xE52F, 0x467C, 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E);
DEFINE_GUID(IID_IMMDeviceEnumerator, 0xA95664D2L, 0x9614, 0x4F35, 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6);
DEFINE_GUID(IID_IMMEndpoint, 0x1BE09788L, 0x6894, 0x4089, 0x85, 0x86, 0x9A, 0x2A, 0x6C, 0x26, 0x5A, 0xC5);
DEFINE_GUID(IID_IAudioClient, 0x1CB9AD4CL, 0xDBFA, 0x4c32, 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2);
DEFINE_GUID(IID_IAudioCaptureClient, 0xC8ADBD64L, 0xE71E, 0x48a0, 0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17);

// This is a fallback if the client application does not provide a GUID.
DEFINE_GUID(CNFA_GUID, 0x899081C7L, 0x9428, 0x4103, 0x87, 0x93, 0x26, 0x47, 0xE5, 0xEA, 0xA2, 0xB4);

struct CNFADriverWASAPI
{
	// Common CNFA items
    void (*CloseFn)(void* object);
	int (*StateFn)(void* object);
	CNFACBType Callback;
	short ChannelCountOut; // Not yet used.
	short ChannelCountIn; // How many cahnnels the input stream has per frame. E.g. stereo = 2.
	int SampleRateOut;
	int SampleRateIn;
	void* Opaque; // Not relevant to us

	// Adjustable WASAPI-specific items
	const char* SessionName; // The name to give our audio sessions. Otherwise, defaults to using embedded EXE name, Window title, or EXE file name directly.
	const GUID* SessionID; // In order to have different CNFA-based applications individually controllable from the volume mixer, this should be set differently for every client program, but constant across all runs/builds of that application.

	// Everything below here is for internal use only. Do not attempt to interact with these items.
	const char* InputDeviceID; // The device to use for getting input from. Can be a render device (operating in loopback), or a capture device.
	const char* OutputDeviceID; // Not yet used.
	IMMDeviceEnumerator* DeviceEnumerator; // The base object that allows us to look through the system's devices, and from there get everything else.
	IMMDevice* Device; // The device we are taking input from.
	IAudioClient* Client; // The base client we use for getting input.
	IAudioCaptureClient* CaptureClient; // The specific client we use for getting input.
	WAVEFORMATEX* MixFormat; // The format of the input stream.
	INT32 BytesPerFrame; // The number of bytes of one full frame of audio. AKA (channel count) * (sample bit depth), in Bytes.
	BOOL StreamReady; // Whether the input stream is ready for data retrieval.
	BOOL KeepGoing; // Whether to continue interacting with the streams, or shutdown the driver.
	og_thread_t ThreadOut; // Not yet used.
	og_thread_t ThreadIn; // The thread used to grab input data.
	HANDLE EventHandleOut; // Not yet used.
	HANDLE EventHandleIn; // The handle used to wait for more input data to be ready in the input thread.
	HANDLE TaskHandleOut; // The task used to request output thread priority changes.
	HANDLE TaskHandleIn; // The task used to request input thread priority changes.
};

// This is where the driver's current state is stored.
static struct CNFADriverWASAPI* WASAPIState;

// Stops streams, ends threads, and cleans up all resources used by the driver.
void CloseCNFAWASAPI(void* stateObj)
{
	struct CNFADriverWASAPI* state = (struct CNFADriverWASAPI*)stateObj;
	if(state != NULL)
	{
		// TODO: See if there are any other items that need cleanup.
		state->KeepGoing = FALSE;
		if (state->ThreadOut != NULL) { OGJoinThread(state->ThreadOut); }
		if (state->ThreadIn != NULL) { OGJoinThread(state->ThreadIn); }
		if (state->EventHandleOut != NULL) { CloseHandle(state->EventHandleOut); }
		if (state->EventHandleIn != NULL) { CloseHandle(state->EventHandleIn); }
		CoTaskMemFree(state->MixFormat);
		if (state->CaptureClient != NULL) { state->CaptureClient->lpVtbl->Release(state->CaptureClient); }
		if (state->Client != NULL) { state->Client->lpVtbl->Release(state->Client); }
		if (state->Device != NULL) { state->Device->lpVtbl->Release(state->Device); }
		if (state->DeviceEnumerator != NULL) { state->DeviceEnumerator->lpVtbl->Release(state->DeviceEnumerator); }
		free(stateObj);
		CoUninitialize();
		printf("[WASAPI] Cleanup completed. Goodbye.\n");
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
	if(state != NULL)
	{
		if (state->StreamReady) { return 1; } // TODO: Output the correct status when output is implemented.
	}
	return 0;
}

// Reads the desired configuration, interfaces with WASAPI to get the current system information, and starts the input stream.
static struct CNFADriverWASAPI* StartWASAPIDriver(struct CNFADriverWASAPI* initState)
{
	WASAPIState = initState;
	WASAPIState->StreamReady = FALSE;
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

	if(WASAPI_EXTRA_DEBUG)
	{
		printf("[WASAPI] CLSID for MMDeviceEnumerator: ");
		PRINTGUID(CLSID_MMDeviceEnumerator);
		printf("\n[WASAPI] IID for IMMDeviceEnumerator: ");
		PRINTGUID(IID_IMMDeviceEnumerator);
		printf("\n[WASAPI] IID for IAudioClient: ");
		PRINTGUID(IID_IAudioClient);
		printf("\n[WASAPI] IID for IAudioCaptureClient: ");
		PRINTGUID(IID_IAudioCaptureClient);
		printf("\n[WASAPI] IID for IMMEndpoint: ");
		PRINTGUID(IID_IMMEndpoint);
		printf("\n");
	}

	ErrorCode = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void**)&(WASAPIState->DeviceEnumerator));
	if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get device enumerator. "); return WASAPIState; }

	WASAPIPrintAllDeviceLists();

	// We need to find the appropriate device to use.
	BYTE DeviceDirection = 2; // 0 = Render, 1 = Capture, 2 = Unknown

	if (WASAPIState->InputDeviceID == NULL)
	{
		WASAPIPRINT("No device specified, attempting to use system default multimedia capture device as input.");
		WASAPIState->Device = WASAPIGetDefaultDevice(TRUE, TRUE);
		DeviceDirection = 1;
	}
	else if (strcmp(WASAPIState->InputDeviceID, "defaultRender") == 0)
	{
		WASAPIPRINT("Attempting to use system default render device as input.");
		WASAPIState->Device = WASAPIGetDefaultDevice(FALSE, TRUE);
		DeviceDirection = 0;
	}
	else if (strncmp("defaultCapture", WASAPIState->InputDeviceID, strlen("defaultCapture")) == 0)
	{
		BOOL IsMultimedia = TRUE;
		if (strstr(WASAPIState->InputDeviceID, "Comm") != NULL) { IsMultimedia = FALSE; }
		printf("[WASAPI] Attempting to use system default %s capture device as input.\n", (IsMultimedia ? "multimedia" : "communications"));
		WASAPIState->Device = WASAPIGetDefaultDevice(TRUE, IsMultimedia);
		DeviceDirection = 1;
	}
	else // A specific device was selected by ID.
	{
		LPWSTR DeviceIDasLPWSTR;
		DeviceIDasLPWSTR = malloc((strlen(WASAPIState->InputDeviceID) + 1) * sizeof(WCHAR));
		mbstowcs(DeviceIDasLPWSTR, WASAPIState->InputDeviceID, strlen(WASAPIState->InputDeviceID) + 1);
		printf("[WASAPI] Attempting to find specified device \"%ls\".\n", DeviceIDasLPWSTR);

		ErrorCode = WASAPIState->DeviceEnumerator->lpVtbl->GetDevice(WASAPIState->DeviceEnumerator, DeviceIDasLPWSTR, &(WASAPIState->Device));
		if (FAILED(ErrorCode))
		{
			WASAPIERROR(ErrorCode, "Failed to get audio device from the given ID. Using default multimedia capture device instead.");
			WASAPIState->Device = WASAPIGetDefaultDevice(TRUE, TRUE);
			DeviceDirection = 1;
		}
		else
		{
			printf("[WASAPI] Found specified device.\n");
			DWORD DeviceState;
			ErrorCode = WASAPIState->Device->lpVtbl->GetState(WASAPIState->Device, &DeviceState);
			if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get device state."); }

			if ((DeviceState & DEVICE_STATE_DISABLED) == DEVICE_STATE_DISABLED) { WASAPIERROR(E_FAIL, "The specified device is currently disabled."); }
			if ((DeviceState & DEVICE_STATE_NOTPRESENT) == DEVICE_STATE_NOTPRESENT) { WASAPIERROR(E_FAIL, "The specified device is not currently present."); }
			if ((DeviceState & DEVICE_STATE_UNPLUGGED) == DEVICE_STATE_UNPLUGGED) { WASAPIERROR(E_FAIL, "The specified device is currently unplugged."); }
		}
	}

	if (DeviceDirection == 2) // We still don't know what type of device we are trying to use. Query the endpoint to find out.
	{
		IMMEndpoint* Endpoint;
		ErrorCode = WASAPIState->Device->lpVtbl->QueryInterface(WASAPIState->Device, &IID_IMMEndpoint, (void**)&Endpoint);
		if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get endpoint of device."); }

		EDataFlow DataFlow;
		ErrorCode = Endpoint->lpVtbl->GetDataFlow(Endpoint, &DataFlow);
		if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Could not determine endpoint type."); }

		DeviceDirection = (DataFlow == eRender) ? 0 : 1;
		
		if (Endpoint != NULL) { Endpoint->lpVtbl->Release(Endpoint); }
	}

	// We should have a device now.
	char* DeviceDirectionDesc = (DeviceDirection == 0) ? "render" : ((DeviceDirection == 1) ? "capture" : "UNKNOWN");

	LPWSTR DeviceID;
	ErrorCode = WASAPIState->Device->lpVtbl->GetId(WASAPIState->Device, &DeviceID);
	if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get audio device ID."); return WASAPIState; }
	else { printf("[WASAPI] Using device ID \"%ls\", which is a %s device.\n", DeviceID, DeviceDirectionDesc); }

	// Start an audio client and get info about the stream format.
	ErrorCode = WASAPIState->Device->lpVtbl->Activate(WASAPIState->Device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&(WASAPIState->Client));
	if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get audio client. "); return WASAPIState; }

	ErrorCode = WASAPIState->Client->lpVtbl->GetMixFormat(WASAPIState->Client, &(WASAPIState->MixFormat));
	if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get mix format. "); return WASAPIState; }
	printf("[WASAPI] Mix format is %d channel, %luHz sample rate, %db per sample.\n", WASAPIState->MixFormat->nChannels, WASAPIState->MixFormat->nSamplesPerSec, WASAPIState->MixFormat->wBitsPerSample);
	printf("[WASAPI] Mix format is format %d, %dB block-aligned, with %dB of extra data in this definition.\n", WASAPIState->MixFormat->wFormatTag, WASAPIState->MixFormat->nBlockAlign, WASAPIState->MixFormat->cbSize);

	// We'll request PCM, 16bpS data from the system. It should be able to do this conversion for us, as long as we are not in exclusive mode.
	// TODO: This isn't working, no matter what combination I try to ask it for. Figure this out, so we don't have to do the conversion ourselves.
	// Also, we probably don't handle channel counts > 2 with this current setup.
	//WASAPIState->MixFormat->wFormatTag = WAVE_FORMAT_PCM;
	//WASAPIState->MixFormat->wBitsPerSample = 16 * WASAPIState->MixFormat->nChannels;
	//WASAPIState->MixFormat->nBlockAlign = 2 * WASAPIState->MixFormat->nChannels;
	//WASAPIState->MixFormat->nAvgBytesPerSec = WASAPIState->MixFormat->nSamplesPerSec * WASAPIState->MixFormat->nBlockAlign;

	WASAPIState->ChannelCountIn = WASAPIState->MixFormat->nChannels;
	WASAPIState->SampleRateIn = WASAPIState->MixFormat->nSamplesPerSec;
	WASAPIState->BytesPerFrame = WASAPIState->MixFormat->nChannels * (WASAPIState->MixFormat->wBitsPerSample / 8);

	REFERENCE_TIME DefaultInterval, MinimumInterval;
	ErrorCode = WASAPIState->Client->lpVtbl->GetDevicePeriod(WASAPIState->Client, &DefaultInterval, &MinimumInterval);
	if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get device timing info. "); return WASAPIState; }
	printf("[WASAPI] Default transaction period is %lld ticks, minimum is %lld ticks.\n", DefaultInterval, MinimumInterval);

	// Configure a capture client.
	UINT32 StreamFlags;
	if (DeviceDirection == 1) { StreamFlags = AUDCLNT_STREAMFLAGS_NOPERSIST | AUDCLNT_STREAMFLAGS_EVENTCALLBACK; }
	else if (DeviceDirection == 0) { StreamFlags = (AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK); }
	else { WASAPIPRINT("[ERR] Device type was not determined!"); return WASAPIState; }

	// TODO: Allow the target application to influence the interval we choose. Super realtime apps may require MinimumInterval.
	ErrorCode = WASAPIState->Client->lpVtbl->Initialize(WASAPIState->Client, AUDCLNT_SHAREMODE_SHARED, StreamFlags, DefaultInterval, DefaultInterval, WASAPIState->MixFormat, WASAPIState->SessionID);
	if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Could not init audio client."); return WASAPIState; }

	WASAPIState->EventHandleIn = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (WASAPIState->EventHandleIn == NULL) { WASAPIERROR(E_FAIL, "Failed to make event handle."); return WASAPIState; }

	ErrorCode = WASAPIState->Client->lpVtbl->SetEventHandle(WASAPIState->Client, WASAPIState->EventHandleIn);
	if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to set event handler."); return WASAPIState; }

	UINT32 BufferFrameCount;
	ErrorCode = WASAPIState->Client->lpVtbl->GetBufferSize(WASAPIState->Client, &BufferFrameCount);
	if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Could not get audio client buffer size."); return WASAPIState; }

	ErrorCode = WASAPIState->Client->lpVtbl->GetService(WASAPIState->Client, &IID_IAudioCaptureClient, (void**)&(WASAPIState->CaptureClient));
	if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Could not get audio capture client."); return WASAPIState; }
	
	// Begin capturing audio. It will be received on a separate thread.
	ErrorCode = WASAPIState->Client->lpVtbl->Start(WASAPIState->Client);
	if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Could not start audio client."); return WASAPIState; }
	WASAPIState->StreamReady = TRUE;

	WASAPIState->KeepGoing = TRUE;
	WASAPIState->ThreadIn = OGCreateThread(ProcessEventAudioIn, WASAPIState);

	return WASAPIState;
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
static void WASAPIPrintAllDeviceLists()
{
	WASAPIPrintDeviceList(eRender);
	WASAPIPrintDeviceList(eCapture);
}

// Prints a list of all available devices of a specified data flow direction to the console.
static void WASAPIPrintDeviceList(EDataFlow dataFlow)
{
	printf("[WASAPI] %s Devices:\n", (dataFlow == eCapture ? "Capture" : "Render"));
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

		printf("[WASAPI] [%d]: \"%ls\" = \"%ls\"\n", DeviceIndex, DeviceFriendlyName, DeviceID);

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

	// TODO: Set this based on our device period requested. If we are using 10ms or higher, just request "Audio", not "Pro Audio".
	DWORD TaskIndex = 0;
	state->TaskHandleIn = AvSetMmThreadCharacteristicsW(L"Pro Audio", &TaskIndex);
	if (state->TaskHandleIn == NULL) { WASAPIERROR(E_FAIL, "Failed to request thread priority elevation on input task."); }

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
		BOOL Released = FALSE;
		ErrorCode = state->CaptureClient->lpVtbl->GetBuffer(state->CaptureClient, &DataBuffer, &FramesAvailable, &BufferStatus, NULL, NULL);
		if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get audio buffer."); continue; }

		// "The data in the packet is not correlated with the previous packet's device position; this is possibly due to a stream state transition or timing glitch."
		// There's no real way for us to notify the client about this...
		if ((BufferStatus & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) == AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)
		{
			WASAPIPRINT("A data discontinuity was detected.");
		}

		if ((BufferStatus & AUDCLNT_BUFFERFLAGS_SILENT) == AUDCLNT_BUFFERFLAGS_SILENT)
		{
			UINT32 Length = FramesAvailable * state->MixFormat->nChannels;
			if (Length == 0) { Length = state->MixFormat->nChannels; }
			INT16* AudioData = malloc(Length * 2);
			for (int i = 0; i < Length; i++) { AudioData[i] = 0; }

			ErrorCode = state->CaptureClient->lpVtbl->ReleaseBuffer(state->CaptureClient, FramesAvailable);
			if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to release audio buffer."); }
			else { Released = TRUE; }

			if (WASAPI_EXTRA_DEBUG) { printf("[WASAPI] SILENCE buffer received. Passing on %d samples.\n", Length); }

			WASAPIState->Callback((struct CNFADriver*)WASAPIState, 0, AudioData, 0, Length / state->MixFormat->nChannels );
			free(AudioData);
		}
		else
		{
			// TODO: This assumes that data is coming in at 32b float format. While this appears to be the format that WASAPI uses internally in all cases I've seen, I don't think it's guaranteed.
			// We should instead read the MixFormat information and properly handle the data in other cases.
			// Ideally, we could request 16b signed PCM data from WASAPI, so we don't even have to do any conversion. But I couldn't get this working yet.
			UINT32 Size = FramesAvailable * state->BytesPerFrame; // Size in bytes
			FLOAT* DataAsFloat = (FLOAT*)DataBuffer; // The raw input data, reinterpreted as floats.
			INT16* AudioData = malloc((FramesAvailable * state->MixFormat->nChannels) * 2); // The data we are passing to the consumer.
			for (INT32 i = 0; i < Size / 4; i++) { AudioData[i] = (INT16)(DataAsFloat[i] * 32767.5F); }

			ErrorCode = state->CaptureClient->lpVtbl->ReleaseBuffer(state->CaptureClient, FramesAvailable);
			if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to release audio buffer."); }
			else { Released = TRUE; }

			if (WASAPI_EXTRA_DEBUG) { printf("[WASAPI] Got %d bytes of audio data in %d frames. Fowarding to %p.\n", Size, FramesAvailable, (void*) WASAPIState->Callback); }

			WASAPIState->Callback((struct CNFADriver*)WASAPIState, 0, AudioData, 0, FramesAvailable );
			free(AudioData);
		}

		if (!Released)
		{
			ErrorCode = state->CaptureClient->lpVtbl->ReleaseBuffer(state->CaptureClient, FramesAvailable);
			if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to release audio buffer."); }
		}

	}

	ErrorCode = state->Client->lpVtbl->Stop(state->Client);
	if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to stop audio client."); }
	
	if(state->TaskHandleIn != NULL) { AvRevertMmThreadCharacteristics(state->TaskHandleIn); }

	state->StreamReady = FALSE;
	return 0;
}

// Begins preparation of the WASAPI driver.
// callback: The user application's function where audio data is placed when received from the system and/or audio data is retrieved from to give to the system.
// sessionName: How your session will appear to the end user if you play audio.
// reqSampleRateIn/Out: Sample rate you'd like to request. Ignored, as this is determined by the system. See note below.
// reqChannelsIn: Input channel count you'd like to request. Ignored, as this is determined by the system. See note below.
// reqChannelsOut: Output channel count you'd like to request. Ignored, as this is determined by the system. See note below.
// sugBufferSize: Buffer size you'd like to request. Ignored, as this is determined by the system. See note below.
// inputDevice: The device you want to receive audio from. Loopback is supported, so this can be either a capture or render device.
//              To get the default render device, specify "defaultRender"
//              To get the default multimedia capture device, specify "defaultCapture"
//              To get the default communications capture device, specify "defaultCaptureComm"
//              A device ID as presented by WASAPI can be specified, regardless of what type it is. If it is invalid, the default capture device is used as fallback.
//              If you do not wish to receive audio, specify null. NOT YET IMPLEMENTED
// outputDevice: The device you want to output audio to. OUTPUT IS NOT IMPLEMENTED.
// NOTES:
// Regarding format requests: Sample rate and channel count is determined by the system settings, and cannot be changed. Resampling/mixing will be required in your application if you cannot accept the current system mode. Make sure to check `WASAPIState` for the current system mode.
//                            Note also that both sample rate and channel count can vary between input and output!
// Currently audio output (playing) is not yet implemented.
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
	// TODO: Waiting for CNFA to support directional sample rates.
	InitState->SampleRateIn = reqSampleRateIn; // Will be overridden by the actual system setting.
	InitState->SampleRateOut = reqSampleRateOut; // Will be overridden by the actual system setting.
	InitState->ChannelCountIn = reqChannelsIn; // Will be overridden by the actual system setting.
	InitState->ChannelCountOut = reqChannelsOut; // Will be overridden by the actual system setting.
	InitState->InputDeviceID = inputDevice;
	InitState->OutputDeviceID = outputDevice;

	InitState->SessionName = sessionName;

	WASAPIPRINT("WASAPI Init");

	return StartWASAPIDriver(InitState);
}

REGISTER_CNFA(cnfa_wasapi, 20, "WASAPI", InitCNFAWASAPIDriver);
