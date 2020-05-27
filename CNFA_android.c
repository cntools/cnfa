//Copyright 2019-2020 <>< Charles Lohr under the ColorChord License, MIT/x11 license or NewBSD Licenses.
// This was originally to be used with rawdrawandroid

#include "CNFA.h"
#include "os_generic.h"
#include <pthread.h> //Using android threads not os_generic threads.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//based on https://github.com/android/ndk-samples/blob/master/native-audio/app/src/main/cpp/native-audio-jni.c

// for native audio
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#include <android_native_app_glue.h>
#include <jni.h>
#include <native_activity.h>

struct CNFADriverAndroid
{
	//Standard header - must remain.
	void (*CloseFn)( void * object );
	int (*StateFn)( void * object );
	CNFACBType callback;
	short channelsPlay;
	short channelsRec;
	int spsPlay;
	int spsRec;
	void * opaque;

	int buffsz;

	SLObjectItf engineObject;
	SLEngineItf engineEngine;
	SLRecordItf recorderRecord;
	SLObjectItf recorderObject;

	SLPlayItf playerPlay;
	SLObjectItf playerObject;
	SLObjectItf outputMixObject;
 
	SLAndroidSimpleBufferQueueItf recorderBufferQueue;
	SLAndroidSimpleBufferQueueItf playerBufferQueue;
	//unsigned recorderSize;

	int recorderBufferSizeBytes;
	int playerBufferSizeBytes;
	short * recorderBuffer;
	short * playerBuffer;
};


void bqRecorderCallback(SLAndroidSimpleBufferQueueItf bq, void *context)
{
	struct CNFADriverAndroid * r = (struct CNFADriverAndroid*)context;
	r->callback( (struct CNFADriver*)r, 0, r->recorderBuffer, 0, r->buffsz/(sizeof(short)*r->channelsRec) );
	(*r->recorderBufferQueue)->Enqueue( r->recorderBufferQueue, r->recorderBuffer, r->recorderBufferSizeBytes/(r->channelsRec*sizeof(short)) );
}

void bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *context)
{
	struct CNFADriverAndroid * r = (struct CNFADriverAndroid*)context;
	r->callback( (struct CNFADriver*)r, r->playerBuffer, 0, r->buffsz/(sizeof(short)*r->channelsPlay), 0 );
	(*r->playerBufferQueue)->Enqueue( r->playerBufferQueue, r->playerBuffer, r->playerBufferSizeBytes/(r->channelsPlay*sizeof(short)));
}

static struct CNFADriverAndroid* InitAndroidDriver( struct CNFADriverAndroid * r )
{
	SLresult result;
	printf( "Starting InitAndroidDriver\n" );
	
	// create engine
	result = slCreateEngine(&r->engineObject, 0, NULL, 0, NULL, NULL);
	assert(SL_RESULT_SUCCESS == result);
	(void)result;

	// realize the engine
	result = (*r->engineObject)->Realize(r->engineObject, SL_BOOLEAN_FALSE);
	assert(SL_RESULT_SUCCESS == result);
	(void)result;

	// get the engine interface, which is needed in order to create other objects
	result = (*r->engineObject)->GetInterface(r->engineObject, SL_IID_ENGINE, &r->engineEngine);
	assert(SL_RESULT_SUCCESS == result);
	(void)result;


	///////////////////////////////////////////////////////////////////////////////////////////////////////
	if( r->channelsPlay )
	{
		printf("create output mix");

		SLDataFormat_PCM format_pcm ={
			SL_DATAFORMAT_PCM,
			r->channelsPlay,
			r->spsPlay*1000,
			SL_PCMSAMPLEFORMAT_FIXED_16,
			SL_PCMSAMPLEFORMAT_FIXED_16,
			(r->channelsPlay==1)?SL_SPEAKER_FRONT_CENTER:3,
			SL_BYTEORDER_LITTLEENDIAN,
		};
		SLDataLocator_AndroidSimpleBufferQueue loc_bq_play = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2};
		SLDataSource source = {&loc_bq_play, &format_pcm};
		const SLInterfaceID ids[1] = {SL_IID_VOLUME};
		const SLboolean req[1] = {SL_BOOLEAN_TRUE};
		const SLInterfaceID id[1] = {SL_IID_ANDROIDSIMPLEBUFFERQUEUE};

		result = (*r->engineEngine)->CreateOutputMix(r->engineEngine, &r->outputMixObject, 0, ids, req);
		result = (*r->outputMixObject)->Realize(r->outputMixObject, SL_BOOLEAN_FALSE);

		SLDataLocator_OutputMix loc_outmix = { SL_DATALOCATOR_OUTPUTMIX, r->outputMixObject };
		SLDataSink sink;
		sink.pFormat = &format_pcm;
		sink.pLocator = &loc_outmix;

		// create audio player
		result = (*r->engineEngine)->CreateAudioPlayer(r->engineEngine, &r->playerObject, &source, &sink, 1, id, req);
		if (SL_RESULT_SUCCESS != result) {
			printf( "CreateAudioPlayer failed\n" );
			return JNI_FALSE;
		}


		// realize the audio player
		result = (*r->playerObject)->Realize(r->playerObject, SL_BOOLEAN_FALSE);
		if (SL_RESULT_SUCCESS != result) {
			printf( "AudioPlayer Realize failed: %d\n", result );
			return JNI_FALSE;
		}

		// get the player interface
		result = (*r->playerObject)->GetInterface(r->playerObject, SL_IID_PLAY, &r->playerPlay);
		assert(SL_RESULT_SUCCESS == result);
		(void)result;

		// get the buffer queue interface
		result = (*r->playerObject)->GetInterface(r->playerObject, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &r->playerBufferQueue);
		assert(SL_RESULT_SUCCESS == result);
		(void)result;

		// register callback on the buffer queue
		result = (*r->playerBufferQueue)->RegisterCallback(r->playerBufferQueue, bqPlayerCallback, r);
		assert(SL_RESULT_SUCCESS == result);
		(void)result;

		printf( "===================== Player init ok.\n" );
	}

	if( r->channelsRec )
	{
		// configure audio source
		SLDataLocator_IODevice loc_devI = {SL_DATALOCATOR_IODEVICE, SL_IODEVICE_AUDIOINPUT, SL_DEFAULTDEVICEID_AUDIOINPUT, NULL};
		SLDataSource audioSrc = {&loc_devI, NULL};

		// configure audio sink
		SLDataFormat_PCM format_pcm ={
			SL_DATAFORMAT_PCM,
			r->channelsRec, 
			r->spsRec*1000,
			SL_PCMSAMPLEFORMAT_FIXED_16,
			SL_PCMSAMPLEFORMAT_FIXED_16,
			(r->channelsRec==1)?SL_SPEAKER_FRONT_CENTER:3,
			SL_BYTEORDER_LITTLEENDIAN,
		};
		SLDataLocator_AndroidSimpleBufferQueue loc_bq = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2};
		SLDataSink audioSnk = {&loc_bq, &format_pcm};


		const SLInterfaceID id[1] = {SL_IID_ANDROIDSIMPLEBUFFERQUEUE};
		const SLboolean req[1] = {SL_BOOLEAN_TRUE};

		result = (*r->engineEngine)->CreateAudioRecorder(r->engineEngine, &r->recorderObject, &audioSrc, &audioSnk, 1, id, req);
		if (SL_RESULT_SUCCESS != result) {
			printf( "CreateAudioRecorder failed\n" );
			return JNI_FALSE;
		}

		// realize the audio recorder
		result = (*r->recorderObject)->Realize(r->recorderObject, SL_BOOLEAN_FALSE);
		if (SL_RESULT_SUCCESS != result) {
			printf( "AudioRecorder Realize failed: %d\n", result );
			return JNI_FALSE;
		}

		// get the record interface
		result = (*r->recorderObject)->GetInterface(r->recorderObject, SL_IID_RECORD, &r->recorderRecord);
		assert(SL_RESULT_SUCCESS == result);
		(void)result;

		// get the buffer queue interface
		result = (*r->recorderObject)->GetInterface(r->recorderObject, SL_IID_ANDROIDSIMPLEBUFFERQUEUE,	&r->recorderBufferQueue);
		assert(SL_RESULT_SUCCESS == result);
		(void)result;

		// register callback on the buffer queue
		result = (*r->recorderBufferQueue)->RegisterCallback(r->recorderBufferQueue, bqRecorderCallback, r);
		assert(SL_RESULT_SUCCESS == result);
		(void)result;
	}


	if( r->playerPlay )
	{
		result = (*r->playerPlay)->SetPlayState(r->playerPlay, SL_PLAYSTATE_STOPPED);
		assert(SL_RESULT_SUCCESS == result); (void)result;
		result = (*r->playerBufferQueue)->Clear(r->playerBufferQueue);
		assert(SL_RESULT_SUCCESS == result); (void)result;
		r->playerBuffer = malloc( r->playerBufferSizeBytes );
		memset( r->playerBuffer, 0, r->playerBufferSizeBytes );
		result = (*r->playerBufferQueue)->Enqueue(r->playerBufferQueue, r->playerBuffer, r->playerBufferSizeBytes );
		assert(SL_RESULT_SUCCESS == result); (void)result;
		result = (*r->playerPlay)->SetPlayState(r->playerPlay, SL_PLAYSTATE_PLAYING);
		assert(SL_RESULT_SUCCESS == result); (void)result;
	}


	if( r->recorderRecord )
	{
		result = (*r->recorderRecord)->SetRecordState(r->recorderRecord, SL_RECORDSTATE_STOPPED);
		assert(SL_RESULT_SUCCESS == result); (void)result;
		result = (*r->recorderBufferQueue)->Clear(r->recorderBufferQueue);
		assert(SL_RESULT_SUCCESS == result); (void)result;
		// the buffer is not valid for playback yet

		r->recorderBuffer = malloc( r->recorderBufferSizeBytes );

		// enqueue an empty buffer to be filled by the recorder
		// (for streaming recording, we would enqueue at least 2 empty buffers to start things off)
		result = (*r->recorderBufferQueue)->Enqueue(r->recorderBufferQueue, r->recorderBuffer, r->recorderBufferSizeBytes );
		// the most likely other result is SL_RESULT_BUFFER_INSUFFICIENT,
		// which for this code example would indicate a programming error
		assert(SL_RESULT_SUCCESS == result); (void)result;

		// start recording
		result = (*r->recorderRecord)->SetRecordState(r->recorderRecord, SL_RECORDSTATE_RECORDING);
		assert(SL_RESULT_SUCCESS == result); (void)result;
	}


	printf( "Complete Init Sound Android\n" );
	return r;
}

int CNFAStateAndroid( void * v )
{
	struct CNFADriverAndroid * soundobject = (struct CNFADriverAndroid *)v;
	return ((soundobject->recorderObject)?1:0) | ((soundobject->playerObject)?2:0);
}

void CloseCNFAAndroid( void * v )
{
	struct CNFADriverAndroid * r = (struct CNFADriverAndroid *)v;
    // destroy audio recorder object, and invalidate all associated interfaces
    if (r->recorderObject != NULL) {
        (*r->recorderObject)->Destroy(r->recorderObject);
        r->recorderObject = NULL;
        r->recorderRecord = NULL;
        r->recorderBufferQueue = NULL;
		if( r->recorderBuffer ) free( r->recorderBuffer );
    }


    if (r->playerObject != NULL) {
        (*r->playerObject)->Destroy(r->playerObject);
        r->playerObject = NULL;
        r->playerPlay = NULL;
        r->playerBufferQueue = NULL;
		if( r->playerBuffer ) free( r->playerBuffer );
    }


    // destroy engine object, and invalidate all associated interfaces
    if (r->engineObject != NULL) {
        (*r->engineObject)->Destroy(r->engineObject);
        r->engineObject = NULL;
        r->engineEngine = NULL;
    }

}


int AndroidHasPermissions(const char* perm_name);
void AndroidRequestAppPermissions(const char * perm);


void * InitCNFAAndroid( CNFACBType cb, const char * your_name, int reqSPSPlay, int reqSPSRec, int reqChannelsPlay, int reqChannelsRec, int sugBufferSize, const char * outputSelect, const char * inputSelect, void * opaque )
{
	struct CNFADriverAndroid * r = (struct CNFADriverAndroid *)malloc( sizeof( struct CNFADriverAndroid ) );
	memset( r, 0, sizeof( *r) );
	r->CloseFn = CloseCNFAAndroid;
	r->StateFn = CNFAStateAndroid;
	r->callback = cb;
	r->opaque = opaque;
	r->channelsPlay = reqChannelsPlay;
	r->channelsRec = reqChannelsRec;
	r->spsRec = reqSPSRec;
	r->spsPlay = reqSPSPlay;
	
	r->recorderBufferSizeBytes = sugBufferSize * 2 * r->channelsRec;
	r->playerBufferSizeBytes = sugBufferSize * 2 * r->channelsPlay;

	int hasperm = AndroidHasPermissions( "RECORD_AUDIO" );
	if( !hasperm )
	{
		AndroidRequestAppPermissions( "RECORD_AUDIO" );
	}
	
	r->buffsz = sugBufferSize;

	return InitAndroidDriver(r);
}

//Tricky: On Android, this can't actually run before main.  Have to manually execute it.

REGISTER_CNFA( AndroidCNFA, 10, "ANDROID", InitCNFAAndroid );

