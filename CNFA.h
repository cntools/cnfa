//Copyright <>< 2010-2020 Charles Lohr (And other authors as cited)
//CNFA is licensed under the MIT/x11, ColorChord or NewBSD Licenses. You choose.
//
// CN's Platform-agnostic, foundational sound driver subsystem.
// Easily output and input sound on a variety of platforms.
//
// Options:
//  * #define CNFA_IMPLEMENTATION before this header and it will build all 
//    definitions in.
//


#ifndef _CNFA_H
#define _CNFA_H



//this #define is per-platform.  For instance on Linux, you have ALSA, Pulse and null
#define MAX_CNFA_DRIVERS 4

struct CNFADriver;

#ifdef __cplusplus
extern "C" {
#endif

//NOTE: Some drivers have synchronous duplex mode, other drivers will use two different callbacks.  If ether is unavailable, it will be NULL.
//I.e. if `out` is null, only use in to read.  If in is null, only place samples in out.
typedef void(*CNFACBType)( struct CNFADriver * sd, short * in, short * out, int framesr, int framesp );

typedef void*(CNFAInitFn)( CNFACBType cb, const char * your_name, int reqSPS, int reqChannelsRec, int reqChannelsPlay, int sugBufferSize, const char * inputSelect, const char * outputSelect, void * opaque );

struct CNFADriver
{
	void (*CloseFn)( void * object );
	int (*StateFn)( void * object );
	CNFACBType callback;
	short channelsPlay;
	short channelsRec;
	int sps;
	void * opaque;

	//More fields may exist on a per-sound-driver basis
};

//Accepts:
//If DriverName = 0 or empty, will try to find best driver.
//
// our_source_name is an optional argument, but on some platforms controls the name of your endpoint.
// reqSPS = 44100 is guaranteed on many platforms.
// reqChannelsRec = 1 or 2 guaranteed on many platforms.
// reqChannelsPlay = 1 or 2 guaranteedon many platforms. NOTE: Some systems require ChannelsPlay == ChannelsRec!
// sugBufferSize = No promises.
// inputSelect = No standardization, NULL is OK for default.
// outputSelect = No standardization, NULL is OK for default.

struct CNFADriver * CNFAInit( const char * driver_name, const char * your_name, CNFACBType cb, int reqSPS, int reqChannelsRec,
	int reqChannelsPlay, int sugBufferSize, const char * inputSelect, const char * outputSelect, void * opaque );
	
int CNFAState( struct CNFADriver * cnfaobject ); //returns bitmask.  1 if mic recording, 2 if play back running, 3 if both running.
void CNFAClose( struct CNFADriver * cnfaobject );


//Called by various sound drivers.  Notice priority must be greater than 0.  Priority of 0 or less will not register.
//This is an internal function.  Applications shouldnot call it.
void RegCNFADriver( int priority, const char * name, CNFAInitFn * fn );

#define REGISTER_CNFA( cnfadriver, priority, name, function ) \
	void __attribute__((constructor)) REGISTER##cnfadriver() { RegCNFADriver( priority, name, function ); }


#ifdef CNFA_IMPLEMENTATION
#include "CNFA.c"
#include "CNFA_null.c"
#if defined(WINDOWS) || defined(WIN32) || defined(WIN64)
#include "CNFA_winmm.c"
#include "CNFA_wasapi.c"
#elif defined( ANDROID ) || defined( __android__ )
#include "CNFA_android.c"
#else
#include "CNFA_pulse.c"
#include "CNFA_alsa.c"
#endif
#endif


#ifdef __cplusplus
};
#endif



#endif

