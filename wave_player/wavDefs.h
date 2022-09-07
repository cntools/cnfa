/*
 * wavDefs.h Samuel Ellicott - 9-3-13
 * defines basic features of PCM encoded wav files
 */

#ifndef _WAV_DEFS_H_
#define _WAV_DEFS_H_

#include <stdint.h>

//#define ABSOLUTE 1
#ifdef ABSOLUTE

	//absolute offsets PCM audacity files only
	//          name        offset  description	
	//RIFF chunk descriptor
	#define     CHUNK_ID    0   //start of the riff chunk
	#define     CHUNK_SIZE  4   //size of the file minus the first 2 entries
	#define     FORMAT      8   //describes the format aka. WAVE

	//fmt sub-chunk descriptor
	#define     FMT_CHUNK_ID    12  //start of the fmt sub chunk
	#define     FMT_CHUNK_SIZE  16  //length of the rest of the sub-chunk. for PCM should be 16
	#define     AUDIO_FORMAT    20  //audio format - for PCM should be 1 (unsigned char)
	#define     NUM_CHANNELS    22  //the number of channels 1 or 2	(unsigned char)
	#define     SAMPLE_RATE     24  //sample rate 8000, 44100, or 48000 typical in hz
	#define     BIT_RATE        28  //bit rate - (SampleRate * NumChannels * BitsPerSample)/8
	#define     BLOCK_ALIGN     32  //NumChannels * BitsPerSample/8 (unsigned char)
	                                //1 - 8 bit mono, 2 - 8 bit stereo/16 bit mono, 4 - 16 bit stereo
	#define     BITS_PER_SAMPLE 34	//bits per channel 8 or 16 (unsigned char)
	
	
	//fmt sub-chunk descriptor
	#define     CHUNK_ID_1      12  //start of the fmt sub chunk
	#define     CHUNK_SIZE_1    16  //length of the rest of the sub-chunk. for PCM should be 1
	
	//info data sub-chunk descriptor
	//if Wav_Data.is_info == 1 than this is info chunk
	//if Wav_Data.is_info == 0 than this is data chunk
	#define     _CHUNK_ID       36  //start of data sub chunk
	#define     _CHUNK_SIZE     40  //chunk size of sub chunk
	#define     _CHUNK_DATA     44  //data in chunk
	
#else
	//general chunk descriptor
	#define     CHUNK_ID        0   //start of the riff chunk
	#define     CHUNK_SIZE      4   //size of the file minus the first 2 entries
	#define     FORMAT          8   //describes the format for RIFF and LIST chunks
	#define     CHUNK_DATA      8   //start of data for other chunks
	#define     CHUNK_ID_LEN    5   //length of chunk ID string (including null char)
	
	//fmt sub-chunk descriptor
	#define     FMT_CHUNK_ID    0   //start of the fmt sub chunk
	#define     FMT_CHUNK_SIZE  4   //length of the rest of the sub-chunk. for PCM should be 16
	#define     AUDIO_FORMAT    8   //audio format - for PCM should be 1 (unsigned char)
	#define     NUM_CHANNELS    10  //the number of channels 1 or 2	(unsigned char)
	#define     SAMPLE_RATE     12  //sample rate 8000, 44100, or 48000 typical in hz
	#define     BIT_RATE        16  //bit rate - (SampleRate * NumChannels * BitsPerSample)/8
	#define     BLOCK_ALIGN     20  //NumChannels * BitsPerSample/8 (unsigned char)
                                    //1 - 8 bit mono, 2 - 8 bit stereo/16 bit mono, 4 - 16 bit stereo
	#define     BITS_PER_SAMPLE 22  //bits per channel 8 or 16 (unsigned char)

#endif //ABSOLUTE

#define MAX_TAG_SIZE 100 //defines the maximum number of characters to be allocated for any info string

/*
 * data on the optional INFO data chunk contains pointers to the following info:
 * title
 * author
 * genre
 */
typedef struct WaveInfoChunk{
	uint8_t is_info;        //1 for info data, 0 for no info data
	uint32_t info_offset;   //the file offset for the info chunk
	uint32_t info_len;      //length of the info chunk
	
	//begin heap pointers
	char *title;
	char *artist;
	char *genre;
	char *creation_date;
} WaveInfoChunk;
/*
 * data structure containing the data neccesarry for PCM audio playback
 */
typedef struct WaveFmtChunk{
	uint32_t fmt_len;           //fmt size: 16 for pcm
	uint16_t audio_format;      //1 = PCM
	uint16_t num_channels;      //1 for mono, 2 for stereo
	uint32_t sample_rate;       //44100 (CD), 48000 (DAT)
	uint32_t byte_rate;         //SampleRate * NumChannels * BitsPerSample/8
	uint16_t block_align;      //1 - 8 bit mono
	                            //2 - 8 bit stereo/16 bit mono
	                            //4 - 16 bit stereo
	uint16_t bits_per_sample;   //8 or 16
	uint8_t bytes_per_sample;
} WaveFmtChunk;

typedef struct WaveDataChunk{
	uint32_t data_offset;       //the file offset for data chunk
	uint32_t data_size;         //data size in bytes
	uint32_t current_offset;    //current offset in file in bytes
	uint16_t num_samples;       //number of samples
	uint16_t samples_left;      //number of samples left to read
} WaveDataChunk;

/*
 * struct all the pertinent information for the wav file
 */
typedef struct WaveHeaderChunk{
	struct WaveFmtChunk fmt;    //the wave format chunk
	struct WaveInfoChunk info;  //the info metadata chunk
	struct WaveDataChunk data;
} WaveHeaderChunk; 

#endif //_WAV_DEFS_H_