# cnfa
CN's foundational audio drivers

See CNFA.h for more help and use info.

This toolset is 100% based around sound callbacks.

You get three functions:

```C
struct CNFADriver* CNFAInit(const char* driver_name,
                            const char* your_name,
                            CNFACBType cb,
                            int reqSPSPlay,
                            int reqSPSRec,
                            int reqChannelsPlay,
                            int reqChannelsRec,
                            int sugBufferSize,
                            const char* outputSelect,
                            const char* inputSelect,
                            void* opaque)

// Returns bitmask: 1 if mic recording, 2 if playback running, 3 if both running.
int CNFAState(struct CNFADriver* cnfaobject)

void CNFAClose(struct CNFADriver* cnfaobject)
```

Then it goes and calls a callback function, the `CNFACBType cb` parameter.  This can feed you new frames, or you can pass frames back in.
`framesp` is the size of one channel of the output buffer in samples. If there are multiple channels they should be interleaved i.e. L:R:L:R...
`framerp` is the size of one channel of the input buffer in samples. Works the same way as the output buffer.
You can obtain the number of input/output channels from `sd->channelsRec` and `sd->channelsPlay` respectively.

```C
void Callback(struct CNFADriver* sd, short* out, short* in, int framesp, int framesr)
{
  int i;
  for( i = 0; i < framesr * sd->channelsRec; i++ ) 
    short value = in[i];
  for( i = 0; i < framesp * sd->channelsPlay; i++ )
    out[i] = 0; //Send output frames.
}
```

There are two examples in this repository, [example.c](example.c) and [wave_player.c](wave_player/wav_player.c). Both of these show examples of using
CNFA to output sound. For use of CNFA for input see [colorchord](https://github.com/cnlohr/colorchord)

### Building .DLL and .SO files
If you would like to use CNFA in a project where using a DLL or SO file is more practical, you can easily build those files. The below steps are for the Clang & GGC compilers, but others like TCC should work fine as well, just have not been tested.

NOTE: In order for functions to be exported, you'll need to make sure `-DBUILD_DLL` is specified!

Parts of CNFA rely on [rawdraw](https://github.com/cntools/rawdraw), so make sure to clone this repo somewhere and insert the path to it in the `[RAWDRAW PATH]` space below:

**Don't forget to install all libraries' headers!** For Linux, at least these packages: `libasound2-dev`, `libpulse-dev`

Windows build:
```PS
& "C:\Program Files\LLVM\bin\clang.exe" CNFA.c -shared -o CNFA.dll -DWINDOWS -DCNFA_IMPLEMENTATION -DBUILD_DLL -D_CRT_SECURE_NO_WARNINGS -I"[RAWDRAW PATH]" -lmmdevapi -lavrt -lole32
```

Linux build:
```Bash
make shared-example
```
This will build the shared library as `libCNFA.so` and will build the example
project to link against it. You can run the example project with `./example`.
The makefile will try and copy the `os_generic.h` header from 
[rawdraw](https://github.com/cntools/rawdraw) 
automatically using `wget`.

The command to build simply build the shared library is:
```Bash
gcc CNFA.c -shared -fpic -o CNFA.so -DCNFA_IMPLEMENTATION -DBUILD_DLL -I"[RAWDRAW PATH]" -lasound -lpulse -lpthread
```
