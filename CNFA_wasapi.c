#include "CNFA.h"
//#include <audioclient.h>
//#include <mmdeviceapi.h>
#include "CNFA_wasapi_utils.h"
#include "windows.h"

#define WASAPIPRINT(message) (printf("[WASAPI] %s", message))
#define WASAPIERROR(error, message) (printf("[WASAPI][ERR] %s HRESULT: 0x&X", message, error))

// Forward Declarations
void CloseCNFAWASAPI(void* stateObj);
int CNFAStateWASAPI(void* object);
static struct CNFADriverWASAPI* StartWASAPIDriver(struct CNFADriverWASAPI* initState);
static IMMDevice* WASAPIGetDefaultDevice(BOOL isCapture);
static void WASAPIPrintDeviceList_Simple();
static void WASAPIPrintDeviceList(EDataFlow dataFlow);
void* InitCNFAWASAPIDriver(CNFACBType callback, const char* sessionName,
                           int reqSampleRate,
						   int reqChannelsIn, int reqChannelsOut, int sugBufferSize,
						   const char* inputDevice, const char* outputDevice);

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
	short ChannelCountOut;
	short ChannelCountIn;
	int SampleRate;
	void* Opaque; // TODO: WTF is this?

	const char* SessionName;

	IMMDeviceEnumerator* DeviceEnumerator;
	IAudioClient* Client;
	IAudioCaptureClient* CaptureClient;
	WAVEFORMATEX* MixFormat;
};

// This is where the driver's current state is stored.
static struct CNFADriverWASAPI* State;

void CloseCNFAWASAPI(void* stateObj)
{
	struct CNFADriverWASAPI* state = (struct CNFADriverWASAPI*)stateObj;
	if(state)
	{
		// TODO: Cleanup stuff.
		free(stateObj);
	}
}

int CNFAStateWASAPI(void* object)
{
	return 0;
}

static struct CNFADriverWASAPI* StartWASAPIDriver(struct CNFADriverWASAPI* initState)
{
	State = initState;

	HRESULT ErrorCode;
	ErrorCode = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, 
								 &IID_IMMDeviceEnumerator, (void**)&(State->DeviceEnumerator));
	if (FAILED(ErrorCode)) {
		WASAPIERROR(ErrorCode, "Failed to get device enumerator. ");
		return State;
	}

	WASAPIPrintDeviceList_Simple();

	IMMDevice* Device;
	Device = WASAPIGetDefaultDevice(FALSE);

	return State;
}

static IMMDevice* WASAPIGetDefaultDevice(BOOL isCapture)
{
	HRESULT ErrorCode;
	IMMDevice* Device;
	ErrorCode = State->DeviceEnumerator->lpVtbl
		->GetDefaultAudioEndpoint(State->DeviceEnumerator, 
			isCapture ? eCapture : eRender, eMultimedia, &Device);
	if (FAILED(ErrorCode))
	{
		WASAPIERROR(ErrorCode, "Failed to get default device.");
		return NULL;
	}
	return Device;
}

static void WASAPIPrintDeviceList_Simple()
{
	WASAPIPrintDeviceList(eRender);
	WASAPIPrintDeviceList(eCapture);
}

static void WASAPIPrintDeviceList(EDataFlow dataFlow)
{
	WASAPIPRINT(strcat((dataFlow == eCapture ? "Capture" : "Render"), " Devices:"));
	IMMDeviceCollection* Devices;
	HRESULT ErrorCode = State->DeviceEnumerator->lpVtbl
		->EnumAudioEndpoints(State->DeviceEnumerator, dataFlow,
			DEVICE_STATE_ACTIVE, &Devices);
	if (FAILED(ErrorCode)) { 
		WASAPIERROR(ErrorCode, "Failed to get audio endpoints.");
		return;
	}

	UINT32 DeviceCount;
	ErrorCode = Devices->lpVtbl->GetCount(Devices, &DeviceCount);
	if (FAILED(ErrorCode)) { 
		WASAPIERROR(ErrorCode, "Failed to get audio endpoint count.");
		return; 
	}

	for (UINT32 DeviceIndex = 0; DeviceIndex < DeviceCount; DeviceIndex++)
	{
		IMMDevice* Device;
		ErrorCode = Devices->lpVtbl->Item(Devices, DeviceIndex, &Device);
		if (FAILED(ErrorCode)) {
			WASAPIERROR(ErrorCode, "Failed to get audio device.");
			continue;
		}

		LPWSTR* DeviceID;
		ErrorCode = Device->lpVtbl->GetId(Device, &DeviceID);
		if (FAILED(ErrorCode)) {
			WASAPIERROR(ErrorCode, "Failed to get audio device ID.");
			continue;
		}

		IPropertyStore* Properties;
		ErrorCode = Device->lpVtbl->OpenPropertyStore(Device, STGM_READ, &Properties);
		if (FAILED(ErrorCode)) {
			WASAPIERROR(ErrorCode, "Failed to get device properties.");
			continue;
		}
		
		LPWSTR DeviceFriendlyName = L"[Name Retrieval Failed]";
		DWORD PropertyCount;
		ErrorCode = Properties->lpVtbl->GetCount(Properties, &PropertyCount);
		if (FAILED(ErrorCode)) {
			WASAPIERROR(ErrorCode, "Failed to get device property count.");
			continue;
		}
		
		// TODO: Read property store for friendly name.

		printf("[WASAPI] [%d]: \"%ls\" = \"%ls\"", DeviceIndex, DeviceFriendlyName, DeviceID);
	}

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
REGISTER_CNFA(WASAPICNFA, 9, "WASAPI", InitCNFAWASAPIDriver);