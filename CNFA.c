//Copyright <>< 2010-2020 Charles Lohr (And other authors as cited)
//CNFA is licensed under the MIT/x11, ColorChord or NewBSD Licenses. You choose.

#ifndef _CNFA_C
#define _CNFA_C

#include "CNFA.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static CNFAInitFn * CNFADrivers[MAX_CNFA_DRIVERS];
static char * CNFADriverNames[MAX_CNFA_DRIVERS];
static int CNFADriverPriorities[MAX_CNFA_DRIVERS];

void RegCNFADriver( int priority, const char * name, CNFAInitFn * fn )
{
	printf("[SDE] Registering driver %s\n", name);
	int j;

	if( priority <= 0 )
	{
		return;
	}

	for( j = MAX_CNFA_DRIVERS-1; j >= 0; j-- )
	{
		//Cruise along, find location to insert
		if( j > 0 && ( !CNFADrivers[j-1] || CNFADriverPriorities[j-1] < priority ) )
		{
			CNFADrivers[j] = CNFADrivers[j-1];
			CNFADriverNames[j] = CNFADriverNames[j-1];
			CNFADriverPriorities[j] = CNFADriverPriorities[j-1];
		}
		else
		{
			CNFADrivers[j] = fn;
			CNFADriverNames[j] = strdup( name );
			CNFADriverPriorities[j] = priority;
			break;
		}
	}
}

struct CNFADriver * CNFAInit( const char * driver_name, const char * your_name, CNFACBType cb, int reqSPS,
	int reqChannelsRec, int reqChannelsPlay, int sugBufferSize, const char * inputSelect, const char * outputSelect, void * opaque)
{

#if defined( ANDROID ) || defined( __android__ )
	//Android can't run static-time code.
	void REGISTERAndroidCNFA();
	REGISTERAndroidCNFA();
#endif

	int i;
	struct CNFADriver * ret = 0;
	if( driver_name == 0 || strlen( driver_name ) == 0 )
	{
		//Search for a driver.
		for( i = 0; i < MAX_CNFA_DRIVERS; i++ )
		{
			if( CNFADrivers[i] == 0 )
			{
				return 0;
			}
			ret = (struct CNFADriver *)CNFADrivers[i]( cb, your_name, reqSPS, reqChannelsRec, reqChannelsPlay, sugBufferSize, inputSelect, outputSelect, opaque );
			if( ret )
			{
				return ret;
			}
		}
	}
	else
	{
		printf( "Initializing CNFA.  Recommended driver: %s\n", driver_name );
		for( i = 0; i < MAX_CNFA_DRIVERS; i++ )
		{
			if( CNFADrivers[i] == 0 )
			{
				return 0;
			}
			if( strcmp( CNFADriverNames[i], driver_name ) == 0 )
			{
				return (struct CNFADriver *)CNFADrivers[i]( cb, your_name, reqSPS, reqChannelsRec, reqChannelsPlay, sugBufferSize, inputSelect, outputSelect, opaque );
			}
		}
	}
	return 0;
}

int CNFAState( struct CNFADriver * cnfaobject )
{
	if( cnfaobject )
	{
		return cnfaobject->StateFn( cnfaobject );
	}
	return -1;
}

void CNFAClose( struct CNFADriver * cnfaobject )
{
	if( cnfaobject )
	{
		cnfaobject->CloseFn( cnfaobject );
	}
}

#endif


