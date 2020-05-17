#include "CNFA.h"

#ifdef TCC
#include "CNFA_wasapi_utils.h"
#else
#include <InitGuid.h>
#include <audioclient.h> // Render and capturing audio
#include <mmdeviceapi.h> // Audio device handling
#include <Functiondiscoverykeys_devpkey.h> // Property keys for audio devices
#include <avrt.h> // Thread management
#endif

#include "windows.h"
#include "os_generic.h"

#define WASAPIPRINT(message) (printf("[WASAPI] %s\n", message))
#define WASAPIERROR(error, message) (printf("[WASAPI][ERR] %s HRESULT: 0x%X\n", message, error))
#define PRINTGUID(guid) (printf("{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", guid.Data1, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]))

#define WASAPI_EXTRA_DEBUG FALSE

// Forward declarations
void CloseCNFAWASAPI(void* stateObj);
int CNFAStateWASAPI(void* object);
static struct CNFADriverWASAPI* StartWASAPIDriver(struct CNFADriverWASAPI* initState);
static IMMDevice* WASAPIGetDefaultDevice(BOOL isCapture);
static void WASAPIPrintAllDeviceLists();
static void WASAPIPrintDeviceList(EDataFlow dataFlow);
void* ProcessEventAudioIn(void* stateObj);
void* InitCNFAWASAPIDriver(CNFACBType callback, const char* sessionName, int reqSampleRate, int reqChannelsIn, int reqChannelsOut, int sugBufferSize, const char* inputDevice, const char* outputDevice);

DEFINE_GUID(CLSID_MMDeviceEnumerator, 0xBCDE0395L, 0xE52F, 0x467C, 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E);
DEFINE_GUID(IID_IMMDeviceEnumerator, 0xA95664D2L, 0x9614, 0x4F35, 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6);
DEFINE_GUID(IID_IAudioClient, 0x1CB9AD4CL, 0xDBFA, 0x4c32, 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2);
DEFINE_GUID(IID_IAudioCaptureClient, 0xC8ADBD64L, 0xE71E, 0x48a0, 0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17);

DEFINE_GUID(CNFA_GUID, 0x899081C7L, 0x9428, 0x4103, 0x87, 0x93, 0x26, 0x47, 0xE5, 0xEA, 0xA2, 0xB4);

struct CNFADriverWASAPI
{
	// Common CNFA items
    void (*CloseFn)(void* object);
	int (*StateFn)(void* object);
	CNFACBType Callback;
	short ChannelCountOut; // Not yet used.
	short ChannelCountIn; // How many cahnnels the input stream has per frame. E.g. stereo = 2.
	int SampleRate;
	void* Opaque; // Not relevant to us

	// Adjustable WASAPI-specific items
	const char* SessionName; // The name to give our audio sessions. Otherwise, defaults to using embedded EXE name, Window title, or EXE file name directly.
	GUID* SessionID; // In order to have different CNFA-based applications individually controllable from the volume mixer, this should be set differently for every client program, but constant across all runs/builds of that application.

	// Everything below here is for internal use only. Do not attempt to interact with these items.
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

void CloseCNFAWASAPI(void* stateObj)
{
	struct CNFADriverWASAPI* state = (struct CNFADriverWASAPI*)stateObj;
	if(state != NULL)
	{
		state->KeepGoing = FALSE;
		if (state->ThreadOut != NULL) { OGJoinThread(state->ThreadOut); }
		if (state->ThreadIn != NULL) { OGJoinThread(state->ThreadIn); }
		if (state->EventHandleOut = NULL) { CloseHandle(state->EventHandleOut); }
		if (state->EventHandleIn = NULL) { CloseHandle(state->EventHandleIn); }
		// TODO: Cleanup stuff.
		free(stateObj);
		// All COM objects.Release()
		CoUninitialize();
	}
}

int CNFAStateWASAPI(void* stateObj)
{
	struct CNFADriverWASAPI* state = (struct CNFADriverWASAPI*)stateObj;
	if(state != NULL)
	{
		if (state->StreamReady) {return 1;}
	}
	return 0;
}

static struct CNFADriverWASAPI* StartWASAPIDriver(struct CNFADriverWASAPI* initState)
{
	WASAPIState = initState;
	WASAPIState->StreamReady = FALSE;
	WASAPIState->SessionID = &CNFA_GUID;

	HRESULT ErrorCode;
	ErrorCode = CoInitialize(NULL); // TODO: Consider using CoInitializeEx if needed for threading.
	if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "COM INIT FAILED!"); return WASAPIState; }

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
		printf("\n");
	}

	ErrorCode = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void**)&(WASAPIState->DeviceEnumerator));
	if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get device enumerator. "); return WASAPIState; }

	WASAPIPrintAllDeviceLists();

	WASAPIState->Device = WASAPIGetDefaultDevice(FALSE);

	LPWSTR* DeviceID;
	ErrorCode = WASAPIState->Device->lpVtbl->GetId(WASAPIState->Device, &DeviceID);
	if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get audio device ID."); return WASAPIState;; }
	else { printf("[WASAPI] Using device ID \"%ls\".\n", DeviceID); }

	BYTE DeviceIsCapture = 2; // 0 = Render, 1 = Capture, 2 = Unknown

	// TODO: Implement detection.
	DeviceIsCapture = 0;

	ErrorCode = WASAPIState->Device->lpVtbl->Activate(WASAPIState->Device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&(WASAPIState->Client));
	if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get audio client. "); return WASAPIState; }

	ErrorCode = WASAPIState->Client->lpVtbl->GetMixFormat(WASAPIState->Client, (void**)&(WASAPIState->MixFormat));
	if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get mix format. "); return WASAPIState; }
	printf("[WASAPI] Mix format is %d channel, %dHz sample rate, %db per sample.\n", WASAPIState->MixFormat->nChannels, WASAPIState->MixFormat->nSamplesPerSec, WASAPIState->MixFormat->wBitsPerSample);
	printf("[WASAPI] Mix format is format %d, %dB block-aligned, with %dB of extra data in this definition.\n", WASAPIState->MixFormat->wFormatTag, WASAPIState->MixFormat->nBlockAlign, WASAPIState->MixFormat->cbSize);

	// We'll request PCM, 16bbs data from the system. It should be able to do this conversion for us, as long as we are not in exclusive mode.
	// TODO: This isn't working, no matter what combination I try to ask it for. Figure this out, so we don't have to do the conversion ourselves.
	//WASAPIState->MixFormat->wFormatTag = WAVE_FORMAT_PCM;
	//WASAPIState->MixFormat->wBitsPerSample = 16 * WASAPIState->MixFormat->nChannels;
	//WASAPIState->MixFormat->nBlockAlign = 2 * WASAPIState->MixFormat->nChannels;
	//WASAPIState->MixFormat->nAvgBytesPerSec = WASAPIState->MixFormat->nSamplesPerSec * WASAPIState->MixFormat->nBlockAlign;
	WASAPIState->ChannelCountIn = WASAPIState->MixFormat->nChannels;
	
	WASAPIState->BytesPerFrame = WASAPIState->MixFormat->nChannels * (WASAPIState->MixFormat->wBitsPerSample / 8);

	REFERENCE_TIME* DefaultInterval, MinimumInterval;
	ErrorCode = WASAPIState->Client->lpVtbl->GetDevicePeriod(WASAPIState->Client, &DefaultInterval, &MinimumInterval);
	if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get device timing info. "); return WASAPIState; }
	printf("[WASAPI] Default transaction period is %d ticks, minimum is %d ticks.\n", DefaultInterval, MinimumInterval);

	WASAPIState->SampleRate = WASAPIState->MixFormat->nSamplesPerSec;

	UINT32 StreamFlags;
	if (DeviceIsCapture == 1) { StreamFlags = AUDCLNT_STREAMFLAGS_NOPERSIST | AUDCLNT_STREAMFLAGS_EVENTCALLBACK; }
	else if (DeviceIsCapture == 0) { StreamFlags = (AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK); }
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
	
	ErrorCode = WASAPIState->Client->lpVtbl->Start(WASAPIState->Client);
	if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Could not start audio client."); return WASAPIState; }
	WASAPIState->StreamReady = TRUE;

	WASAPIState->KeepGoing = TRUE;
	WASAPIState->ThreadIn = OGCreateThread(ProcessEventAudioIn, WASAPIState);

	return WASAPIState;
}

// Gets the default render or capture device.
static IMMDevice* WASAPIGetDefaultDevice(BOOL isCapture)
{
	HRESULT ErrorCode;
	IMMDevice* Device;
	ErrorCode = WASAPIState->DeviceEnumerator->lpVtbl->GetDefaultAudioEndpoint(WASAPIState->DeviceEnumerator, isCapture ? eCapture : eRender, eMultimedia, (void**)&Device);
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
	HRESULT ErrorCode = WASAPIState->DeviceEnumerator->lpVtbl->EnumAudioEndpoints(WASAPIState->DeviceEnumerator, dataFlow, DEVICE_STATE_ACTIVE, (void**)&Devices);
	if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get audio endpoints."); return; }

	UINT32 DeviceCount;
	ErrorCode = Devices->lpVtbl->GetCount(Devices, &DeviceCount);
	if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get audio endpoint count."); return; }

	for (UINT32 DeviceIndex = 0; DeviceIndex < DeviceCount; DeviceIndex++)
	{
		IMMDevice* Device;
		ErrorCode = Devices->lpVtbl->Item(Devices, DeviceIndex, (void**)&Device);
		if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get audio device."); continue; }

		LPWSTR* DeviceID;
		ErrorCode = Device->lpVtbl->GetId(Device, &DeviceID);
		if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get audio device ID."); continue; }

		IPropertyStore* Properties;
		ErrorCode = Device->lpVtbl->OpenPropertyStore(Device, STGM_READ, (void**)&Properties);
		if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get device properties."); continue; }
		
		PROPVARIANT Variant;
		PropVariantInit(&Variant);

		ErrorCode = Properties->lpVtbl->GetValue(Properties, &PKEY_Device_FriendlyName, &Variant);
		if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get device friendly name."); }

		LPWSTR DeviceFriendlyName = L"[Name Retrieval Failed]";
		if (&Variant != NULL && Variant.pwszVal != NULL) { DeviceFriendlyName = Variant.pwszVal; }

		printf("[WASAPI] [%d]: \"%ls\" = \"%ls\"\n", DeviceIndex, DeviceFriendlyName, DeviceID);
	}

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
		// Waits up to infinite time to get the next event from the audio system.
		// TODO: Consider adding a timeout if needed?
		DWORD WaitResult = WaitForSingleObject(state->EventHandleIn, INFINITE);

		ErrorCode = state->CaptureClient->lpVtbl->GetNextPacketSize(state->CaptureClient, &PacketLength);
		if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get audio packet size."); continue; }

		BYTE* DataBuffer;
		UINT32 FramesAvailable;
		DWORD BufferStatus;
		BOOL Released = FALSE;
		ErrorCode = state->CaptureClient->lpVtbl->GetBuffer(state->CaptureClient, &DataBuffer, &FramesAvailable, &BufferStatus, NULL, NULL);
		if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to get audio buffer."); continue; }

		if ((BufferStatus & AUDCLNT_BUFFERFLAGS_SILENT) == AUDCLNT_BUFFERFLAGS_SILENT)
		{
			// TODO: Clear the buffer, as there are no active streams anymore, and we won't receive any more events until a stream starts.
		}
		else
		{
			// TODO THIS SECTION NEEDS CLEANUP AND HANDLING OF OTHER DATATYPES!!!
			UINT32 Size = FramesAvailable * state->BytesPerFrame; // Size in bytes
			FLOAT* DataAsFloat = (FLOAT*)DataBuffer;
			UINT16* AudioData = malloc((FramesAvailable * state->MixFormat->nChannels) * 2);
			for (INT32 i = 0; i < Size / 4; i++) { AudioData[i] = (SHORT)(DataAsFloat[i] * 32767.5F); }

			ErrorCode = state->CaptureClient->lpVtbl->ReleaseBuffer(state->CaptureClient, FramesAvailable);
			if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to release audio buffer."); }
			else { Released = TRUE; }

			if (WASAPI_EXTRA_DEBUG) { printf("[WASAPI] Got %d bytes of audio data in %d frames. Fowarding to %d.\n", Size, FramesAvailable, WASAPIState->Callback); }

			WASAPIState->Callback((struct CNFADriver*)WASAPIState, AudioData, 0, (FramesAvailable * state->MixFormat->nChannels) / 2, 0);
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

// callback: The user application's function where audio data is placed when received from the system and/or audio data is retrieved from to give to the system.
// sessionName: How your session will appear to the end user if you play audio.
// reqSampleRate: Sample rate you'd like to request. Ignored, as this is determined by the system. See note below.
// reqChannelsIn: Input channel count you'd like to request. Ignored, as this is determined by the system. See note below.
// reqChannelsOut: Output channel count you'd like to request. Ignored, as this is determined by the system. See note below.
// sugBufferSize: Buffer size you'd like to request. Ignored, as this is determined by the system. See note below.
// inputDevice: The device you want to receive audio from. Loopback is supported, so this can be either a capture or render device.
//              To get the default render device, specify "defaultRender"
//              To get the default capture device, specify "defaultCapture"
//              A device ID as presented by WASAPI can be specified, regardless of what type it is.
//              If you do not wish to receive audio, specify null.
// outputDevice: The device you want to output audio to. OUTPUT IS NOT IMPLEMENTED.
// NOTES: 
// Regarding format requests: Sample rate and channel count is determined by the system settings, and cannot be changed. Resampling/mixing will be required in your application if you cannot accept the current system mode. Make sure to check `WASAPIState` for the current system mode.
//                            Note also that both sample rate and channel count can vary between input and output!
void* InitCNFAWASAPIDriver(CNFACBType callback, const char* sessionName, int reqSampleRate, int reqChannelsIn, int reqChannelsOut, int sugBufferSize, const char* inputDevice, const char* outputDevice)
{
	struct CNFADriverWASAPI * InitState = malloc(sizeof(struct CNFADriverWASAPI));
	InitState->CloseFn = CloseCNFAWASAPI;
	InitState->StateFn = CNFAStateWASAPI;
	InitState->Callback = callback;
	// TODO: Waiting for CNFA to support directional sample rates.
	InitState->SampleRate = reqSampleRate; // Will be overridden by the actual system setting.
	InitState->ChannelCountIn = reqChannelsIn; // Will be overridden by the actual system setting.
	InitState->ChannelCountOut = reqChannelsOut; // Will be overridden by the actual system setting.
	
	InitState->SessionName = sessionName;

	WASAPIPRINT("WASAPI Init");

	return StartWASAPIDriver(InitState);
}

// This is the equivalent of a static constructor that also calls the base constructor.
REGISTER_CNFA(cnfa_wasapi, 9, "WASAPI", InitCNFAWASAPIDriver);