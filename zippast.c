// ZIP-PAST .ZIP file embedder
// Dan Jackson, 2019

// Recipe (defaults):
// * one or more initial bytes ('-mode:byte')
// * end of file comment to push EOCD out of last 8kiB: >=8171 bytes ('-comment 8171')
// * output file must not have the extension '.zip' (-> .bin)

#define TEXT_STRING  "Rename file extension to .ZIP\r\n"
#define HEADER_STRING  TEXT_STRING "\x1A"	// 0x1A=EOF (for TYPE, etc.)
#define COMMENT_STRING "\x80" TEXT_STRING	// top-bit is cleared to start with NUL
#define MULTIPART_BOUNDARY "--" "MultipartBoundary" "--" "zippast" "----"

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#ifdef _MSC_VER
#define strcasecmp _stricmp
#define strdup _strdup
#endif

typedef enum {
	MODE_STANDARD,	// .zip-email with default pre/post padding
	MODE_NONE,		// pass-through (convert headers if requested)
	MODE_BMP,		// .bmp valid (nonsense) image
	MODE_WAV,		// .wav valid (nonsense) sound file
	MODE_EML,		// (not working) .eml (email) with attachment -- the offsets need fixing (e.g. duplicate central directory?)
	MODE_MHTML,		// (not working) .mht (MHTML) document with linked attachment -- self-download does not work (yet?), and the offsets will need fixing (e.g. duplicate central directory?)
	MODE_HTML,		// (not working) .html -- self-download does not work as HTML parsers normalize/remove certain bytes (e.g. CR/LF and NULL)
	MODE_BYTE,		// .bin with default pre/post padding
} HeaderMode;

// Write a bitmap header, pass negative height for top-down, works for 1/2/4/8/16/32-bit, 
// with <=8-bit having a palette (user must write 2^N * RGBX8888 entries), 
// 16/32-bit would probably need the BI_BITFIELDS writing to be more useful, 
// each span must be padded to 4 bytes.
#define BMP_WRITER_SIZE_HEADER	54
int BitmapWriteHeader(void *buffer, int width, int height, int bitsPerPixel)
{
	const unsigned int headerSize = BMP_WRITER_SIZE_HEADER;	// 54;									// Header size (54)
	const unsigned int paletteSize = ((bitsPerPixel <= 8) ? ((unsigned int)1 << bitsPerPixel) : 0) << 2;	// Number of palette bytes
	const unsigned int stride = 4 * ((width * ((bitsPerPixel + 7) / 8) + 3) / 4);							// Byte width of each line
	const unsigned long biSizeImage = (unsigned long)stride * (height < 0 ? -height : height);				// Total number of bytes that will be written
	const unsigned long bfOffBits = headerSize + paletteSize;
	const unsigned long bfSize = bfOffBits + biSizeImage;
	unsigned char * const p = (unsigned char *)buffer;
	// Unset bytes are zero
	memset(buffer, 0, headerSize);
	// @0 WORD bfType
	p[0] = 'B'; p[1] = 'M';
	// @2 DWORD bfSize
	p[2] = (unsigned char)bfSize; p[3] = (unsigned char)(bfSize >> 8); p[4] = (unsigned char)(bfSize >> 16); p[5] = (unsigned char)(bfSize >> 24);
	// @6 WORD bfReserved1
	// @8 WORD bfReserved2
	// @10 DWORD bfOffBits
	p[10] = (unsigned char)bfOffBits; p[11] = (unsigned char)(bfOffBits >> 8); p[12] = (unsigned char)(bfOffBits >> 16); p[13] = (unsigned char)(bfOffBits >> 24);
	// @14 DWORD biSize
	p[14] = 40;
	// @18 DWORD biWidth
	p[18] = (unsigned char)width; p[19] = (unsigned char)(width >> 8); p[20] = (unsigned char)(width >> 16); p[21] = (unsigned char)(width >> 24);
	// @22 DWORD biHeight
	p[22] = (unsigned char)height; p[23] = (unsigned char)(height >> 8); p[24] = (unsigned char)(height >> 16); p[25] = (unsigned char)(height >> 24);
	// @26 WORD biPlanes
	p[26] = 1;
	// @28 WORD biBitCount
	p[28] = bitsPerPixel;
	// @30 DWORD biCompression (0=BI_RGB, 3=BI_BITFIELDS)
	// @34 biSizeImage
	p[34] = (unsigned char)biSizeImage; p[35] = (unsigned char)(biSizeImage >> 8); p[36] = (unsigned char)(biSizeImage >> 16); p[37] = (unsigned char)(biSizeImage >> 24);
	// @38 DWORD biXPelsPerMeter
	// @42 DWORD biYPelsPerMeter
	// @46 DWORD biClrUsed
	// @50 DWORD biClrImportant
	// @54 <end>
	return headerSize;
}

// Write a WAV header
#define WAV_WRITER_SIZE_HEADER	44
int WavWriteHeader(void *buffer, unsigned int bitsPerSample, unsigned int chans, unsigned int freq, unsigned int numSamples)
{
	unsigned char * const p = (unsigned char *)buffer;
	const unsigned int headerSize = WAV_WRITER_SIZE_HEADER;				// 44;
	unsigned int bytesPerSample = (bitsPerSample + 7) / 8;
	unsigned int bytesPerSec = bytesPerSample * chans * freq;
	unsigned int blockAlign = bytesPerSample * chans;
	unsigned int dataSize = numSamples * chans * bytesPerSample;
	if (dataSize & 1) dataSize++;		// trailing padding byte
	unsigned int riffSize = dataSize + 36;
	// Unset bytes are zero
	memset(buffer, 0, headerSize);
	p[0] = 'R'; p[1] = 'I'; p[2] = 'F'; p[3] = 'F';						// ckID
	p[4] = (unsigned char)riffSize; p[5] = (unsigned char)(riffSize >> 8); p[6] = (unsigned char)(riffSize >> 16); p[7] = (unsigned char)(riffSize >> 24); // ckSize
	p[8] = 'W'; p[9] = 'A'; p[10] = 'V'; p[11] = 'E';					// WAVE ID
	p[12] = 'f'; p[13] = 'm'; p[14] = 't'; p[15] = ' ';					// 'fmt ' ckID
	p[16] = 16; p[17] = 0; p[18] = 0; p[19] = 0;						// 'fmt ' cksize
	p[20] = 1; p[21] = 0;												// wFormatTag: WAVE_FORMAT_PCM=1
	p[22] = (unsigned char)chans; p[23] = (unsigned char)(chans >> 8);	// nChannels
	p[24] = (unsigned char)freq; p[25] = (unsigned char)(freq >> 8); p[26] = (unsigned char)(freq >> 16); p[27] = (unsigned char)(freq >> 24); // nSamplesPerSec
	p[28] = (unsigned char)bytesPerSec; p[29] = (unsigned char)(bytesPerSec >> 8); p[30] = (unsigned char)(bytesPerSec >> 16); p[31] = (unsigned char)(bytesPerSec >> 24); // nAvgBytesPerSec
	p[32] = (unsigned char)blockAlign; p[33] = (unsigned char)(blockAlign >> 8);		// nBlockAlign
	p[34] = (unsigned char)bitsPerSample; p[35] = (unsigned char)(bitsPerSample >> 8);	// wBitsPerSample
	p[36] = 'd'; p[37] = 'a'; p[38] = 't'; p[39] = 'a';					// 'data' ckID
	p[40] = (unsigned char)dataSize; p[41] = (unsigned char)(dataSize >> 8); p[42] = (unsigned char)(dataSize >> 16); p[43] = (unsigned char)(dataSize >> 24); // ckSize			
	return headerSize;
}

unsigned char *readFile(const char *filename, size_t *outLength)
{
	FILE *fp = fopen(filename, "rb");
	if (fp == NULL) { perror("ERROR: Problem opening input file"); return NULL; }
	if (fseek(fp, 0, SEEK_END) != 0) { perror("ERROR: Problem seeking input file to end"); fclose(fp); return NULL; }
	long length = ftell(fp);
	if (length < 0) { perror("ERROR: Problem determining file length"); fclose(fp); return NULL; }
	if (fseek(fp, 0, SEEK_SET) != 0) { perror("ERROR: Problem seeking input file to start"); fclose(fp); return NULL; }
	unsigned char *buffer = (unsigned char *)malloc((size_t)length);
	if (buffer == NULL) { perror("ERROR: Problem allocating memory for input file"); fclose(fp); return NULL; }
	size_t lengthRead = fread(buffer, 1, (size_t)length, fp);
	fclose(fp);
	if (lengthRead != length) { perror("ERROR: Problem reading input file"); free(buffer); return NULL; }
	*outLength = lengthRead;
	return buffer;
}


// Buffer sizes required (user must add 'alignment' bytes if they want padding)
#define ZIP_WRITER_MAX_PATH 256
#define ZIP_WRITER_SIZE_HEADER (46 + ZIP_WRITER_MAX_PATH)
#define ZIP_WRITER_SIZE_MAX    (ZIP_WRITER_SIZE_HEADER + 4) //// max(header, footer, directory) +4 bytes for "extra field" padding header, user must add 'alignment' bytes to this length

// ZIP Timestamp
#define ZIP_DATETIME(_year, _month, _day, _hours, _minutes, _seconds) ( (((unsigned long)((_year) - 1980) & 0x7f) << 25) | (((unsigned long)(_month) & 0x0f) << 21) | (((unsigned long)(_day) & 0x1f) << 16) | (((unsigned long)(_hours) & 0x1f) << 11) | (((unsigned long)(_minutes) & 0x3f) << 5) | (((unsigned long)(_seconds) & 0x3f) >> 1))

// File state tracking
typedef struct zipwriter_file_tag_t
{
//private:
	const char *filename;				// pointer to filename (must be valid when central directory entry is written)
	unsigned long offset;				// start file offset
	unsigned long modified;				// modified date
	unsigned long length;				// file length
	int extraFieldLength;				// extra field length
	unsigned long crc;					// CRC
	struct zipwriter_file_tag_t *next;	// next element in linked list
} zipwriter_file_t;

// ZIP writer state tracking
typedef struct
{
//private:
	unsigned long length;				// overall ZIP file length
	int numFiles;						// number of files
	zipwriter_file_t *files;			// linked list of files
	zipwriter_file_t *lastFile;			// tail of linked list
	zipwriter_file_t *currentFile;		// current file being written
	zipwriter_file_t *centralDirectoryFile;	// current central directory file entry being written
	int centralDirectoryEntries;
	unsigned long centralDirectoryOffset;
	unsigned long centralDirectorySize;
} zipwriter_t;

// Karl Malbrain's compact CRC-32. See "A compact CCITT crc16 and crc32 C implementation that balances processor cache usage against speed": http://www.geocities.com/malbrain/
// This implementation by Rich Geldreich <richgel99@gmail.com> from miniz.c (public domain zlib-subset - "This is free and unencumbered software released into the public domain")
#define CRC32_INIT (0)
static unsigned long crc32(unsigned long crc, const unsigned char *ptr, size_t buf_len)
{
	static const unsigned long s_crc32[16] = { 0, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c, 0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c, 0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c };
	unsigned long crcu32 = (unsigned long)crc;
	if (!ptr) return CRC32_INIT;
	crcu32 = (~crcu32) & 0xfffffffful;
	while (buf_len--)
	{
		unsigned char b = *ptr++;
		crcu32 = (crcu32 >> 4) ^ s_crc32[(crcu32 & 0xF) ^ (b & 0xF)];
		crcu32 = (crcu32 >> 4) ^ s_crc32[(crcu32 & 0xF) ^ (b >> 4)];
	}
	return (~crcu32) & 0xfffffffful;
}

void ZIPWriterInitialize(zipwriter_t *context)
{
	// Clear context
	memset(context, 0, sizeof(zipwriter_t));
}

// Generate the ZIP local header for a file
int ZIPWriterStartFile(zipwriter_t *context, zipwriter_file_t *file, const char *filename, unsigned long modified, int alignment, void *buffer)
{
	// Start writing header
	unsigned char *p = buffer;

	// Initialize file structure
	memset(file, 0, sizeof(zipwriter_file_t));
	file->filename = filename;		// TODO: Check length
	file->modified = modified;
	file->offset = context->length;
	file->extraFieldLength = 0;

	// Calculate extra field length to match alignment
	if (alignment > 0)
	{
		size_t contentOffset = file->offset + 30 + strlen(file->filename);
		size_t alignedOffset = (contentOffset + alignment - 1) / alignment * alignment;
		if (contentOffset != alignedOffset)
		{
			file->extraFieldLength = (int)(alignedOffset - contentOffset);
		}
	}

	// Start file list, or append if we've already got one
	if (context->centralDirectoryFile == NULL) { context->centralDirectoryFile = file; }
	if (context->files == NULL)
	{
		context->files = file;
	}
	else
	{
		context->lastFile->next = file;
	}

	// Update file pointer
	context->currentFile = file;
	context->lastFile = file;
	context->numFiles++;

	// Local header
	p[0] = 0x50; p[1] = 0x4b; p[2] = 0x03; p[3] = 0x04; // Local file header signature
	p[4] = 0x14; p[5] = 0x00;					// Version needed to extract
	p[6] = 0x00 | (1 << 3); p[7] = 0x00;			// Flags (b3 = data descriptor)
	p[8] = 0; p[9] = 0;							// Compression method (0=store, 8=deflated)
	p[10] = (unsigned char)(context->currentFile->modified); p[11] = (unsigned char)(context->currentFile->modified >> 8);					// Modification time
	p[12] = (unsigned char)(context->currentFile->modified >> 16); p[13] = (unsigned char)(context->currentFile->modified >> 24);			// Modification date
	p[14] = 0; p[15] = 0; p[16] = 0; p[17] = 0;	// CRC32
	p[18] = 0; p[19] = 0; p[20] = 0; p[21] = 0;	// Compressed size
	p[22] = 0; p[23] = 0; p[24] = 0; p[25] = 0;	// Uncompressed size
	p[26] = (unsigned char)strlen(context->currentFile->filename); p[27] = (unsigned char)(strlen(context->currentFile->filename) >> 8);		// Filename length
	p[28] = (unsigned char)(context->currentFile->extraFieldLength); p[29] = (unsigned char)(context->currentFile->extraFieldLength >> 8);	// Extra field length
	memcpy(p + 30, context->currentFile->filename, strlen(context->currentFile->filename));
	p += 30 + strlen(context->currentFile->filename);

	// Extra field (padding)
	if (file->extraFieldLength > 0)
	{
		memset(p, 0, file->extraFieldLength);
		p += file->extraFieldLength;
	}

	context->currentFile->crc = CRC32_INIT;

	context->length += (int)((char *)p - (char *)buffer);
	return (int)((char *)p - (char *)buffer);
}

// Update the context with the ZIP file data
void ZIPWriterFileContent(zipwriter_t *context, const void *data, size_t length)
{
	// Update CRC
	context->currentFile->crc = crc32(context->currentFile->crc, data, length);

	// Update file and archive lengths
	context->currentFile->length += (unsigned long)length;
	context->length += (unsigned long)length;
}

// Generate the ZIP local header for a file
int ZIPWriterEndFile(zipwriter_t *context, void *buffer)
{
	// Start writing header
	unsigned char *p = (unsigned char *)buffer;

	// Extended local header
	p[0] = 0x50; p[1] = 0x4b; p[2] = 0x07; p[3] = 0x08; // Extended local file header signature
	p[4] = (unsigned char)(context->currentFile->crc); p[5] = (unsigned char)(context->currentFile->crc >> 8); p[6] = (unsigned char)(context->currentFile->crc >> 16); p[7] = (unsigned char)(context->currentFile->crc >> 24);	// CRC32
	p[8] = (unsigned char)(context->currentFile->length); p[9] = (unsigned char)(context->currentFile->length >> 8); p[10] = (unsigned char)(context->currentFile->length >> 16); p[11] = (unsigned char)(context->currentFile->length >> 24);	// Compressed size
	p[12] = (unsigned char)(context->currentFile->length); p[13] = (unsigned char)(context->currentFile->length >> 8); p[14] = (unsigned char)(context->currentFile->length >> 16); p[15] = (unsigned char)(context->currentFile->length >> 24);	// Uncompressed size
	p += 16;

	context->length += (int)((char *)p - (char *)buffer);
	return (int)((char *)p - (char *)buffer);
}

// Generate a ZIP central directory entry
int ZIPWriterCentralDirectoryEntry(zipwriter_t *context, void *buffer)
{
	// Start writing header
	unsigned char *p = (unsigned char *)buffer;

	// Is this the start of the central directory?
	if (context->centralDirectoryEntries <= 0)
	{
		context->centralDirectoryOffset = context->length;
		context->centralDirectorySize = 0;
		//context->centralDirectoryFile = context->files;
	}

	// Are there any remaining entries?
	if (context->centralDirectoryEntries >= context->numFiles || context->centralDirectoryFile == NULL)
	{
		return 0;
	}

	// Starting this entry
	context->centralDirectoryEntries++;

	// Central directory
	p[0] = 0x50; p[1] = 0x4b; p[2] = 0x01; p[3] = 0x02; // Central directory
	p[4] = 0x14; p[5] = 0x00;					// Version made by
	p[6] = 0x14; p[7] = 0x00;					// Version needed to extract
	p[8] = (1 << 3); p[9] = 0x00;				// General purpose bit flag
	p[10] = 0; p[11] = 0;						// Compression method (0=store, 8=deflated)
	p[12] = (unsigned char)(context->centralDirectoryFile->modified); p[13] = (unsigned char)(context->centralDirectoryFile->modified >> 8);					// Modification time
	p[14] = (unsigned char)(context->centralDirectoryFile->modified >> 16); p[15] = (unsigned char)(context->centralDirectoryFile->modified >> 24);			// Modification date
	p[16] = (unsigned char)(context->centralDirectoryFile->crc); p[17] = (unsigned char)(context->centralDirectoryFile->crc >> 8); p[18] = (unsigned char)(context->centralDirectoryFile->crc >> 16); p[19] = (unsigned char)(context->centralDirectoryFile->crc >> 24);	// CRC32
	p[20] = (unsigned char)(context->centralDirectoryFile->length); p[21] = (unsigned char)(context->centralDirectoryFile->length >> 8); p[22] = (unsigned char)(context->centralDirectoryFile->length >> 16); p[23] = (unsigned char)(context->centralDirectoryFile->length >> 24);	// Compressed size
	p[24] = (unsigned char)(context->centralDirectoryFile->length); p[25] = (unsigned char)(context->centralDirectoryFile->length >> 8); p[26] = (unsigned char)(context->centralDirectoryFile->length >> 16); p[27] = (unsigned char)(context->centralDirectoryFile->length >> 24);	// Uncompressed size
	p[28] = (unsigned char)strlen(context->centralDirectoryFile->filename); p[29] = (unsigned char)(strlen(context->centralDirectoryFile->filename) >> 8);	// Filename length
	p[30] = (unsigned char)(context->centralDirectoryFile->extraFieldLength); p[31] = (unsigned char)(context->centralDirectoryFile->extraFieldLength >> 8);// Extra field length
	p[32] = 0; p[33] = 0;						// File comment length
	p[34] = 0; p[35] = 0;						// Disk number start
	p[36] = 0; p[37] = 0;						// Internal file attributes
	p[38] = 0; p[39] = 0; p[40] = 0; p[41] = 0;	// External file attributes
	p[42] = (unsigned char)(context->centralDirectoryFile->offset); p[43] = (unsigned char)(context->centralDirectoryFile->offset >> 8); p[44] = (unsigned char)(context->centralDirectoryFile->offset >> 16); p[45] = (unsigned char)(context->centralDirectoryFile->offset >> 24);	// Relative offset of local header
	memcpy(p + 46, context->centralDirectoryFile->filename, strlen(context->centralDirectoryFile->filename));
	p += 46 + strlen(context->centralDirectoryFile->filename);

	// Extra field (padding)
	if (context->centralDirectoryFile->extraFieldLength > 0)
	{
		memset(p, 0, context->centralDirectoryFile->extraFieldLength);
		p += context->centralDirectoryFile->extraFieldLength;
	}

	// Advance to the next entry
	context->centralDirectoryFile = context->centralDirectoryFile->next;

	context->centralDirectorySize += (int)((char *)p - (char *)buffer);
	context->length += (int)((char *)p - (char *)buffer);
	return (int)((char *)p - (char *)buffer);
}

// Generate the ZIP central directory end
int ZIPWriterCentralDirectoryEnd(zipwriter_t *context, void *buffer)
{
	// Start writing header
	unsigned char *p = buffer;

	// Local header
	p[0] = 0x50; p[1] = 0x4b; p[2] = 0x05; p[3] = 0x06; // End of central directory header
	p[4] = 0x00; p[5] = 0x00;					// Number of this disk
	p[6] = 0x00; p[7] = 0x00;					// Number of the disk with the start of the central directory
	p[8] = context->numFiles; p[9] = 0;			// Number of entries in the central directory on this disk
	p[10] = context->numFiles; p[11] = 0;		// Total number of entries in the central directory
	
	p[12] = (unsigned char)(context->centralDirectorySize); p[13] = (unsigned char)(context->centralDirectorySize >> 8); p[14] = (unsigned char)(context->centralDirectorySize >> 16); p[15] = (unsigned char)(context->centralDirectorySize >> 24);			// Size of the central directory
	p[16] = (unsigned char)(context->centralDirectoryOffset); p[17] = (unsigned char)(context->centralDirectoryOffset >> 8); p[18] = (unsigned char)(context->centralDirectoryOffset >> 16); p[19] = (unsigned char)(context->centralDirectoryOffset >> 24);	// Offset of the start of the central directory from the starting disk
	p[20] = 0; p[21] = 0;						// ZIP file comment length
	p += 22;

	context->length += (int)((char *)p - (char *)buffer);
	return (int)((char *)p - (char *)buffer);
}

#define ZIP_READ_WORD(p) ((p)[0] | ((p)[1] << 8))
#define ZIP_READ_DWORD(p) ((p)[0] | ((p)[1] << 8) | ((p)[2] << 16) | ((p)[3] << 24))
#define ZIP_WRITE_WORD(p, v) (((p)[0]) = ((v) & 0xff), ((p)[1]) = (((v) >> 8) & 0xff))
#define ZIP_WRITE_DWORD(p, v) (((p)[0]) = ((v) & 0xff), ((p)[1]) = (((v) >> 8) & 0xff), ((p)[2]) = (((v) >> 16) & 0xff), ((p)[3]) = (((v) >> 24) & 0xff))

bool isZip(unsigned char *data, size_t length)
{
	// Check End of central directory record (EOCD) -- only works if no file comment (could scan)
	if (length < 22) { return false; }
	unsigned char *eocd = data + length - 22;
	if (ZIP_READ_DWORD(eocd) != 0x06054b50) { return false; }
	return true;
}

// Convert entries to data descriptor/extended local header.
bool zipConvert(unsigned char **data, size_t *length)
{
	// Check End of central directory record (EOCD) -- only works if no file comment (could scan)
	if (*length < 22) { fprintf(stderr, "ERROR: ZIP file too small.\n"); return false; }
	size_t eocd = *length - 22;
	if (ZIP_READ_DWORD(*data + eocd) != 0x06054b50)
	{
		fprintf(stderr, "ERROR: ZIP file not valid or not supported (ZIP files with whole-file comment are not currently supported).\n");
		return false;
	}
	int numRecords = ZIP_READ_WORD(*data + eocd + 8);	// Number of central directory records on this disk
	size_t cd = ZIP_READ_DWORD(*data + eocd + 16);		// Offset of start of central directory

	typedef struct
	{
		bool patch;
		size_t entry;
		size_t localFile;
		size_t crc32;
		size_t compressedSize;
		size_t uncompressedSize;
		size_t filenameSize;
		size_t extraFieldSize;
	} fileinfo_t;

	fileinfo_t *files = (fileinfo_t *)malloc(numRecords * sizeof(fileinfo_t));
	memset(files, 0x00, numRecords * sizeof(fileinfo_t));
	size_t entryPosition = 0;
	int countPatched = 0;
	for (int i = 0; i < numRecords; i++)
	{
		unsigned char *entry = *data + cd + entryPosition;
		if (entry < *data || entry + 46 > *data + *length)
		{
			fprintf(stderr, "ERROR: Convert ZIP internal file positions are not valid (while scanning entry #%d).\n", i + 1);
			return false;
		}
		if (ZIP_READ_DWORD(entry) != 0x02014b50)
		{
			fprintf(stderr, "ERROR: Convert ZIP file central directory entry #%d not valid.\n", i + 1);
			return false;
		}

		int fileNameLength = ZIP_READ_WORD(entry + 28);		// File name length (NOTE: 7-Zip uses local name, while Windows uses central name)
		int extraFieldLength = ZIP_READ_WORD(entry + 30);	// Extra field length
		int fileCommentLength = ZIP_READ_WORD(entry + 32);	// File comment length

		files[i].patch = !(entry[8] & (1 << 3));
		if (files[i].patch) { countPatched++; }
		files[i].entry = cd + entryPosition;
		files[i].localFile = (size_t)ZIP_READ_DWORD(entry + 42);			// Relative offset of local file header
		files[i].filenameSize = fileNameLength;
		files[i].extraFieldSize = extraFieldLength;

		// Advance to next central directory entry
		entryPosition += 46 + fileNameLength + extraFieldLength + fileCommentLength;
	}

	if (countPatched <= 0)
	{
		fprintf(stderr, "INFO: No entries to convert (of %d)\n", numRecords);
		return true;
	}
	fprintf(stderr, "INFO: Converting %d/%d entries(s)\n", countPatched, numRecords);
	size_t overallOffset = countPatched * 16;
	size_t newLength = *length + overallOffset;
	unsigned char *newBuffer = malloc(newLength);
	memset(newBuffer, 0, newLength);

	for (int i = 0; i < numRecords; i++)
	{
		fileinfo_t *file = &files[i];

		// Eek, O(n^2) algorithm, should sort entries (central directory entries may unordered).
		int offset = 0;
		for (int j = 0; j < numRecords; j++)
		{
			if (file[j].localFile < file->localFile && file[j].patch)
			{
				offset += 16;
			}
		}

		// Adjust central directory local file offset
fprintf(stderr, "INFO: Converting entry %d: adjusting by offset %u (altering this entry: %s)\n", i + 1, (unsigned int)offset, file->patch ? "yes" : "no");
		ZIP_WRITE_DWORD(*data + file->entry + 42, file->localFile + offset);
		if (file->patch)
		{
			// Patching: add to general purpose bit flag
			*(*data + file->entry + 8) |= (1 << 3);  // General purpose bit flag
		}

		unsigned char *localEntry = *data + file->localFile;
		if (ZIP_READ_DWORD(localEntry) != 0x04034b50)
		{
			fprintf(stderr, "ERROR: Convert ZIP file local file header #%d not valid.\n", i + 1);
			return false;
		}

		file->crc32 = ZIP_READ_DWORD(localEntry + 14);
		file->compressedSize = ZIP_READ_DWORD(localEntry + 18);
		file->uncompressedSize = ZIP_READ_DWORD(localEntry + 22);
		file->filenameSize = ZIP_READ_WORD(localEntry + 26);
		file->extraFieldSize = ZIP_READ_WORD(localEntry + 28);

		if (file->patch)
		{
			localEntry[6] |= (1 << 3); 				// Flags (b3 = data descriptor)			
			ZIP_WRITE_DWORD(localEntry + 14, 0);	// clear CRC
			ZIP_WRITE_DWORD(localEntry + 18, 0);	// clear compressed size
			ZIP_WRITE_DWORD(localEntry + 22, 0);	// clear uncompressed size
		}

		// Copy entry and file
		size_t entrySize = 30 + file->filenameSize + file->extraFieldSize + file->compressedSize;
		memcpy(newBuffer + file->localFile + offset, *data + file->localFile, entrySize);

		// Add extended local file header
		if (file->patch)
		{
			unsigned char *extHeader = newBuffer + file->localFile + offset + entrySize;
			ZIP_WRITE_DWORD(extHeader + 0, 0x08074b50); // Extended local file header signature
			ZIP_WRITE_DWORD(extHeader + 4, file->crc32);
			ZIP_WRITE_DWORD(extHeader + 8, file->compressedSize);
			ZIP_WRITE_DWORD(extHeader + 12, file->uncompressedSize);
		}
	}

fprintf(stderr, "INFO: Post-conversion adjustment of EOCD CD position by %u\n", (unsigned int)overallOffset);
	ZIP_WRITE_DWORD(*data + eocd + 16, cd + overallOffset);	// Patch central directory position
	// Copy CD
	memcpy(newBuffer + cd + overallOffset, *data + cd, *length - cd);
	free(*data);
	*data = newBuffer;
	*length = newLength;
	return true;
}


// Patch the offsets in the specified ZIP file data's central directory
bool zipOffsets(unsigned char **data, size_t *length, size_t headerSize, size_t commentPad)
{
	size_t offset = headerSize; 

	fprintf(stderr, "INFO: Offsetting .ZIP by %u (+%u end comment)\n", (unsigned int)headerSize, (unsigned int)commentPad);

	// Check End of central directory record (EOCD) -- only works if no file comment (could scan)
	if (*length < 22) { fprintf(stderr, "ERROR: ZIP file too small.\n"); return false; }
	size_t eocd = *length - 22;
	if (ZIP_READ_DWORD(*data + eocd) != 0x06054b50)
	{
		fprintf(stderr, "ERROR: ZIP file not valid or not supported (ZIP files with whole-file comment are not currently supported).\n");
		return false;
	}
	int numRecords = ZIP_READ_WORD(*data + eocd + 8);	// Number of central directory records on this disk
	size_t cd = ZIP_READ_DWORD(*data + eocd + 16);		// Offset of start of central directory

	size_t entryPosition = 0;
	for (int i = 0; i < numRecords; i++)
	{
		unsigned char *entry = *data + cd + entryPosition;
		if (entry < *data || entry + 46 > *data + *length)
		{
			fprintf(stderr, "ERROR: ZIP internal file positions are not valid (while scanning entry #%d).\n", i + 1);
			return false;
		}
		if (ZIP_READ_DWORD(entry) != 0x02014b50)
		{
			fprintf(stderr, "ERROR: ZIP file central directory entry #%d not valid.\n", i + 1);
			return false;
		}

		int fileNameLength = ZIP_READ_WORD(entry + 28);		// File name length (NOTE: 7-Zip uses local name, while Windows uses central name)
		int extraFieldLength = ZIP_READ_WORD(entry + 30);	// Extra field length
		int fileCommentLength = ZIP_READ_WORD(entry + 32);	// File comment length
		int localFile = ZIP_READ_DWORD(entry + 42);			// Relative offset of local file header

		// Patch local file offset position
		ZIP_WRITE_DWORD(entry + 42, localFile + offset);

		// Advance to next central directory entry
		entryPosition += 46 + fileNameLength + extraFieldLength + fileCommentLength;
	}

	ZIP_WRITE_DWORD(*data + eocd + 16, cd + offset);	// Patch central directory position
	ZIP_WRITE_WORD(*data + eocd + 20, commentPad);		// Add comment length

	return true;
}


unsigned char *generateBmp(size_t contentsLength, size_t *outHeaderSize)
{
	// Fixed width and bits-per-pixel
	int width = 1024;
	int bpp = 24;
	int span = 4 * ((width * ((bpp + 7) / 8) + 3) / 4);

	// Determine height from file size
	int height = (((int)contentsLength + span - 1) / span);
	size_t fileSize = BMP_WRITER_SIZE_HEADER + height * span;
	size_t headerSize = fileSize - contentsLength;
	fprintf(stderr, "ZIPPAST: BMP: Read %u, Header %u, Total %u, Image %ux%u (span %u).\n", (unsigned int)contentsLength, (unsigned int)headerSize, (unsigned int)(contentsLength + headerSize), width, height, span);

	// Create header
	unsigned char *header = (unsigned char *)malloc(headerSize);
	if (header == NULL) { perror("ERROR: Problem allocating header buffer"); return NULL; }
	memset(header, 0, headerSize);
	BitmapWriteHeader(header, width, -height, bpp);
	*outHeaderSize = headerSize;
	return header;
}

unsigned char *generateWav(size_t contentsLength, size_t *outHeaderSize)
{
	unsigned int bitsPerSample = 8;
	unsigned int chans = 1;
	unsigned int freq = 44100;
	unsigned int bytesPerSample = (bitsPerSample + 7) / 8;
	unsigned int align = bytesPerSample * chans;
	unsigned int numSamples = (((unsigned int)contentsLength + align - 1) / align) * align;
	if (numSamples & 1) { numSamples++; }	// instead of trailing padding byte (as we are 1-channel, 8-bit samples)
	unsigned int dataSize = numSamples * chans * bytesPerSample;
	//if (dataSize & 1) dataSize++;		// trailing padding byte
	size_t fileSize = WAV_WRITER_SIZE_HEADER + dataSize;
	size_t headerSize = fileSize - contentsLength;

	fprintf(stderr, "ZIPPAST: WAV: Read %u, Header %u, Total %u, Samples %u.\n", (unsigned int)contentsLength, (unsigned int)headerSize, (unsigned int)(contentsLength + headerSize), numSamples);

	// Create header
	unsigned char *header = (unsigned char *)malloc(headerSize);
	if (header == NULL) { perror("ERROR: Problem allocating header buffer"); return NULL; }
	memset(header, 0, headerSize);
	WavWriteHeader(header, bitsPerSample, chans, freq, numSamples);
	*outHeaderSize = headerSize;
	return header;
}

// TODO: The attached .ZIP file has incorrect offsets in the central directory -- try to solve this with a second central directory within the main part of the .ZIP file
unsigned char *generateEml(size_t contentsLength, size_t *outHeaderSize, unsigned char *comment, size_t commentPad)
{
	const char *emlHeader =
		"From: <zippast>\r\n"
		//"Snapshot-Content-Location: zippast\r\n"
		"Subject: ZIP File Attached\r\n"
		//"Date: Thu, 01 Jan 1970 00:00:00 +0000\r\n"
		//"MIME-Version: 1.0\r\n"
		"Content-Type: multipart/related; type=\"text/html\"; boundary=\"" MULTIPART_BOUNDARY "\"\r\n"
		"\r\n"
		"--" MULTIPART_BOUNDARY "\r\n"
		"Content-Type: text/html\r\n"
		//"Content-ID: <_index>\r\n"
		//"Content-Transfer-Encoding: binary\r\n"
		//"Content-Location: index.html\r\n"
		"\r\n"
		"<!doctype html>\r\n"
		"<html>\r\n"
		"<head>\r\n"
		"<meta charset='ISO-8859-1'>\r\n"
		"</head>\r\n"
		"<body>\r\n"
		"Please open the attachment (or rename this .eml file to .zip)\r\n"
		"</body>\r\n"
		"</html>\r\n"
		"\r\n"
		"--" MULTIPART_BOUNDARY "\r\n"
		"Content-Type: application/zip\r\n"
		//"Content-ID: <_zip>\r\n"
		"Content-Disposition: attachment; filename=\"file.zip\"\r\n"
		"Content-Transfer-Encoding: binary\r\n"
		"Content-Length: %u\r\n"
		"\r\n"
	;

	const char *emlFooterStart = ""
		// "\r\n"
		// "\r\n"
		// "--" MULTIPART_BOUNDARY "\r\n"
		// "Content-Type: text/plain\r\n"
		// "\r\n"
	;
	const char *emlFooterEnd =
		//"\r\n"
		//"\r\n"
		"--" MULTIPART_BOUNDARY "--\r\n"
	;

	if (commentPad >= strlen(emlFooterStart)) {
		memcpy(comment, emlFooterStart, strlen(emlFooterStart));
		size_t ofs = strlen(emlFooterStart), count = commentPad - strlen(emlFooterStart), mod = strlen(TEXT_STRING);
		if (mod > 0) {
			for (size_t i = 0; i < count; i++) {
				comment[ofs + i] = TEXT_STRING[i % mod];
			}
		}
	}
	if (commentPad >= strlen(emlFooterStart) + strlen(emlFooterEnd)) {
		memcpy(comment + commentPad - strlen(emlFooterEnd), emlFooterEnd, strlen(emlFooterEnd));
	}

	unsigned char *header = (unsigned char *)malloc(strlen(emlHeader) + 512);
	if (header == NULL) { perror("ERROR: Problem allocating header buffer"); return NULL; }
	*outHeaderSize = sprintf((char *)header, emlHeader, contentsLength);
	return header;
}

// TODO: Find a way of downloading the attached file (e.g. represent as a BMP and render to canvas if not tainted?), also solve the offset issue as with .eml files (e.g. a second central directory?)
unsigned char *generateMhtml(size_t contentsLength, size_t *outHeaderSize, unsigned char *comment, size_t commentPad)
{
	const char *mhtmlHeader =
		//"From: <zippast>\r\n"
		//"Snapshot-Content-Location: zippast\r\n"
		//"Subject: ZIP File Attached\r\n"
		//"Date: Thu, 01 Jan 1970 00:00:00 +0000\r\n"
		//"MIME-Version: 1.0\r\n"
		"Content-Type: multipart/related; type=\"text/html\"; boundary=\"" MULTIPART_BOUNDARY "\"\r\n"
		"\r\n"
		"--" MULTIPART_BOUNDARY "\r\n"
		"Content-Type: text/html\r\n"
		//"Content-ID: <_index>\r\n"
		//"Content-Transfer-Encoding: binary\r\n"
		//"Content-Location: index.html\r\n"
		"\r\n"
		"<!doctype html>\r\n"
		"<html>\r\n"
		"<head>\r\n"
		"<meta charset='ISO-8859-1'>\r\n"
		"</head>\r\n"
		"<body>\r\n"
		"<embed type='application/zip' src='cid:_zip'>...</embed>\r\n"
		"</body>\r\n"
		"</html>\r\n"
		"\r\n"
		"--" MULTIPART_BOUNDARY "\r\n"
		"Content-Type: application/zip\r\n"
		"Content-ID: <_zip>\r\n"
		"Content-Disposition: attachment; filename=\"file.zip\"\r\n"
		"Content-Transfer-Encoding: binary\r\n"
		"Content-Length: %u\r\n"
		"\r\n"
	;

	const char *mhtmlFooterStart = ""
		// "\r\n"
		// "\r\n"
		// "--" MULTIPART_BOUNDARY "\r\n"
		// "Content-Type: text/plain\r\n"
		// "\r\n"
	;
	const char *mhtmlFooterEnd =
		//"\r\n"
		//"\r\n"
		"--" MULTIPART_BOUNDARY "--\r\n"
	;

	if (commentPad >= strlen(mhtmlFooterStart)) {
		memcpy(comment, mhtmlFooterStart, strlen(mhtmlFooterStart));
		size_t ofs = strlen(mhtmlFooterStart), count = commentPad - strlen(mhtmlFooterStart), mod = strlen(TEXT_STRING);
		if (mod > 0) {
			for (size_t i = 0; i < count; i++) {
				comment[ofs + i] = TEXT_STRING[i % mod];
			}
		}
	}
	if (commentPad >= strlen(mhtmlFooterStart) + strlen(mhtmlFooterEnd)) {
		memcpy(comment + commentPad - strlen(mhtmlFooterEnd), mhtmlFooterEnd, strlen(mhtmlFooterEnd));
	}

	unsigned char *header = (unsigned char *)malloc(strlen(mhtmlHeader) + 512);
	if (header == NULL) { perror("ERROR: Problem allocating header buffer"); return NULL; }
	*outHeaderSize = sprintf((char *)header, mhtmlHeader, contentsLength);
	return header;
}

unsigned char *generateHtml(size_t contentsLength, size_t *outHeaderSize)
{
	const char *htmlHeader =
		"<!doctype html>\n"
		"<html>\n"
		"<head>\n"
		"<meta charset='ISO-8859-1'><!-- or windows-1252 -->\n"
		"</head>\n"
		"<body>\n"
		"<p><a href='' download='file.zip' type='application/zip' target='iframe'>Download</a> - right-click and <em>Save link as</em>, ensure <code>.zip</code> is added to the file name, <em>Save</em>, then open it.</p>\n"
		"<iframe src='about:blank' name='iframe' hidden></iframe>\n"
		"</body>\n"
		"<script>\n"
		"\n"
		"// Check for failed download\n"
		"const iframe = document.querySelector('IFRAME');\n"
		"let firstLoad = true;\n"
		"iframe.addEventListener('load', function() {\n"
		"  if (!firstLoad) {\n"
		"    console.error('download failed');\n"
		"  }\n"
		"  firstLoad = false;\n"
		"});\n"
		"iframe.src = 'about:blank';\n"
		"  \n"
		"document.addEventListener('DOMContentLoaded', function () {\n"
		"  // Chrome does not use the anchor 'download' attribute to name the file, so this is to prevent a new tab (or loop when auto-clicking)\n"
		"  if (window.top != window.self) return;\n"
		"\n"
		"  // Find filename and .zip version\n"
		"  const pathname = window.location.pathname;\n"
		"  const srcFilename = pathname.slice(pathname.lastIndexOf('/') + 1);\n"
		"  const filename = ((srcFilename.slice(-5).toLowerCase() == '.html') ? srcFilename.slice(0, -5) : srcFilename) + '.zip';\n"
		"  \n"
		"  // Set the anchor tag download attribute (Chrome will ignore this on the local filesystem)\n"
		"  const anchorElement = document.querySelector('A');\n"
		"  anchorElement.setAttribute('href', srcFilename);\n"
		"  anchorElement.setAttribute('download', filename);\n"
		"  \n"
		"  // It would be nice to download the binary contents directly, but the HTML parsing rules mangle the data (e.g. normalize CRLFs)\n"
		"  // document.querySelector('PLAINTEXT');\n"
		"\n"
		"  // Some browsers will not use the 'download' name from 'file:' URLs (but will from 'blob:' URLs)\n"
		"  // If allowed to fetch from 'file:' URLs (e.g. Firefox), get the file contents and use this as a blob\n"
		"  const fetchUrl = anchorElement.href;\n"
		"  fetch(fetchUrl)\n"
		"  .then((response) => response.blob())\n"
		"  .then((blob) => {\n"
		"    const blobUrl = URL.createObjectURL(blob);\n"
		"    anchorElement.setAttribute('href', blobUrl);\n"
		"    anchorElement.removeAttribute('target');\n"
		"  })\n"
		"  .catch((e) => {\n"
		"    console.error(e);\n"
		"  });\n"
		"\n"
		"  // Automatically click the link to trigger the download on some browsers\n"
		"  anchorElement.click();\n"
		"});\n"
		"</script>\n"
		"<PLAINTEXT HIDDEN>"
	;
	
	size_t headerSize = strlen(htmlHeader);
	unsigned char *header = (unsigned char *)malloc(headerSize);
	if (header == NULL) { perror("ERROR: Problem allocating header buffer"); return NULL; }
	*outHeaderSize = headerSize;
	memcpy(header, htmlHeader, headerSize);
	return header;
}

unsigned char *zipFile(const char *filename, const unsigned char *contents, size_t contentsLength, size_t *zipLength)
{
	// [
	//   ZIP LOCAL HEADER <30+n>
	//   FILE CONTENT <f>
	//   ZIP EXTENDED LOCAL HEADER <16>
	// ]...
	// ZIP CENTRAL DIRECTORY ENTRY... <46+n>
	// ZIP END CENTRAL DIRECTORY <22>
	size_t length = 30 + strlen(filename) + contentsLength + 16 + 46 + strlen(filename) + 22;
	unsigned char *buffer = malloc(length);
	unsigned char *p = buffer;
	if (buffer == NULL)
	{
		perror("ERROR: Problem allocating zip buffer");
		return NULL;
	}

	zipwriter_t zip;
	ZIPWriterInitialize(&zip);

	zipwriter_file_t file;
	p += ZIPWriterStartFile(&zip, &file, filename, ZIP_DATETIME(2000,1,1,0,0,0), 0, p);
	memcpy(p, contents, contentsLength);
	ZIPWriterFileContent(&zip, contents, contentsLength);
	p += contentsLength;
	p += ZIPWriterEndFile(&zip, p);
	p += ZIPWriterCentralDirectoryEntry(&zip, p);
	p += ZIPWriterCentralDirectoryEnd(&zip, p);

	*zipLength = (size_t)(p - buffer);
	if (*zipLength != length) { fprintf(stderr, "WARNING: Zip output %u, expected %u.\n", (unsigned int)*zipLength, (unsigned int)length); }
	return buffer;
}

const char *findFilename(const char *file)
{
	for (const char *p = file + strlen(file); p >= file; p--)
	{
		char c = *p;
#ifdef _WIN32
		if (c == '\\') c = '/';
#endif
		if (c == '/') return p + 1;
	}
	return file;
}

int process(const char *inputFile, const char *outputFile, HeaderMode mode, size_t commentPad, bool convert)
{
	// Check parameters
	if (commentPad < 0 || commentPad > 0xffff)
	{
		fprintf(stderr, "ZIPPAST: Comment pad out of range (0-65535): %u\n", (unsigned int)commentPad);
		return 1;
	}

	// Read content
	fprintf(stderr, "ZIPPAST: Reading: %s\n", inputFile);
	size_t contentsLength = 0;
	unsigned char *contents = readFile(inputFile, &contentsLength);
	if (contents == NULL) { return 1; }

	// Zip
	if (!isZip(contents, contentsLength))
	{
		const char *filename = findFilename(inputFile);
		fprintf(stderr, "ZIPPAST: Wrapping in ZIP...\n");
		size_t zipLength = 0;
		unsigned char *zipContents = zipFile(filename, contents, contentsLength, &zipLength);
		free(contents);
		contents = zipContents;
		contentsLength = zipLength;
		if (contents == NULL) { return 1; }
	}

	// Convert ZIP file
	if (convert)
	{
		if (!zipConvert(&contents, &contentsLength))
		{
			fprintf(stderr, "ERROR: Problem converting ZIP entries\n");
			free(contents);
			return 1;
		}
	}

	// Additional ZIP comment pad at end of file
	const char *commentString = COMMENT_STRING;
	unsigned char *comment = NULL;
	if (commentPad > 0)
	{
		comment = (unsigned char *)malloc(commentPad);
		if (comment == NULL)
		{
			perror("ERROR: Problem allocating comment memory");
			return 1;
		}
		memset(comment, ' ', commentPad);
		const size_t commentStringLength = strlen(commentString);
		if (commentStringLength > 0)
		{
			for (size_t i = 0; i < commentPad; i++)
			{
				comment[i] = commentString[i % commentStringLength] & 0x7f;	// clear top bit to allow easier control codes
			}
		}
#if 0	// Fake central directory at end (7-Zip ignores, others do not?)
		if (commentPad > 22)
		{
			unsigned char *eocd = comment + commentPad - 22;
			memset(eocd, 0x00, 22);
			eocd[0]  = 0x50; eocd[1]  = 0x4b; eocd[2] = 0x05; eocd[3] = 0x06;	// End of central directory signature
			//eocd[4]  = 0x00; eocd[5]  = 0x00; // Number of this disk
			//eocd[6]  = 0x00; eocd[7]  = 0x00; // Disk where central directory starts
			//eocd[8]  = 0x00; eocd[9]  = 0x00; // Number of central directory records on this disk
			//eocd[10] = 0x00; eocd[11] = 0x00; // Total number of central directory records
			//eocd[12] = 0x00; eocd[13] = 0x00; eocd[14] = 0x00; eocd[15] = 0x00; // Size of central directory(bytes)
			//eocd[16] = 0x00; eocd[17] = 0x00; eocd[18] = 0x00; eocd[19] = 0x00; // Offset of start of central directory, relative to start of archive
			//eocd[20] = 0x00; eocd[21] = 0x00; // Comment length
		}
#endif
	}

	// Generate required header
	size_t headerSize = 0;
	unsigned char *header = NULL;
	if (mode == MODE_BMP)
	{
		header = generateBmp(contentsLength + commentPad, &headerSize);
	}
	else if (mode == MODE_WAV)
	{
		header = generateWav(contentsLength + commentPad, &headerSize);
	}
	else if (mode == MODE_EML) {
		const char *footer = NULL;
		header = generateEml(contentsLength, &headerSize, comment, commentPad);
	}
	else if (mode == MODE_MHTML) {
		header = generateMhtml(contentsLength, &headerSize, comment, commentPad);
	}
	else if (mode == MODE_HTML) {
		header = generateHtml(contentsLength + commentPad, &headerSize);
	}
	else if (mode == MODE_STANDARD)
	{
		const char *data; // = "\x1A";	// DOS EOF
		data = HEADER_STRING;
		header = (unsigned char *)strdup(data);
		for (unsigned char *p = header; *p != 0; p++) *p &= 0x7f;	// clear top bit to allow easier control codes from source
		headerSize = strlen((char *)header);
	}
	else if (mode == MODE_BYTE)
	{
		const char *data; // = "\x1A";	// DOS EOF
		data = HEADER_STRING;
		header = (unsigned char *)strdup(data);
		for (unsigned char *p = header; *p != 0; p++) *p &= 0x7f;	// clear top bit to allow easier control codes from source
		headerSize = strlen((char *)header);
	}
	else  // mode == MODE_NONE
	{
		;
	}
	if (header == NULL && mode != MODE_NONE)
	{
		free(contents);
		return 1;
	}

	// Patch ZIP file
	if (!zipOffsets(&contents, &contentsLength, headerSize, commentPad))
	{
		fprintf(stderr, "ERROR: Problem offsetting ZIP file contents by %u\n", (unsigned int)headerSize);
		free(contents);
		return 1;
	}

	// Write output
	fprintf(stderr, "ZIPPAST: Writing: %s\n", outputFile);
	FILE *fp = stdout;
	if (outputFile[0] != '\0' || !strcmp(outputFile, "-")) fp = fopen(outputFile, "wb");
	if (fp == NULL) { perror("ERROR: Problem opening output file"); free((void *)header); free(contents); return 1; }
	size_t written = 0;
	if (header != NULL)
	{
fprintf(stderr, "OUTPUT: Header: %u\n", (unsigned int)headerSize);
		written += fwrite(header, 1, headerSize, fp);
		free(header);
	}
fprintf(stderr, "OUTPUT: Contents: %u\n", (unsigned int)contentsLength);
	written += fwrite(contents, 1, contentsLength, fp);
	free(contents);
fprintf(stderr, "OUTPUT: Comment: %u\n", (unsigned int)commentPad);
	if (commentPad > 0)
	{
		written += fwrite(comment, 1, commentPad, fp);
		free(comment);
	}
	if (fp != stdout) fclose(fp);
	if (written != headerSize + contentsLength + commentPad)
	{
		fprintf(stderr, "ERROR: Problem writing file contents.\n");
		return 1;
	}

	return 0;
}

// Return a allocated string for the replacement of the extension for the specified file name.
const char *replaceExtension(const char *inputFile, const char *newExt)
{
	char *outputFile = (char *)malloc(strlen(inputFile) + strlen(newExt) + 1);
	if (outputFile == NULL)
	{
		perror("ERROR: Problem creating output file name");
		return NULL;
	}
	strcpy(outputFile, inputFile);
	for (char *p = outputFile + strlen(outputFile); p >= outputFile && *p != '\\' && *p != '/'; p--)
	{
		if (*p == '.') { *p = '\0'; break; }
	}
	strcat(outputFile, newExt);
	return outputFile;
}

int run(int argc, char *argv[])
{
	bool help = false;
	bool convert = false;
	int positional = 0;
	const char *inputFile = NULL;
	const char *outputFile = NULL;
	size_t commentPad = (1<<13) - 22 + 1;		// To push EOCD out of last 8kB: default=8171
	HeaderMode mode = MODE_STANDARD;

	for (int i = 1; i < argc; i++)
	{
		if (!strcmp(argv[i], "--help"))
		{
			help = true;
		}
		else if (!strcmp(argv[i], "-out"))
		{
			outputFile = argv[++i];
		}
		else if (!strcmp(argv[i], "-comment"))
		{
			commentPad = (size_t)strtoul(argv[++i], NULL, 0);
		}
		else if (!strcmp(argv[i], "-mode:none")) { mode = MODE_NONE; }
		else if (!strcmp(argv[i], "-mode:bmp")) { mode = MODE_BMP; }
		else if (!strcmp(argv[i], "-mode:wav")) { mode = MODE_WAV; }
		else if (!strcmp(argv[i], "-mode:eml")) { mode = MODE_EML; }
		else if (!strcmp(argv[i], "-mode:mhtml")) { mode = MODE_MHTML; }
		else if (!strcmp(argv[i], "-mode:html")) { mode = MODE_HTML; }
		else if (!strcmp(argv[i], "-mode:standard")) { mode = MODE_STANDARD; }
		else if (!strcmp(argv[i], "-mode:byte")) { mode = MODE_BYTE; }
		else if (!strcmp(argv[i], "-zip:keep")) { convert = false; }
		else if (!strcmp(argv[i], "-zip:convert")) { convert = true; }
		else if (argv[i][0] == '-')
		{
			fprintf(stderr, "ERROR: Unsupported argument: %s\n", argv[i]);
			help = true;
		}
		else
		{
			if (positional == 0)
			{
				inputFile = argv[i];
			}
			else
			{
				fprintf(stderr, "ERROR: Unexpected positional argument: %s\n", argv[i]);
				help = true;
			}
			positional++;
		}
	}

	if (!help && (inputFile == NULL || strlen(inputFile) <= 0))
	{
		fprintf(stderr, "ERROR: Input file not specified\n");
		help = true;
	}

	if (help)
	{
		printf("Usage: zippast <file.{zip|*}> [-zip:<convert|keep>] [-mode:<standard|byte|none|bmp|wav>] [-comment <size=8171>] [-out <file.{bin|dat|bmp|wav|html}>]\n");
		return 1;
	}

	// Generate an output file based on the input file name
	if (outputFile == NULL)
	{
		const char *newExt;
		if (mode == MODE_BMP) newExt = ".bmp";
		else if (mode == MODE_WAV) newExt = ".wav";
		else if (mode == MODE_EML) newExt = ".eml";
		else if (mode == MODE_MHTML) newExt = ".mht";
		else if (mode == MODE_HTML) newExt = ".html";
		else if (mode == MODE_STANDARD) newExt = ".zip-email";
		else if (mode == MODE_BYTE) newExt = ".bin";
		else newExt = ".dat";	// MODE_NONE
		outputFile = replaceExtension(inputFile, newExt);
		if (outputFile == NULL) return 1;
	}

	int returnValue = process(inputFile, outputFile, mode, commentPad, convert);
	return returnValue;
}

int main(int argc, char *argv[])
{
	int returnValue = run(argc, argv);
#if (defined(_WIN32) && defined(_DEBUG))
	fprintf(stderr, "Press any key to continue . . . ");
	getchar();
#endif
	return returnValue;
}
