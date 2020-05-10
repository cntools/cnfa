//Copyright 2015 <>< Charles Lohr under the ColorChord License.

#include "sound.h"
#include "os_generic.h"
#include <stdlib.h>
#include "parameters.h"

struct CNFADriverNull
{
	void (*CloseFn)( void * object );
	int (*StateFn)( void * object );
	CNFACBType callback;
	short channelsPlay;
	short channelsRec;
	int sps;
	void * opaque;
};

void CloseCNFANull( struct CNFADriverNull * object )
{
	free( object );
}

int CNFAStateNull( struct CNFADriverNull * object )
{
	return 0;
}


void * InitCNFANull( CNFACBType cb, const char * your_name, int reqSPS, int reqChannelsRec, int reqChannelsPlay, int sugBufferSize, const char * inputSelect, const char * outputSelect )
{
	struct CNFADriverNull * r = malloc( sizeof( struct CNFADriverNull ) );
	r->CloseFn = CloseCNFANull;
	r->StateFn = CNFAStateNull;
	r->cnfacb = cb;
	r->sps = reqSPS;
	r->channelsPlay = reqChannelsPlay;
	r->channelsRec = reqChannelsRec;
	return r;
}


REGISTER_CNFA( NullCNFA, 1, "NULL", InitCNFANull );

