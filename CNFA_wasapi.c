#include "CNFA.h"
#include "CNFA_wasapi_utils.h"
//#include <InitGuid.h>
//#include <audioclient.h>
//#include <mmdeviceapi.h>
#include "windows.h"
#include "os_generic.h"

#define WASAPIPRINT(message) (printf("[WASAPI] %s\n", message))
#define WASAPIERROR(error, message) (printf("[WASAPI][ERR] %s HRESULT: 0x%X\n", message, error))

// Forward declarations
void CloseCNFAWASAPI(void* stateObj);
int CNFAStateWASAPI(void* object);
static struct CNFADriverWASAPI* StartWASAPIDriver(struct CNFADriverWASAPI* initState);
static IMMDevice* WASAPIGetDefaultDevice(BOOL isCapture);
static void WASAPIPrintAllDeviceLists();
static void WASAPIPrintDeviceList(EDataFlow dataFlow);
void* ProcessEventAudioIn(void* stateObj);
void* InitCNFAWASAPIDriver(CNFACBType callback, const char* sessionName, int reqSampleRate, int reqChannelsIn, int reqChannelsOut, int sugBufferSize, const char* inputDevice, const char* outputDevice);

DEFINE_GUID(IID_IMMDeviceEnumerator, 0xA95664D2L, 0x9614, 0x4F35, 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6);
DEFINE_GUID(CLSID_MMDeviceEnumerator, 0xBCDE0395L, 0xE52F, 0x467C, 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E);
DEFINE_GUID(IID_IAudioClient, 0x1CB9AD4CL, 0xDBFA, 0x4c32, 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2);
DEFINE_GUID(IID_IAudioCaptureClient, 0xC8ADBD64L, 0xE71E, 0x48a0, 0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17);


DEFINE_GUID(CNFA_GUID, 0x899081C7L, 0x9428, 0x4103, 0x87, 0x93, 0x26, 0x47, 0xE5, 0xEA, 0xA2, 0xB4); // TODO: Remove this and generate it based on the application name instead

struct CNFADriverWASAPI
{
    void (*CloseFn)(void* object);
	int (*StateFn)(void* object);
	CNFACBType Callback;
	short ChannelCountOut; // Not yet used.
	short ChannelCountIn; // How many cahnnels the input stream has per frame. E.g. stereo = 2.
	int SampleRate;
	void* Opaque; // Not relevant to us

	const char* SessionName;

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

	HRESULT ErrorCode;
	ErrorCode = CoInitialize(NULL); // TODO: Consider using CoInitializeEx if needed for threading.
	if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "COM INIT FAILED!"); return WASAPIState; }

	printf("CLSID MMDeviceEnumerator GUID: 0x%08X-0x%04X-0x%04X-0x%02X-0x%02X-0x%02X-0x%02X-0x%02X-0x%02X-0x%02X-0x%02X\n",
		CLSID_MMDeviceEnumerator.Data1, CLSID_MMDeviceEnumerator.Data2, CLSID_MMDeviceEnumerator.Data3,
		CLSID_MMDeviceEnumerator.Data4[ 0 ], CLSID_MMDeviceEnumerator.Data4[ 1 ],
    	CLSID_MMDeviceEnumerator.Data4[ 2 ], CLSID_MMDeviceEnumerator.Data4[ 3 ],
    	CLSID_MMDeviceEnumerator.Data4[ 4 ], CLSID_MMDeviceEnumerator.Data4[ 5 ],
    	CLSID_MMDeviceEnumerator.Data4[ 6 ], CLSID_MMDeviceEnumerator.Data4[ 7 ]
	);
	printf("IID IMMDeviceEnumerator GUID: 0x%08X-0x%04X-0x%04X-0x%02X-0x%02X-0x%02X-0x%02X-0x%02X-0x%02X-0x%02X-0x%02X\n",
		IID_IMMDeviceEnumerator.Data1, IID_IMMDeviceEnumerator.Data2, IID_IMMDeviceEnumerator.Data3,
		IID_IMMDeviceEnumerator.Data4[ 0 ], IID_IMMDeviceEnumerator.Data4[ 1 ],
    	IID_IMMDeviceEnumerator.Data4[ 2 ], IID_IMMDeviceEnumerator.Data4[ 3 ],
    	IID_IMMDeviceEnumerator.Data4[ 4 ], IID_IMMDeviceEnumerator.Data4[ 5 ],
    	IID_IMMDeviceEnumerator.Data4[ 6 ], IID_IMMDeviceEnumerator.Data4[ 7 ]
	);
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
	ErrorCode = WASAPIState->Client->lpVtbl->Initialize(WASAPIState->Client, AUDCLNT_SHAREMODE_SHARED, StreamFlags, DefaultInterval, DefaultInterval, WASAPIState->MixFormat, &CNFA_GUID);
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

static void WASAPIPrintAllDeviceLists()
{
	WASAPIPrintDeviceList(eRender);
	WASAPIPrintDeviceList(eCapture);
}

static void WASAPIPrintDeviceList(EDataFlow dataFlow)
{
	WASAPIPRINT(strcat((dataFlow == eCapture ? "Capture" : "Render"), " Devices:"));
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

		// TODO: Figure out how to get device name.
		//ErrorCode = Properties->lpVtbl->GetValue(Properties, ???, &Variant);

		LPWSTR DeviceFriendlyName = L"[Name Retrieval Failed]";

		printf("[WASAPI] [%d]: \"%ls\" = \"%ls\"\n", DeviceIndex, DeviceFriendlyName, DeviceID);
	}

}

void* ProcessEventAudioIn(void* stateObj)
{
	struct CNFADriverWASAPI* state = (struct CNFADriverWASAPI*)stateObj;
	HRESULT ErrorCode;
	UINT32 PacketLength;

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
			FLOAT* DataAsFloat = (FLOAT*)DataBuffer; // Count = Size/4
			UINT16* AudioData = malloc((FramesAvailable * state->MixFormat->nChannels) * 2); // Count = Size / 4
			//memcpy(&AudioData, &DataBuffer, Size);

			//printf("[WASAPI] Frame count %d, total pack size %d. BPF is %d. Length was expected to be %d earlier.\n", FramesAvailable, Size, state->BytesPerFrame, PacketLength);

			for (INT32 i = 0; i < Size / 4; i++)
			{
				//printf("[WASAPI] Looking at data point %d. Float is %f.\n", i, DataAsFloat[i]);
				AudioData[i] = (SHORT)(DataAsFloat[i] * 32767.5F);
			}

			ErrorCode = state->CaptureClient->lpVtbl->ReleaseBuffer(state->CaptureClient, FramesAvailable);
			if (FAILED(ErrorCode)) { WASAPIERROR(ErrorCode, "Failed to release audio buffer."); }
			else { Released = TRUE; }

			//printf("[WASAPI] We got data! \\o/ Size %d\n", Size);

			//printf("[WASAPI] Doing callback on address %d with data at address %d of length %d\n", WASAPIState->Callback, AudioData, (FramesAvailable * state->MixFormat->nChannels));

			WASAPIState->Callback((struct CNFADriver*)WASAPIState, AudioData, 0, (FramesAvailable * state->MixFormat->nChannels) / 2, 0);

			//printf("[WASAPI] Callback done.\n");

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

	state->StreamReady = FALSE;
	return 0;
}

// This looks like it is essentially a constructor.
// callback: ???
// sessionName: How your session will appear to the user if you play audio.
// reqSampleRate: The sample rate you'd like to request. See note below.
// reqChannelsIn: The number of audio channels you'd like to request for the audio input. See note below.
// reqChannelsOut: The number of audio channels you'd like to request for the audio output. See note below.
// sugBufferSize: ???
// inputDevice: The device you want to receive audio from. Loopback is supported, so this can be either a capture or render device.
//				To get the default render device, specify "defaultRender"
// 				To get the default capture device, specify "defaultCapture"
//				A device ID as presented by WASAPI can be specified, regardless of what type it is.
//				If you do not wish to receive audio, specify null.
// outputDevice: The device you want to output audio to. OUTPUT IS NOT IMPLEMENTED.
void* InitCNFAWASAPIDriver(CNFACBType callback, const char* sessionName, int reqSampleRate, int reqChannelsIn, int reqChannelsOut, int sugBufferSize, const char* inputDevice, const char* outputDevice)
{
	struct CNFADriverWASAPI * InitState = malloc(sizeof(struct CNFADriverWASAPI));
	InitState->CloseFn = CloseCNFAWASAPI;
	InitState->StateFn = CNFAStateWASAPI;
	InitState->Callback = callback;
	InitState->SampleRate = reqSampleRate;
	InitState->ChannelCountIn = reqChannelsIn;
	InitState->ChannelCountOut = reqChannelsOut;
	
	InitState->SessionName = sessionName;

	WASAPIPRINT("WASAPI Init");

	return StartWASAPIDriver(InitState);
}

// This is the equivalent of a static constructor that also calls the base constructor.
REGISTER_CNFA(cnfa_wasapi, 9, "WASAPI", InitCNFAWASAPIDriver);