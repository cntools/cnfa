#include <stdio.h>
#include <math.h>

#if defined(WINDOWS) || defined(__WINDOWS__) || defined(_WINDOWS) \
                     || defined(WIN32)       || defined(WIN64) \
                     || defined(_WIN32)      || defined(_WIN64) \
                     || defined(__WIN32__)   || defined(__CYGWIN__) \
                     || defined(__MINGW32__) || defined(__MINGW64__) \
                     || defined(__TOS_WIN__) || defined(_MSC_VER)
#include <windows.h>
#define sleep(time_s) Sleep((time_s) * 1000)
#else
#include <unistd.h>
#endif

// If using the shared library, don't define CNFA_IMPLEMENTATION 
// (it's already in the library).
#ifndef USE_SHARED
#define CNFA_IMPLEMENTATION
#endif
#include "CNFA.h"

#define RUNTIME 5

double omega = 0;
int totalframesr = 0;
int totalframesp = 0;

void Callback( struct CNFADriver * sd, short * out, short * in, int framesp, int framesr )
{
	int i;

	totalframesr += framesr;
	totalframesp += framesp;

	int channels = sd->channelsPlay;
	for( i = 0; i < framesp; i++ )
	{
		// Shift phase, so we run at 440 Hz (A4)
		omega += ( 3.14159 * 2 * 440. ) / sd->spsPlay;

		// Make the 440 Hz tone at 10% volume and convert to short. 
		short value = sin( omega ) * 0.1 * 32767;

		int c;
		for( c = 0; c < channels; c++ )
		{
			*(out++) = value;
		}
	}
}


struct CNFADriver * cnfa;

int main( int argc, char ** argv )
{
	cnfa = CNFAInit( 

		NULL, //You can select a plaback driver, or use NULL for default.
		"cnfa_example", Callback, 
		48000, //Requested samplerate for playback
		48000, //Requested samplerate for recording
		2, //Number of playback channels.
		2, //Number of record channels.
		1024, //Buffer size in frames.
		0, //Could be a string, for the selected input device - but 0 means default.
		0,  //Could be a string, for the selected output device - but 0 means default.
		0 // 'opaque' value if the driver wanted it.
	 );

	sleep( RUNTIME );
	printf( "Received %d (%d per sec) frames\nSent %d (%d per sec) frames\n", totalframesr, totalframesr/RUNTIME, totalframesp, totalframesp/RUNTIME );
}

