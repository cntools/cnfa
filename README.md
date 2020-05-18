# cnfa
CN's foundational audio drivers

See CNFA.h for more help and use info.

This toolset is 100% based around sound callbacks.

You get three functions:

```
struct CNFADriver * CNFAInit( const char * driver_name, const char * your_name, CNFACBType cb, int reqSPS, int reqChannelsRec,
	int reqChannelsPlay, int sugBufferSize, const char * inputSelect, const char * outputSelect, void * opaque )	
int CNFAState( struct CNFADriver * cnfaobject ); //returns bitmask.  1 if mic recording, 2 if play back running, 3 if both running.
void CNFAClose( struct CNFADriver * cnfaobject );
```

Then it goes and calls a callback function, the `CNFACBType cb` parameter.  This can feed you new frames, or you can pass frames back in.

```
void Callback( struct CNFADriver * sd, short * in, short * out, int framesr, int framesp )
{
  int i;
  for( i = 0; i < framesr * sd->channelsRec; i++ ) 
    short value = in[i];
  for( i = 0; i < framesp * sd->channelsPlay; i++ )
    out[i] = 0; //Send output frames.
}
```

This repo does not contain any tests or building, as this is intended as a library-only.  For any use, please see colorchord: https://github.com/cnlohr/colohrchord

