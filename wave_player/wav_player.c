/*
 * Sam Ellicott - 09-06-22
 * CNFA demo wave file player
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "wavDefs.h"

// wave file player function prototypes
int loadHeader(FILE *file, WaveHeaderChunk *hdr);
int loadInfo(FILE *file, WaveHeaderChunk *hdr);
int printInfo(FILE *file);
int readData(FILE *file, WaveHeaderChunk *hdr, void* buff, int buff_len);
void freeData(WaveHeaderChunk *hdr);

/* ------------------------------------------ Main Application Code ------------------------------*/
#define BUFF_SIZE 2048 
// If using the shared library, don't define CNFA_IMPLEMENTATION 
// (it's already in the library).
#ifndef USE_SHARED
#define CNFA_IMPLEMENTATION
#endif
#include "../CNFA.h"

int totalframesr = 0;
int totalframesp = 0;

FILE* wav_file;
WaveHeaderChunk hdr;
struct CNFADriver * cnfa;
short buff[BUFF_SIZE];
int is_done;

void Callback( struct CNFADriver * sd, short * out, short * in, int framesp, int framesr )
{
	int* is_done_ptr = (int*) sd->opaque;
	const int output_channels = sd->channelsPlay;
	const int file_channels = hdr.fmt.num_channels;
	const int output_buff_sz = framesp * output_channels;
	int br = 0;

	// if we have already ended the file, then clear the buffer and exit function
	if(*is_done_ptr) {
		memset(out, 0, sizeof(short) * output_buff_sz);
		return;
	}

	totalframesr += framesr;
	totalframesp += framesp;

	if (output_channels == file_channels) {
		int read_buff_sz = framesp * sd->channelsPlay;
		br = readData(wav_file, &hdr, out, read_buff_sz);
	}
	else if (output_channels > file_channels) {
		int read_buff_sz = framesp;

		// exit loop if we are done filling the output buffer or we are at the end of file
		int samples_remaining = read_buff_sz;
		while (samples_remaining > 0 && br >= 0) {
			int read_sz = (samples_remaining > BUFF_SIZE) ? BUFF_SIZE : samples_remaining;
			br = readData(wav_file, &hdr, buff, read_sz);
			// duplicate data on left and right channels
			for (int i = 0; i < read_buff_sz; ++i){
				out[2*i]   = buff[i];
				out[2*i+1] = buff[i];
			}
			samples_remaining -= br;
		}
	}
	else {
		printf("what are you doing? mono sound output?\n");
	}

	// end of file
	if (br < 0) {
		printf("End of wave file: setting flag\n");
		*is_done_ptr = 1;
	}
}

int main (int nargs, char** args) {
    const char* filename;
	// if there is a file given on the command line play it.
	if(nargs >= 2) {
		filename = args[1];
	}
	else {
		printf("\nError, no input file\nUseage %s <wave file>\n", args[0]);
		return 1;
	}
	wav_file = fopen(filename, "r");

	printInfo(wav_file);
	printf("\n\n");

	printf("loading file\n");
	loadHeader(wav_file, &hdr);

	printf("playing file\n");

	is_done = 0;
	cnfa = CNFAInit( 
		NULL,                // String, for the driver "PULSE", "WASAPI" (output only) - NULL means default. 
		"cnfa_example",      // Name of program to audio driver
		Callback,            // CNFA callback function handle
		hdr.fmt.sample_rate, // Requested samplerate for playback
		hdr.fmt.sample_rate, // Requested samplerate for record
		2,                   // Number of playback channels.
		2,                   // Number of record channels.
		1024,                // Buffer size in frames.
		NULL,                // String, for the selected input device - NULL means default.
		NULL,                // String, for the selected output device - NULL means default.
		&is_done             // pass an integer as an "opaque" object so that CNFA can close
	);

	int runtime = 0;
	const char* spin_glyph = "-\\|/";
	const char* glyph = spin_glyph;
	int i = 0;
	while (!is_done){
		sleep(1);
		++runtime;
		printf("\r %c ", *glyph++);
		fflush(stdout);
		if (!*glyph) {
			glyph = spin_glyph;
		}
	}

	CNFAClose(cnfa);
	fclose(wav_file);

	printf( "Received %d (%d per sec) frames\nSent %d (%d per sec) frames\n",
		totalframesr, totalframesr/runtime,   // recorded samples, recorded samples/sec
		totalframesp, totalframesp/runtime ); // outputted samples, outputted samples/sec

	return 0;
}

/* ------------------------------------------ Wave File Player Code ------------------------------*/
int printInfo(FILE *file){
	WaveHeaderChunk wav_data;
	
	if(loadHeader(file, &wav_data)!=0){
		printf("file invalid\n\r");
		return 1;
	}
	//print file data
	printf("\n");
	printf("Audio Format: %i \n", wav_data.fmt.audio_format);
	printf("Channels: %u \n", wav_data.fmt.num_channels);
	printf("Sample Rate: %lu \n", wav_data.fmt.sample_rate);
	printf("Block Alignment (bytes): %u \n", wav_data.fmt.block_align);
	printf("Bits-per-Sample: %u \n", wav_data.fmt.bits_per_sample);
	
	if(loadInfo(file, &wav_data)==0){//if there is data show it
		printf("\n");
		printf("Track name: %s \n\r", wav_data.info.title);
		printf("Artist: %s \n\r", wav_data.info.artist);
		printf("Genre: %s \n\r", wav_data.info.genre);
		printf("Creation date: %s \n\r", wav_data.info.creation_date);
	}

	freeData(&wav_data);
	return 0;
}

int loadHeader(FILE *file, WaveHeaderChunk *hdr){
	char chunk_id[CHUNK_ID_LEN];
	uint32_t chunk_offset=0;//keeps track of position in file - referenced to the chunk id
	uint32_t chunk_len=0;//the length of the current chunk
	uint32_t br;


	//check if the file is valid first
	if(file==NULL){
		printf("Could not open file \n");
		return 1;
	}
	// check that the pointer is valid
	if (!hdr) {
		return 2;
	}
	// clear pointers
	hdr->info.creation_date = NULL;
	hdr->info.genre = NULL;
	hdr->info.artist = NULL;
	hdr->info.title = NULL;

	//look for RIFF/WAVE file header
	fseek(file, CHUNK_ID, SEEK_SET);
	fgets(chunk_id, CHUNK_ID_LEN, file);
	if(strncmp(chunk_id, "RIFF", 4)!=0){
		printf("File is not a wav file\n");
		return 1;
	}
	fseek(file, FORMAT, SEEK_SET);
	fgets(chunk_id, CHUNK_ID_LEN, file);
	if(strncmp(chunk_id, "WAVE", 4)!=0){
		printf("File is not a wav file \n");
		return 1;		
	}
	chunk_offset+=CHUNK_DATA+4;//add the file offset of the begining of the first chunk
	
	//must be WAVE look for chunks
	do{
		//get the chunk id
		fseek(file, chunk_offset+CHUNK_ID, SEEK_SET);
		fgets(chunk_id, CHUNK_ID_LEN, file);

		//get the chunk length
		fseek(file, chunk_offset+CHUNK_SIZE, SEEK_SET);
		br = fread(&chunk_len, sizeof(uint32_t), 1, file);//get length
	
		//check for fmt chunk
		if(strncmp(chunk_id, "fmt ", 4)==0){//if format section
			//get the format section of the file
			fseek(file, chunk_offset+AUDIO_FORMAT, SEEK_SET);
			br = fread(&hdr->fmt.audio_format, sizeof(uint16_t), 1, file);
			
			//number of channels
			fseek(file, chunk_offset+NUM_CHANNELS, SEEK_SET);
			br = fread(&hdr->fmt.num_channels, sizeof(uint16_t), 1, file);

			//sample rate
			fseek(file, chunk_offset+SAMPLE_RATE, SEEK_SET);
			br = fread(&hdr->fmt.sample_rate, sizeof(uint32_t), 1, file);

			//bits per channel tells if mono, stereo, or hifi stereo
			fseek(file, chunk_offset+BLOCK_ALIGN, SEEK_SET);
			br = fread(&hdr->fmt.block_align, sizeof(uint16_t), 1, file);

			//bits per channel 8 or 16
			fseek(file, chunk_offset+BITS_PER_SAMPLE, SEEK_SET);
			br = fread(&hdr->fmt.bits_per_sample, sizeof(uint16_t), 1, file);

			hdr->fmt.bytes_per_sample=hdr->fmt.bits_per_sample/8;
		}
		else if(strncmp(chunk_id, "data", 4)==0){//if chunk is of data type
			// grab data chunk location and size
			hdr->data.data_offset = chunk_offset;
			hdr->data.data_size = chunk_len;
			hdr->data.num_samples = (uint16_t) chunk_len/hdr->fmt.bytes_per_sample;
			hdr->data.samples_left = hdr->data.num_samples;
			hdr->data.current_offset = chunk_offset+CHUNK_DATA;
		}
		else if(strncmp(chunk_id, "LIST", 4)==0){//if chunk is of LIST type
			//check if it is of info type
			fseek(file, chunk_offset+CHUNK_DATA, SEEK_SET);
			fgets(chunk_id, 5, file);
			if(strncmp(chunk_id, "INFO", 4)==0){
				// grab info location and size
				hdr->info.info_offset = chunk_offset+4;
				hdr->info.info_len = chunk_len;
				hdr->info.is_info = 1;
			}
			else{
				// no file info (artist, genre, etc)
				hdr->info.is_info = 0;
			}	
		}
		else if(strncmp(chunk_id, "data", 4)!=0){
			//get length of chunk
			fseek(file, chunk_offset+CHUNK_SIZE, SEEK_SET);
			br = fread(&chunk_len, 2, 1, file);//get length
		}

		//done reading skip chunk
		chunk_offset=chunk_offset+chunk_len+CHUNK_DATA;
		fseek(file, chunk_offset, SEEK_SET);
		
	} while(fgets(chunk_id, 5, file)!=NULL);//loop until the end of the file is reached

	return 0;
}

/*
 * reads the info chunk if available and puts data into the WaveHeader structure provided
 */
int loadInfo(FILE *file, WaveHeaderChunk *hdr){
	char chunk_id[CHUNK_ID_LEN];
	uint32_t chunk_offset=hdr->info.info_offset+CHUNK_DATA;//current file position
	uint32_t chunk_len=0;//the length of the current chunk
	uint32_t tag_len=0;
	uint32_t br;

	if(hdr->info.is_info==0){//if no data
		printf("No artist information is available for this wav file \n");
		return 1;//no data
	}
	
	fseek(file, chunk_offset, SEEK_SET);//go to chunk data
	while(chunk_offset-4 - hdr->info.info_offset < hdr->info.info_len){//while in info chunk
		//get the chunk id
		fseek(file, chunk_offset+CHUNK_ID, SEEK_SET);
		fgets(chunk_id, 5, file);
		//get the chunk length
		fseek(file, chunk_offset+CHUNK_SIZE, SEEK_SET);
		br = fread(&chunk_len, sizeof(uint32_t), 1, file);//get length
		//make sure the length of the tag isn't to big
		if(chunk_len > MAX_TAG_SIZE){//if too big
			tag_len=MAX_TAG_SIZE;
		}
		else{
			tag_len=chunk_len;
		}
		
		//go to tag information	
		fseek(file, chunk_offset+CHUNK_DATA, SEEK_SET);
		if(strncmp(chunk_id, "INAM", 4)==0){       // if is name chunk
			hdr->info.title=malloc(tag_len+1);     // allocate memory for tag
			fgets(hdr->info.title, tag_len, file); // read tag
			
		}
		else if(strncmp(chunk_id, "IART", 4)==0){   // if is artist chunk
			hdr->info.artist=malloc(tag_len+1);     // allocate memory for tag
			fgets(hdr->info.artist, tag_len, file); // read tag
			
		}
		else if(strncmp(chunk_id, "IGNR", 4)==0){  // if is genre chunk
			hdr->info.genre=malloc(tag_len+1);     // allocate memory for tag
			fgets(hdr->info.genre, tag_len, file); // read tag
			
		}
		else if(strncmp(chunk_id, "ICRD", 4)==0){          // if is creation date chunk
			hdr->info.creation_date=malloc(tag_len+1);     // allocate memory for tag
			fgets(hdr->info.creation_date, tag_len, file); // read tag
			
		}
		//done reading skip chunk
		chunk_offset=chunk_offset+chunk_len+CHUNK_DATA;
		fseek(file, chunk_offset, SEEK_SET);
	}
	return 0;
}

int readData(FILE *file, WaveHeaderChunk *hdr, void* buff, int buff_len){
	uint32_t br;
	int bytes_to_read = hdr->fmt.bytes_per_sample*buff_len;

	if(!hdr) {
		printf("Error: No valid header\n");
		return -1;
	}

	fseek(file, hdr->data.current_offset, SEEK_SET);
	br = fread(buff, hdr->fmt.bytes_per_sample, buff_len, file);


	int bytes_read = hdr->fmt.bytes_per_sample*br;

	//printf("samples read: %u\n", br);

	hdr->data.samples_left -= br;
	hdr->data.current_offset += bytes_read;

	if(feof(file)){
		br = -1;
	}

	return br;
}

void freeData(WaveHeaderChunk *hdr) {
	//free the info tag heap data
	if(hdr->info.title){
		free(hdr->info.title);
		hdr->info.title=NULL;
	}
	
	if(hdr->info.artist){
		free(hdr->info.artist);
		hdr->info.artist=NULL;
	}
	
	if(hdr->info.genre){
		free(hdr->info.genre);
		hdr->info.genre=NULL;
	}
	
	if(hdr->info.creation_date){
		free(hdr->info.creation_date);
		hdr->info.creation_date=NULL;
	}
}
