//Copyright 2015-2020 <>< Charles Lohr under the MIT/x11, NewBSD or ColorChord License.  You choose.

//This file is really rough.  Full duplex doesn't seem to work hardly at all.


#include "CNFA.h"
#include "os_generic.h"
#include <stdlib.h>

#include <pulse/simple.h>
#include <pulse/pulseaudio.h>
#include <pulse/error.h>
#include <stdio.h>
#include <string.h>

#define BUFFERSETS 3


//from http://www.freedesktop.org/wiki/Software/PulseAudio/Documentation/Developer/Clients/Samples/AsyncPlayback/
//also http://maemo.org/api_refs/5.0/5.0-final/pulseaudio/pacat_8c-example.html


struct CNFADriverPulse
{
	void (*CloseFn)( void * object );
	int (*StateFn)( void * object );
	CNFACBType callback;
	short channelsPlay;
	short channelsRec;
	int spsPlay;
	int spsRec;
	void * opaque;

	char * sourceNamePlay;
	char * sourceNameRec;

	og_thread_t thread;
 	pa_stream *  	play;
 	pa_stream *  	rec;
	pa_context *  pa_ctx;
	pa_mainloop *pa_ml;
	int pa_ready;
	int buffer;
	//More fields may exist on a per-sound-driver basis
};




int CNFAStatePulse( void * v )
{
	struct CNFADriverPulse * soundobject = (struct CNFADriverPulse *)v;
	return ((soundobject->play)?2:0) | ((soundobject->rec)?1:0);
}

void CloseCNFAPulse( void * v )
{
	struct CNFADriverPulse * r = (struct CNFADriverPulse *)v;
	if( r )
	{
		if( r->play )
		{
			pa_stream_unref (r->play);
			r->play = 0;
		}

		if( r->rec )
		{
			pa_stream_unref (r->rec);
			r->rec = 0;
		}
		OGUSleep(2000);
		OGCancelThread( r->thread );


		if( r->sourceNamePlay ) free( r->sourceNamePlay );
		if( r->sourceNameRec ) free( r->sourceNameRec );
		free( r );
	}
}

static void * CNFAThread( void * v )
{
	struct CNFADriverPulse * r = (struct CNFADriverPulse*)v;
	while(1)
	{
		pa_mainloop_iterate( r->pa_ml, 1, NULL );
	}
	return 0;
}

static void stream_request_cb(pa_stream *s, size_t length, void *userdata)
{
	struct CNFADriverPulse * r = (struct CNFADriverPulse*)userdata;
	if( !r->play )
	{
		return;
	}
	short bufp[length*r->channelsPlay/sizeof(short)];
	r->callback( (struct CNFADriver*)r, bufp, 0, length/(sizeof(short)*r->channelsPlay), 0 );
	pa_stream_write(r->play, &bufp[0], length, NULL, 0LL, PA_SEEK_RELATIVE);
}


static void stream_record_cb(pa_stream *s, size_t length, void *userdata)
{
	struct CNFADriverPulse * r = (struct CNFADriverPulse*)userdata;

	uint16_t * bufr;

    if (pa_stream_peek(r->rec, (const void**)&bufr, &length) < 0) {
        fprintf(stderr, ("pa_stream_peek() failed: %s\n"), pa_strerror(pa_context_errno(r->pa_ctx)));
        return;
    }

	short * buffer;
    buffer = (short*)pa_xmalloc(length);
    memcpy(buffer, bufr, length);
	pa_stream_drop(r->rec);
	r->callback( (struct CNFADriver*)r, 0, buffer, 0, length/(sizeof(short)*r->channelsRec) );
	pa_xfree( buffer );
}



static void stream_underflow_cb(pa_stream *s, void *userdata) {
  printf("underflow\n");
}


void pa_state_cb(pa_context *c, void *userdata) {
	pa_context_state_t state;
	int *pa_ready = (int*)userdata;
	state = pa_context_get_state(c);
	switch  (state) {
		// These are just here for reference
		case PA_CONTEXT_UNCONNECTED:
		case PA_CONTEXT_CONNECTING:
		case PA_CONTEXT_AUTHORIZING:
		case PA_CONTEXT_SETTING_NAME:
		default:
			break;
		case PA_CONTEXT_FAILED:
		case PA_CONTEXT_TERMINATED:
			*pa_ready = 2;
			break;
		case PA_CONTEXT_READY:
			*pa_ready = 1;
		break;
	}
}


void * InitCNFAPulse( CNFACBType cb, const char * your_name, int reqSPSPlay, int reqSPSRec, int reqChannelsPlay, int reqChannelsRec, int sugBufferSize, const char * outputSelect, const char * inputSelect, void * opaque )
{
	static pa_buffer_attr bufattr;
	static pa_sample_spec ss;
	int error;
	pa_mainloop_api *pa_mlapi;
	const char * title = your_name;

	struct CNFADriverPulse * r = (struct CNFADriverPulse *)malloc( sizeof( struct CNFADriverPulse ) );

	r->pa_ml = pa_mainloop_new();
	if( !r->pa_ml )
	{
		fprintf( stderr, "Failed to initialize pa_mainloop_new()\n" );
		goto fail;
	}

	pa_mlapi = pa_mainloop_get_api(r->pa_ml);
	if( !pa_mlapi )
	{
		fprintf( stderr, "Failed to initialize pa_mainloop_get_api()\n" );
		goto fail;
	}

	r->pa_ctx = pa_context_new(pa_mlapi, title );
	pa_context_connect(r->pa_ctx, NULL, PA_CONTEXT_NOFLAGS, NULL);

	//TODO: pa_context_set_state_callback

	r->CloseFn = CloseCNFAPulse;
	r->StateFn = CNFAStatePulse;
	r->callback = cb;
	r->opaque = opaque;
	r->spsPlay = reqSPSPlay;
	r->spsRec = reqSPSRec;
	r->channelsPlay = reqChannelsPlay;
	r->channelsRec = reqChannelsRec;
	r->sourceNamePlay = outputSelect?strdup(outputSelect):0;
	r->sourceNameRec = inputSelect?strdup(inputSelect):0;

	r->play = 0;
	r->rec = 0;
	r->buffer = sugBufferSize;

	printf ("Pulse: from: [O/I] %s/%s (%s) / (%d,%d)x(%d,%d) (%d)\n", r->sourceNamePlay, r->sourceNameRec, title, r->spsPlay, r->spsRec, r->channelsPlay, r->channelsRec, r->buffer );

	memset( &ss, 0, sizeof( ss ) );

	ss.format = PA_SAMPLE_S16NE;

	r->pa_ready = 0;
	pa_context_set_state_callback(r->pa_ctx, pa_state_cb, &r->pa_ready);

	while (r->pa_ready == 0)
	{
		pa_mainloop_iterate(r->pa_ml, 1, NULL);
	}

	int bufbytes = r->buffer * sizeof(short) * r->channelsRec;

	if( r->channelsPlay )
	{
		ss.channels = r->channelsPlay;
		ss.rate = r->spsPlay;

		if (!(r->play = pa_stream_new(r->pa_ctx, "Play", &ss, NULL))) {
			error = -3; //XXX ??? TODO
			fprintf(stderr, __FILE__": pa_simple_new() failed: %s\n", pa_strerror(error));
			goto fail;
		}

		pa_stream_set_underflow_callback(r->play, stream_underflow_cb, NULL);
		pa_stream_set_write_callback(r->play, stream_request_cb, r );

		bufattr.fragsize = (uint32_t)-1;
		bufattr.maxlength = bufbytes*3; //XXX TODO Consider making this -1
		bufattr.minreq = 0;
		bufattr.prebuf =  (uint32_t)-1;
		bufattr.tlength = bufbytes*3;
		int ret = pa_stream_connect_playback(r->play, r->sourceNamePlay, &bufattr,
				                    // PA_STREAM_INTERPOLATE_TIMING
				                    // |PA_STREAM_ADJUST_LATENCY //Some servers don't like the adjust_latency flag.
				                    // |PA_STREAM_AUTO_TIMING_UPDATE, NULL, NULL);
					PA_STREAM_NOFLAGS, NULL, NULL );
		if( ret < 0 )
		{
			fprintf(stderr, __FILE__": (PLAY) pa_stream_connect_playback() failed: %s\n", pa_strerror(ret));
			goto fail;
		}

	}

	if( r->channelsRec )
	{
		ss.channels = r->channelsRec;
		ss.rate = r->spsRec;

		if (!(r->rec = pa_stream_new(r->pa_ctx, "Record", &ss, NULL))) {
			error = -3; //XXX ??? TODO
			fprintf(stderr, __FILE__": pa_simple_new() failed: %s\n", pa_strerror(error));
			goto fail;
		}

		pa_stream_set_read_callback(r->rec, stream_record_cb, r );

		bufattr.fragsize = bufbytes;
		bufattr.maxlength = (uint32_t)-1;//(uint32_t)-1; //XXX: Todo, should this be low?
		bufattr.minreq = bufbytes;
		bufattr.prebuf = (uint32_t)-1;
		bufattr.tlength = bufbytes*3;
		int ret = pa_stream_connect_record(r->rec, r->sourceNameRec, &bufattr, 
//							       PA_STREAM_INTERPOLATE_TIMING
			                       PA_STREAM_ADJUST_LATENCY  //Some servers don't like the adjust_latency flag.
//		                     	PA_STREAM_AUTO_TIMING_UPDATE
//								PA_STREAM_NOFLAGS
				);

printf( "PA REC RES: %d\n", ret );

		if( ret < 0 )
		{
			fprintf(stderr, __FILE__": (REC) pa_stream_connect_playback() failed: %s\n", pa_strerror(ret));
			goto fail;
		}
	}

	printf( "Pulse initialized.\n" );


	r->thread = OGCreateThread( CNFAThread, r );


	if( r->play )
	{
		stream_request_cb( r->play, bufbytes, r );
		stream_request_cb( r->play, bufbytes, r );
	}

	return r;

fail:
	if( r )
	{
		if( r->play ) pa_xfree (r->play);
		if( r->rec ) pa_xfree (r->rec);
		free( r );
	}
	return 0;
}



REGISTER_CNFA( PulseCNFA, 11, "PULSE", InitCNFAPulse );


