//Copyright 2015-2020 <>< Charles Lohr under the MIT/x11, NewBSD or ColorChord License.  You choose.

#include "CNFA.h"
#include "os_generic.h"
#include <sys/audioio.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

struct CNFADriverSun
{
	void (*CloseFn)( void * object );
	int (*StateFn)( void * object );
	CNFACBType callback;
	short channelsPlay;
	short channelsRec;
	int spsPlay;
	int spsRec;
	void * opaque;

	char * devRec;
	char * devPlay;

	short * samplesRec;
	short * samplesPlay;

	og_thread_t threadPlay;
	og_thread_t threadRec;
	int bufsize;
	int playback_handle;
	int record_handle;

	char playing;
	char recording;
};

int CNFAStateSun( void * v )
{
	struct CNFADriverSun * r = (struct CNFADriverSun *)v;
	return ((r->playing)?2:0) | ((r->recording)?1:0);
}

void CloseCNFASun( void * v )
{
	struct CNFADriverSun * r = (struct CNFADriverSun *)v;
	if( r )
	{
		if( r->playback_handle != -1 ) close (r->playback_handle);
		if( r->record_handle != -1 ) close (r->record_handle);

		if( r->threadPlay ) OGJoinThread( r->threadPlay );
		if( r->threadRec ) OGJoinThread( r->threadRec );

		OGUSleep(2000);

		free( r->devRec );
		free( r->devPlay );
		free( r->samplesRec );
		free( r->samplesPlay );
		free( r );
	}
}


void * RecThread( void * v )
{
	struct CNFADriverSun * r = (struct CNFADriverSun *)v;
	size_t nbytes = r->bufsize * (2 * r->channelsRec);
	do
	{
		int nread = read( r->record_handle, r->samplesRec, nbytes );
		if( nread < 0 )
		{
			fprintf( stderr, "Warning: Sun Recording Failed\n" );
			break;
		}
		r->recording = 1;
		r->callback( (struct CNFADriver *)r, NULL, r->samplesRec, 0, (nread / 2) / r->channelsRec);
	} while( 1 );
	r->recording = 0;
	fprintf( stderr, "Sun Recording Stopped\n" );
	return 0;
}

void * PlayThread( void * v )
{
	struct CNFADriverSun * r = (struct CNFADriverSun *)v;
	size_t nbytes = r->bufsize * (2 * r->channelsPlay);
	int err;

	r->callback( (struct CNFADriver *)r, r->samplesPlay, NULL, r->bufsize, 0 );
	err = write( r->playback_handle, r->samplesPlay, nbytes );

	while( err >= 0 )
	{
		r->callback( (struct CNFADriver *)r, r->samplesPlay, NULL, r->bufsize, 0 );
		err = write( r->playback_handle, r->samplesPlay, nbytes );
		r->playing = 1;
	}
	r->playing = 0;
	fprintf( stderr, "Sun Playback Stopped\n" );
	return 0;
}

static struct CNFADriverSun * InitSun( struct CNFADriverSun * r )
{
	const char * devPlay = r->devPlay;
	const char * devRec = r->devRec;
	struct audio_info rinfo, pinfo;

	if( devRec == NULL || strcmp ( devRec, "default" ) == 0 )
	{
		devRec = "/dev/audio";
	}

	if( devPlay == NULL || strcmp ( devPlay , "default" ) == 0 )
	{
		devPlay = "/dev/audio";
	}

	printf( "CNFA Sun Init -> devPlay: %s, channelsPlay: %d, spsPlay: %d, devRec: %s, channelsRec: %d, spsRec: %d\n", devPlay, r->channelsPlay, r->spsPlay, devRec, r->channelsRec, r->spsRec);

	if( r->channelsPlay && r->channelsRec && strcmp (devPlay, devRec) == 0 )
	{
		if ( (r->playback_handle = r->record_handle = open (devPlay, O_RDWR)) < 0 )
		{
			fprintf (stderr, "cannot open audio device (%s)\n", 
				 strerror (errno));
			goto fail;
		}
	}
	else
	{
		if( r->channelsPlay )
		{
			if ( (r->playback_handle = open (devPlay, O_WRONLY)) < 0 )
			{
				fprintf (stderr, "cannot open output audio device %s (%s)\n", 
					 r->devPlay, strerror (errno));
				goto fail;
			}
		}

		if( r->channelsRec )
		{
			if ( (r->record_handle = open (devRec, O_RDONLY)) < 0 )
			{
				fprintf (stderr, "cannot open input audio device %s (%s)\n", 
					 r->devRec, strerror (errno));
				goto fail;
			}
		}
	}

	if( r->playback_handle )
	{
		AUDIO_INITINFO(&pinfo);

		pinfo.play.precision = 16;
		pinfo.play.encoding = AUDIO_ENCODING_LINEAR;
		pinfo.play.sample_rate = r->spsPlay;
		pinfo.play.channels = r->channelsPlay;

		if ( ioctl(r->playback_handle, AUDIO_SETINFO, &pinfo) < 0 )
		{
			fprintf (stderr, "cannot set audio playback format (%s)\n",
				 strerror (errno));
			goto fail;
		}

		if ( ioctl(r->playback_handle, AUDIO_GETINFO, &pinfo) < 0 )
		{
			fprintf (stderr, "cannot get audio record format (%s)\n",
				 strerror (errno));
			goto fail;
		}

		r->spsPlay = pinfo.play.sample_rate;
		r->channelsPlay = pinfo.play.channels;

		if ( (r->samplesPlay = calloc(2 * r->channelsPlay, r->bufsize)) == NULL )
		{
			goto fail;
		}
	}

	if( r->record_handle )
	{
		AUDIO_INITINFO(&rinfo);

		rinfo.record.precision = 16;
		rinfo.record.encoding = AUDIO_ENCODING_LINEAR;
		rinfo.record.sample_rate = r->spsRec;
		rinfo.record.channels = r->channelsRec;

		if ( ioctl(r->record_handle, AUDIO_SETINFO, &rinfo) < 0 )
		{
			fprintf (stderr, "cannot set audio record format (%s)\n",
				 strerror (errno));
			goto fail;
		}

		if ( ioctl(r->record_handle, AUDIO_GETINFO, &rinfo) < 0 )
		{
			fprintf (stderr, "cannot get audio record format (%s)\n",
				 strerror (errno));
			goto fail;
		}

		r->spsRec = rinfo.record.sample_rate;
		r->channelsRec = rinfo.record.channels;

		if ( (r->samplesRec = calloc(2 * r->channelsRec, r->bufsize)) == NULL )
		{
			goto fail;
		}
	}

	if( r->playback_handle )
	{
		r->threadPlay = OGCreateThread( PlayThread, r );
	}

	if( r->record_handle )
	{
		r->threadRec = OGCreateThread( RecThread, r );
	}

	printf( "CNFA Sun Init Out -> channelsPlay: %d, spsPlay: %d, channelsRec: %d, spsRec: %d\n", r->channelsPlay, r->spsPlay, r->channelsRec, r->spsRec);

	return r;

fail:
	if( r )
	{
		if( r->playback_handle != -1 ) close (r->playback_handle);
		if( r->record_handle != -1 ) close (r->record_handle);
		free( r->samplesPlay );
		free( r->samplesRec );
		free( r );
	}
	return 0;
}



void * InitSunDriver( CNFACBType cb, const char * your_name, int reqSPSPlay, int reqSPSRec, int reqChannelsPlay, int reqChannelsRec, int sugBufferSize, const char * outputSelect, const char * inputSelect, void * opaque )
{
	struct CNFADriverSun * r = (struct CNFADriverSun *)malloc( sizeof( struct CNFADriverSun ) );

	r->CloseFn = CloseCNFASun;
	r->StateFn = CNFAStateSun;
	r->callback = cb;
	r->opaque = opaque;
	r->spsPlay = reqSPSPlay;
	r->spsRec = reqSPSRec;
	r->channelsPlay = reqChannelsPlay;
	r->channelsRec = reqChannelsRec;

	r->devRec = (inputSelect)?strdup(inputSelect):0;
	r->devPlay = (outputSelect)?strdup(outputSelect):0;

	r->samplesPlay = NULL;
	r->samplesRec = NULL;

	r->playback_handle = -1;
	r->record_handle = -1;
	r->bufsize = sugBufferSize;

	return InitSun(r);
}

REGISTER_CNFA( SUN, 10, "Sun", InitSunDriver );

