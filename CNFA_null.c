//Copyright 2015-2020 <>< Charles Lohr under the ColorChord License.

#include "CNFA.h"
#include "os_generic.h"
#include <stdlib.h>

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

void CloseCNFANull( void * object )
{
	free( object );
}

int CNFAStateNull( void * object )
{
	return 0;
}


void * InitCNFANull( CNFACBType cb, const char * your_name, int reqSPS, int reqChannelsRec, int reqChannelsPlay, int sugBufferSize, const char * inputSelect, const char * outputSelect, void * opaque )
{
	struct CNFADriverNull * r = malloc( sizeof( struct CNFADriverNull ) );
	r->CloseFn = CloseCNFANull;
	r->StateFn = CNFAStateNull;
	r->callback = cb;
	r->sps = reqSPS;
	r->opaque = opaque;
	r->channelsPlay = reqChannelsPlay;
	r->channelsRec = reqChannelsRec;
	return r;
}


REGISTER_CNFA( NullCNFA, 1, "NULL", InitCNFANull );

