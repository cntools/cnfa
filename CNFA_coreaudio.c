//Copyright 2015-2020 <>< Charles Lohr under the MIT/x11, NewBSD or ColorChord License.  You choose.

// TODO:
// - Test recording
// - Device selection
// - dylib build

#include "CNFA.h"
#include "os_generic.h"
#include <stdlib.h>

#include <CoreAudio/CoreAudio.h>
#include <AudioToolbox/AudioQueue.h>
#include <stdio.h>
#include <string.h>

#define BUFFERSETS 3


// https://developer.apple.com/library/archive/documentation/MusicAudio/Conceptual/CoreAudioOverview/CoreAudioEssentials/CoreAudioEssentials.html


struct CNFADriverCoreAudio
{
	void (*CloseFn)( void * object );
	int (*StateFn)( void * object );
	CNFACBType callback;
	short channelsPlay;
	short channelsRec;
	int spsPlay;
	int spsRec;
	void * opaque;

	// CoreAudio-specific fields
	char * sourceNamePlay;
	char * sourceNameRec;

	AudioQueueRef   play;
	AudioQueueRef   rec;

	AudioQueueBufferRef playBuffers[BUFFERSETS];
	AudioQueueBufferRef recBuffers[BUFFERSETS];

	int buffer;
};

int CNFAStateCoreAudio( void * v )
{
	struct CNFADriverCoreAudio * soundobject = (struct CNFADriverCoreAudio *)v;
	return ((soundobject->play)?2:0) | ((soundobject->rec)?1:0);
}

void CloseCNFACoreAudio( void * v )
{
	struct CNFADriverCoreAudio * r = (struct CNFADriverCoreAudio *)v;
	if( r )
	{
		if( r->play )
		{
			AudioQueueDispose(r->play, true);
			r->play = 0;
		}

		if( r->rec )
		{
			AudioQueueDispose(r->rec, true);
			r->rec = 0;
		}

		if( r->sourceNamePlay ) free( r->sourceNamePlay );
		if( r->sourceNameRec ) free( r->sourceNameRec );
		free( r );
	}
}

static void CoreAudioOutputCallback(void *userdata, AudioQueueRef inAQ, AudioQueueBufferRef inBuffer)
{
	// Write our data to the input buffer
	struct CNFADriverCoreAudio * r = (struct CNFADriverCoreAudio*)userdata;
	if (!r->play)
	{
		return;
	}

	r->callback((struct CNFADriver*)r, (short*)inBuffer->mAudioData, NULL, inBuffer->mAudioDataBytesCapacity / sizeof(short) / r->channelsPlay, 0);
	// Assume the buffer was completely filled?
	inBuffer->mAudioDataByteSize = inBuffer->mAudioDataBytesCapacity;

	// I think this isn't necessary for non-compressed audio formats
	if (inBuffer->mPacketDescriptionCapacity > 0)
	{
		inBuffer->mPacketDescriptionCount = 1;
		inBuffer->mPacketDescriptions[0].mDataByteSize = inBuffer->mAudioDataBytesCapacity;
		inBuffer->mPacketDescriptions[0].mStartOffset = 0;
		inBuffer->mPacketDescriptions[0].mVariableFramesInPacket = 0;
	}

	// Actually send the buffer to be played!
	AudioQueueEnqueueBuffer(inAQ, inBuffer, 0, NULL);
}

static void CoreAudioInputCallback(void *userdata, AudioQueueRef inAQ, AudioQueueBufferRef inBuffer, const AudioTimeStamp *inStartTime, UInt32 inNumberPacketDescriptions, const AudioStreamPacketDescription *inPacketDescs)
{
	// Read our data from the output buffer
	struct CNFADriverCoreAudio * r = (struct CNFADriverCoreAudio*)userdata;
	r->callback((struct CNFADriver*)r, NULL, (short*)inBuffer->mAudioData, 0, inBuffer->mAudioDataByteSize / sizeof(short));
}

void * InitCNFACoreAudio( CNFACBType cb, const char * your_name, int reqSPSPlay, int reqSPSRec, int reqChannelsPlay, int reqChannelsRec, int sugBufferSize, const char * outputSelect, const char * inputSelect, void * opaque )
{
	const char * title = your_name;

	struct CNFADriverCoreAudio * r = (struct CNFADriverCoreAudio *)malloc( sizeof( struct CNFADriverCoreAudio ) );

	r->CloseFn = CloseCNFACoreAudio;
	r->StateFn = CNFAStateCoreAudio;
	r->callback = cb;
	r->opaque = opaque;
	r->spsPlay = reqSPSPlay;
	r->spsRec = reqSPSRec;
	r->channelsPlay = reqChannelsPlay;
	r->channelsRec = reqChannelsRec;
	r->sourceNamePlay = outputSelect?strdup(outputSelect):0;
	r->sourceNameRec = inputSelect?strdup(inputSelect):0;

	memset(r->playBuffers, 0, sizeof(r->playBuffers));
	memset(r->recBuffers, 0, sizeof(r->recBuffers));

	r->play = 0;
	r->rec = 0;
	r->buffer = sugBufferSize;

	printf ("CoreAudio: from: [O/I] %s/%s (%s) / (%d,%d)x(%d,%d) (%d)\n", r->sourceNamePlay, r->sourceNameRec, title, r->spsPlay, r->spsRec, r->channelsPlay, r->channelsRec, r->buffer );

	int bufBytesPlay = r->buffer * sizeof(short) * r->channelsPlay;
	int bufBytesRec = r->buffer * sizeof(short) * r->channelsRec;

	if( r->channelsPlay )
	{
		AudioStreamBasicDescription playDesc = {0};
		playDesc.mFormatID = kAudioFormatLinearPCM;
		playDesc.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
		playDesc.mSampleRate = r->spsPlay;
		playDesc.mBitsPerChannel = 8 * sizeof(short);
		playDesc.mChannelsPerFrame = r->channelsPlay;
		// Bytes per channel, multiplied by number of channels
		playDesc.mBytesPerFrame = r->channelsPlay * (playDesc.mBitsPerChannel / 8);
		// Always 1 for uncompressed audio
		playDesc.mFramesPerPacket = 1;
		playDesc.mBytesPerPacket = playDesc.mBytesPerFrame * playDesc.mFramesPerPacket;
		playDesc.mReserved = 0;

		OSStatus result = AudioQueueNewOutput(&playDesc, CoreAudioOutputCallback, (void*)r, NULL /* Default run loop*/, NULL /* Equivalent to kCFRunLoopCommonModes */, 0 /* flags, reserved*/, &r->play);

		if( 0 != result )
		{
			fprintf(stderr, __FILE__": (PLAY) AudioQueueNewOutput() failed: %s\n", strerror(result));
			goto fail;
		}

		for (int i = 0; i < BUFFERSETS; i++)
		{
			result = AudioQueueAllocateBuffer(r->play, bufBytesPlay, &r->playBuffers[i]);

			if (0 != result)
			{
				fprintf(stderr, __FILE__": (PLAY) AudioQueueNewOutput() failed: %s\n", strerror(result));
				goto fail;
			}

			// Prefill buffer
			CoreAudioOutputCallback(r, r->play, r->playBuffers[i]);
		}

		AudioQueueStart(r->play, NULL);
	}

	if( r->channelsRec )
	{
		AudioStreamBasicDescription recDesc = {0};
		recDesc.mFormatID = kAudioFormatLinearPCM;
		recDesc.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
		recDesc.mSampleRate = r->spsRec;
		recDesc.mBitsPerChannel = 8 * sizeof(short);
		recDesc.mChannelsPerFrame = r->channelsRec;
		// Bytes per channel, multiplied by number of channels
		recDesc.mBytesPerFrame = recDesc.mChannelsPerFrame * (recDesc.mBitsPerChannel / 8);
		// Always 1 for uncompressed audio
		recDesc.mFramesPerPacket = 1;
		recDesc.mBytesPerPacket = recDesc.mBytesPerFrame * recDesc.mFramesPerPacket;

		OSStatus result = AudioQueueNewInput(&recDesc, CoreAudioInputCallback, (void*)r, NULL, NULL, 0, &r->rec);

		if (0 != result)
		{
			fprintf(stderr, __FILE__": (RECORD) AudioQueueNewInput() failed: %s\n", strerror(result));
			goto fail;
		}

		for (int i = 0; i < BUFFERSETS; i++)
		{
			result = AudioQueueAllocateBuffer(r->rec, bufBytesRec, &r->recBuffers[i]);

			if (0 != result)
			{
				fprintf(stderr, __FILE__": (RECORD) AudioQueueNewOutput() failed: %s\n", strerror(result));
				goto fail;
			}

			// No buffer prefill for input
		}

		AudioQueueStart(r->rec, NULL);
	}

	printf( "CoreAudio initialized.\n" );

	return r;

fail:
	if( r )
	{
		if( r->play )
		{
			// Buffers are automatically freed with the queue
			AudioQueueDispose (r->play, true);
		}

		if( r->rec )
		{
			// Buffers are automatically freed with the queue
			AudioQueueDispose (r->rec, true);
		}
		free( r );
	}
	return 0;
}



REGISTER_CNFA( CoreAudioCNFA, 10, "COREAUDIO", InitCNFACoreAudio );
