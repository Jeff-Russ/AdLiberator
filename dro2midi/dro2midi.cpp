//
// DRO2MIDI - Convert DOSBox raw OPL captures (.dro), Rdos (.raw) and id
//            Software (.imf, .wlf) into MIDI files (.mid)
//
// Created by malvineous@shikadi.net in June 2007.  See README for license.
// Based on imf2midi v1.0 written by Guenter Nagler in 1996 (gnagler@ihm.tu-graz.ac.at)
//
//  v1.0 / 2007-06-16 / malvineous@shikadi.net: Original release
//     - imf2midi with .imf reader hacked to read .dro files instead
//
//  v1.1 / 2007-07-28 / malvineous@shikadi.net: More file formats
//     - Added .imf and .raw support.
//     - Replaced Guenter's OPL -> MIDI frequency conversion algorithm (from a
//       lookup table into a much more accurate formula), consequently was able
//       to simplify pitchbend code (now conversions with pitchbends enabled
//       sound quite good!)
//
//  v1.2 / 2007-07-28 / malvineous@shikadi.net: Bugfix release
//     - Fixed some file length calculations causing some files to be converted
//       without any notes.
//     - Added portamento-to-note for large (>2 semitone) pitchbends, but it
//       doesn't seem to work when using Timidity.
//
//  v1.3 / 2007-09-02 / malvineous@shikadi.net: New features
//     - Fixed "tom tom" incorrectly called "bass drum" in output messages.
//     - Fixed multi-note pitchbends by removing portamento-to-note and
//       adjusting standard pitchbend range instead, thanks to a suggestion
//       by Xky (xkyrauh2001@hotmail.com)
//     - Implemented a better method for reading Adlib register -> MIDI patch
//       mapping information (all stored in inst.txt now instead of having a
//       seperate file for each instrument.)  Also improved method for mapping
//       instruments to percussion on MIDI channel 10.
//     - Fixed OPL rhythm instrument conversion issue (a MIDI noteon was being
//       generated too often - if the OPL instrument is on and we receive
//       another keyon, it *shouldn't* generate a fresh MIDI keyon.)
//     - Fixed IMF type-1 conversion issue where unsigned numbers were being
//       read as signed, and the conversion was cutting off half way through.
//
//  v1.4 / 2009-03-28 / malvineous@shikadi.net
//     - Some code cleanup, fixed all the warnings in midiio.cpp.
//     - Fixed a bunch of char/unsigned char issues, hopefully Win32
//       conversions will now be as reliable as under Linux.
//     - Added line numbers to instrument names and mapping file error messages.
//     - Added new instrument mapping entries for rhythm mode instruments
//       (which previously were hard coded.)
//     - Added transpose and mute options to instrument mapping file.
//     - Added -c option to change OPL constant during conversion, thanks to a
//       suggestion from Wraithverge (liam82067@yahoo.com)
//     - Added -v option to disable note volume (helps with Stunts which
//       otherwise comes out with no notes because they're all silent.)
//     - Corrected OPL volume -> MIDI note velocity algorithm, and added hard
//       limit to prevent notes from having a zero velocity (which stops them
//       from being converted, like with Stunts.)
//     - New instrument mappings (to be copied into insts.txt) are printed to
//       stderr, so "dro2midi 2>> insts.txt" will conveniently append them all
//       to the mapping file.  Thanks to Wraithverge for the idea.
//     - Replaced getop() and getchannel() lookup functions with GET_OP() and
//       GET_CHANNEL() macro algorithms to calculate the values as required.
//     - Included 128 standard GM instrument mappings extracted from Creative
//       Labs' MIDI player (see gen_test_midi.cpp.)
//
//  v1.5 / 2010-03-28 / Wraithverge (liam82067 at yahoo dot com): Changes
//     - Added code for several MIDI Controller Events for output MID-files,
//       and they are the following:
//       Reset Controllers [121] -- At the head of the Event list.
//       Balance (or Pan)   [10] -- At the head of the Event list.
//       Expression         [11] -- At the head of the Event list.
//       All Notes Off     [123] -- At the foot of the Event list.
//       Hopefully, other musicians will find these to be needed, as well.
//     - Added detectors for the (at this time) two DRO formats, and a hint
//       to let us know that the DRO v2.0 format is not supported.
//
//  v1.6 / 2013-11-14 / bsa (http://bsutherland.github.io/JuceOPLVSTi)
//	   - Added -s switch to save detected instruments in Creative Sound
//       Blaster Instrument format (.sbi)
//

#define VERSION           "1.6"
#define MAPPING_FILE      "inst.txt"

#define PATCH_NAME_FILE   "patch.txt"
#define PERC_NAME_FILE    "drum.txt"
#define NUM_MIDI_PATCHES  128  // 128 MIDI instruments
#define NUM_MIDI_PERC     128  // 46 MIDI percussive notes (channel 10), but 128 possible notes
#define INSTR_NAMELEN      32  // Maximum length of an instrument name

//#define PITCHBEND_RANGE 12.0   // 12 == pitchbends can go up to a full octave
#define PITCHBEND_RANGE 24.0   // 24 == pitchbends can go up two full octaves
#define PITCHBEND_ONESEMITONE  (8192.0 / PITCHBEND_RANGE)
const double pitchbend_center = 8192.0;

#include "midiio.hpp"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>

#define WRITE_BINARY  "wb"
#define READ_TEXT     "r"

#ifdef _MSC_VER
// Keep MS VC++ happy
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#define snprintf _snprintf
#define log2(x) (log(x) / log(2.))
inline double round( double d )
{
return floor( d + 0.5 );
}
#endif

bool bRhythm = true; // convert rhythm mode instruments (-r)
bool bUsePitchBends = true; // use pitch bends to better match MIDI note frequency with the OPL frequency (-p)
bool bApproximatePitchbends = false; // if pitchbends are disabled, should we approximate them by playing the nearest note when the pitch changes?
bool bPerfectMatchesOnly = false;  // if true, only match perfect instruments
bool bEnableVolume = true; // enable note velocity based on OPL instrument volume
bool bWriteSbiInstruments = false; // write detected instruments to .SBI files

// Rhythm instruments
enum RHYTHM_INSTRUMENT {
	NormalInstrument,
	BassDrum,
	SnareDrum,
	TomTom,
	TopCymbal,
	HiHat
};

// MIDI channels to use for these instruments when they are mapped as normal
// notes.  Probably best not to use 1-10 as these are used by the rest of the
// OPL mapping code.  Note these are zero-based, so the GM drum channel is
// channel 9 in this context.
#define CHAN_BASSDRUM  10 // Bass drum sits on OPL channel 7
#define CHAN_SNAREDRUM 11 // OPL channel 8 carrier
#define CHAN_TOMTOM    12 // OPL channel 9 modulator
#define CHAN_TOPCYMBAL 13 // OPL channel 9 carrier
#define CHAN_HIHAT     14 // OPL channel 8 modulator

char cPatchName[NUM_MIDI_PATCHES][INSTR_NAMELEN];
char cPercName[NUM_MIDI_PERC][INSTR_NAMELEN];

char* input = 0;
char* output = 0;

FILE* f = 0;  // input file
MidiWrite* write = 0;

//int resolution = 384; // 560Hz IMF
int resolution = 500;   // 1000Hz DRO
int program = 0;
float tempo = 120.0;

// Arrays of [9] refer to OPL channels, arrays of [16] also refer to OPL
// channels but use fake channels (11-15) for rhythm mode instruments (but
// only for elements like keyon and instrument mapping that can happen
// independently of the OPL channel in use.  Things like OPL channel pitch
// which affect both rhythm mode instruments using that channel are not
// stored separately, i.e. they're in the [9] array.)
int mapchannel[16];
int curfreq[9];
bool keyAlreadyOn[16];
int lastkey[16]; // last MIDI key pressed on this channel
int pitchbent[16];
int transpose[16]; // used for instruments with mapped transpose values
int drumnote[16]; // note to play on MIDI channel 10 if Adlib channel has a
	// percussive instrument assigned to it
int lastprog[16]; // last program/patch set on the MIDI channel
bool mute[16]; // true if the instrument on this channel is currently muted

// Statistics
int iNotesActive = 0;
int iPitchbendCount = 0;
int iTotalNotes = 0;

typedef struct
{
	/*unsigned char reg20[2];
	unsigned char reg40[2];
	unsigned char reg60[2];
	unsigned char reg80[2];
	unsigned char regC0;
	unsigned char regE0[2];*/

	unsigned int reg20[2];
	unsigned int reg40[2];
	unsigned int reg60[2];
	unsigned int reg80[2];
	unsigned int regC0;
	unsigned int regE0[2];

	// Is this a normal instrument or a mapping for a rhythm instrument?
	RHYTHM_INSTRUMENT eRhythmInstrument;

	int prog;  // MIDI patch (or -1 if a drum)
	int isdrum;
	int note; // note to play if drum
	bool muted; // true if this instrument is muted
	char name[128];

	signed int iTranspose; // number of semitones to transpose this instrument

	// The instrument can be redirected to another.  This is used when a new
	// instrument is encountered - its info is printed to the screen then it's
	// recorded (pointing to its best match) to prevent it appearing as a new
	// instrument hundreds of times and cluttering the output.
	int redirect; // if >= 0, use ::instr[redirect] instead of this one

	// When using this struct to cache current channel parms we need to
	// remember the octave, so that when registers 0xA0-0xA8 are changed
	// (to change note frequency) we can still pass the octave to the note
	// conversion function (as the octave is otherwise only available when
	// setting registers 0xB0-0xB8.)
	int iOctave;
} INSTRUMENT;

INSTRUMENT reg[9]; // current registers of channel

#define MAXINSTR  2048
int instrcnt = 0;
INSTRUMENT instr[MAXINSTR];

int iFormat = 0; // input format
#define FORMAT_IMF  1
#define FORMAT_DRO  2
#define FORMAT_RAW  3
int iSpeed = 0; // clock speed (in Hz)
int iInitialSpeed = 0; // first iSpeed value written to MIDI header

double dbConversionVal;
// Convert the given OPL F-num and octave values into a fractional MIDI note
// number (for the use of pitchbends.)
double freq2key(int freq, int octave)
{
	int iFNum = freq;
	int iBlock = octave;
	double dbOriginalFreq = dbConversionVal * (double)iFNum * pow(2, (double)(iBlock - 20));
	return 69.0 + 12.0 * log2(dbOriginalFreq / 440.0);
}

void version()
{
  printf("DRO2MIDI v" VERSION " - Convert raw Adlib captures to General MIDI\n"
		"Written by malvineous@shikadi.net in 2007\n"
		"Heavily based upon IMF2MIDI written by Guenter Nagler in 1996\n"
		"With contributions by Wraithverge (C) 2010, bsa in 2013\n"
		"http://www.shikadi.net/utils/\n"
		"\n"
	);
	return;
}

void usage()
{
	version();
  fprintf(stderr,
		"Usage: dro2midi [-p [-a]] [-r] [-i] [-c alt|<num>] [-v] input.dro output.mid\n"
		"\n"
		"Where:\n"
		"  -p   Disable use of MIDI pitch bends\n"
		"  -a   If pitchbends are disabled, approximate by playing the nearest note\n"
		"  -r   Don't convert OPL rhythm-mode instruments\n"
		"  -i   Only use instruments that match perfectly (default is 'close enough is\n"
		"       good enough.') Useful when guessing new patches. Instruments that can't\n"
		"       be matched use the first entry in " MAPPING_FILE " (piano by default)\n"
		"  -c   Change the value used to convert OPL notes into MIDI notes from the\n"
		"       default 49716.  Any non-zero value can be specified.  The special word\n"
		"       \"alt\" means 50000, the other commonly used value.  It is unlikely any\n"
		"       other values will need to be used, unless you get an excessive amount\n"
		"       of artificial pitchbends in the output MIDI.\n"
		"  -v   Disable note velocity and play all notes as loudly as possible,\n"
		"       instead of trying to match the volume of the OPL note.\n"
		"  -s   Write detected instruments to .sbi files\n"
		"       (Creative Sound Blaster Instrument).\n"		
		"\n"
		"Supported input formats:\n"
		" .raw  Rdos RAW OPL capture\n"
		" .dro  DOSBox RAW OPL capture\n"
		" .imf  id Software Music Format (type-0 and type-1 at 560Hz)\n"
		" .wlf  id Software Music Format (type-0 and type-1 at 700Hz)\n"
		"\n"
		"Instrument definitions are read in from " MAPPING_FILE ".  Instrument names\n"
		"are read in from " PATCH_NAME_FILE " and " PERC_NAME_FILE ".  See the README for more details.\n"
	);
  exit(1);
}

/* Channel        1   2   3   4   5   6   7   8   9
 * Operator 1    00  01  02  08  09  0A  10  11  12  } cells
 * Operator 2    03  04  05  0B  0C  0D  13  14  15  }
 */
// Converts a cell into a channel
#define GET_CHANNEL(i)  ((((i) / 8) * 3) + (((i) % 8) % 3))

// Converts a cell into 0 (for operator 1) or 1 (for operator 2)
#define GET_OP(i) (((i) % 8) / 3)

// Helper functions to read data from files in a non-optimised but platform
// independent (little/big endian) way.
inline unsigned char readByte(FILE *f)
{
	unsigned char c;
	fread(&c, 1, 1, f);
	return c;
}
inline unsigned short readUINT16LE(FILE *f)
{
	unsigned char c[2];
	fread(&c, 1, 2, f);
	return c[0] | (c[1] << 8L);
}
inline unsigned long readUINT32LE(FILE *f)
{
	unsigned char c[4];
	fread(&c, 1, 4, f);
	return c[0] | (c[1] << 8L) | (c[2] << 16L) | (c[3] << 24L);
}

bool loadInstruments(void)
{
	for (int i = 0; i < NUM_MIDI_PATCHES; i++) sprintf(cPatchName[i], "Patch #%d", i+1);
	for (int i = 0; i < NUM_MIDI_PERC; i++) sprintf(cPercName[i], "Note #%d", i);

	char line[256];

	FILE* p = fopen(PATCH_NAME_FILE, "r");
  if (!p) {
		fprintf(stderr, "Warning: Unable to open file listing patch names ("
			PATCH_NAME_FILE ")\nInstrument names will not be available.\n");
	} else {
		while (fgets(line, sizeof(line)-1, p)) {
			int iValue, iLen;
			char *p = strpbrk(line, "\n\r");
			if (p) *p = '\0'; // terminate the string at the newline
			if (sscanf(line, "%d=%n", &iValue, &iLen) == 1) {
				assert(iValue <= NUM_MIDI_PATCHES);
				snprintf(cPatchName[iValue-1], INSTR_NAMELEN, "%s [%d]", &line[iLen], iValue);
			} else if ((line[0] != '#') && (line[0] != '\n')) {
				fprintf(stderr, "Invalid line in " PATCH_NAME_FILE ": %s\n", line);
			}
		}
		fclose(p);
	}

	p = fopen(PERC_NAME_FILE, "r");
  if (!p) {
		fprintf(stderr, "Warning: Unable to open file listing percussion note "
			"names (" PERC_NAME_FILE ")\nPercussion names will not be available.\n");
	} else {
		while (fgets(line, sizeof(line)-1, p)) {
			int iValue, iLen;
			char *p = strpbrk(line, "\n\r");
			if (p) *p = '\0'; // terminate the string at the newline
			if (sscanf(line, "%d=%n", &iValue, &iLen) == 1) {
				assert(iValue <= NUM_MIDI_PERC);
				snprintf(cPercName[iValue], INSTR_NAMELEN, "%s [%d]", &line[iLen], iValue);
			} else if ((line[0] != '#') && (line[0] != '\n')) {
				fprintf(stderr, "Invalid line in " PERC_NAME_FILE ": %s\n", line);
			}
		}
		fclose(p);
	}

	FILE* f = fopen(MAPPING_FILE, "r");
  if (!f) {
		fprintf(stderr, "Warning: Unable to open instrument mapping file "
			MAPPING_FILE ", defaulting to a Grand Piano\nfor all instruments.\n");
		return true;
	}
	INSTRUMENT in;
  memset(&in, 0, sizeof(in));
	in.redirect = -1; // none of these should redirect (but later automatic
		// instruments will redirect to these ones)

	// Loop until we run out of lines in the data file or we hit the maximum
	// number of instruments
	char value[256];
	int iLineNum = 0;
	for (::instrcnt = 0; fgets(line, sizeof(line)-1, f) && (instrcnt < MAXINSTR);) {
		iLineNum++;

		// Ignore blank lines and comments
		if ((line[0] == '#') || (line[0] == '\r') || (line[0] == '\n')) continue;

		// Figure out what type of rhythm mode instrument (if any) the first two
		// chars are referring to.
		char cInstType[3];
		int iNumFields = sscanf(line, "%2s ", cInstType);
		if ((cInstType[0] == 'N') && (cInstType[1] == 'O')) in.eRhythmInstrument = NormalInstrument;
		else if ((cInstType[0] == 'B') && (cInstType[1] == 'D')) in.eRhythmInstrument = BassDrum;
		else if ((cInstType[0] == 'S') && (cInstType[1] == 'D')) in.eRhythmInstrument = SnareDrum;
		else if ((cInstType[0] == 'T') && (cInstType[1] == 'T')) in.eRhythmInstrument = TomTom;
		else if ((cInstType[0] == 'T') && (cInstType[1] == 'C')) in.eRhythmInstrument = TopCymbal;
		else if ((cInstType[0] == 'H') && (cInstType[1] == 'H')) in.eRhythmInstrument = HiHat;
		else {
			fprintf(stderr, "Invalid instrument type \"%s\" on line %d:\n\n  %s\n",
				cInstType, iLineNum, line);
			return false;
		}

		switch (in.eRhythmInstrument) {
			case NormalInstrument:
			case BassDrum:
				// Normal instrument or rhythm Bass Drum, read both
				// operators + connection byte
				iNumFields = sscanf(&line[3], "%02X-%02X/%02X-%02X/%02X-%02X/%02X-%02X"
					"/%02X/%02X-%02X: %255c\n",
					&in.reg20[0], &in.reg20[1],
					&in.reg40[0], &in.reg40[1],
					&in.reg60[0], &in.reg60[1],
					&in.reg80[0], &in.reg80[1],
					&in.regC0,
					&in.regE0[0], &in.regE0[1], value);
				if (iNumFields != 12) {
					fprintf(stderr, "Unable to parse line %d: (expected 12 instrument "
						"fields, got %d)\n\n%s\n", iLineNum, iNumFields, line);
					return false;
				}
				break;

			case TomTom:
			case HiHat:
				// This instrument is one operator only, but it does use the connection
				// byte (probably)
				iNumFields = sscanf(&line[3], "%02X/%02X/%02X/%02X/%02X/%02X: %255c\n",
					&in.reg20[0],
					&in.reg40[0],
					&in.reg60[0],
					&in.reg80[0],
					&in.regC0,
					&in.regE0[0], value);
				if (iNumFields != 7) {
					fprintf(stderr, "Unable to parse line %d: (expected 7 instrument "
						"fields, got %d)\n\n%s\n", iLineNum, iNumFields, line);
					return false;
				}
				break;

			case SnareDrum:
			case TopCymbal:
				// This instrument does not uses the connection byte, so read in one byte
				// less.  Also read the values into the other operator.
				iNumFields = sscanf(&line[3], "%02X/%02X/%02X/%02X/%02X: %255c\n",
					&in.reg20[1],
					&in.reg40[1],
					&in.reg60[1],
					&in.reg80[1],
					&in.regE0[1], value);
				if (iNumFields != 6) {
					fprintf(stderr, "Unable to parse line %d: (expected 6 instrument "
						"fields, got %d)\n\n%s\n", iLineNum, iNumFields, line);
					return false;
				}
				break;
		}

		// Default options
		in.isdrum = 0;
		in.prog = 1;
		in.iTranspose = 0;
		in.muted = false;

		// If we got this far it's a valid instrument
		int iValue;
		char nextopt[256];
		// We need to manually terminate the %255c in the sscanf() above, so do
		// this by terminating it at the end-of-line char.  Hopefully reading past
		// the end of the input string (which causes this) won't crash things...
		for (int i = 0; i < 256; i++) {
			if (value[i] == '\n') value[i] = 0;
			else if (value[i] == '\0') break;
		}
		value[255] = 0;

		const char *cList = value;
		int iLen;
		while (sscanf(cList, "%s%n", nextopt, &iLen) >= 1) {
			cList += iLen;
			if (nextopt[0] == '#') break; // reached an end of line comment
			if (nextopt[0] == ' ') continue; // skip double spaces
			if (sscanf(nextopt, "patch=%d", &iValue) == 1) {
				// MIDI patch
				in.isdrum = 0;
				in.prog = iValue - 1;
				if ((in.prog < 0) || (in.prog > 127)) {
					fprintf(stderr, "ERROR: Instrument #%d (line %d) was set to "
						"patch=%d, but this value must be between 1 and 128 inclusive.\n",
						instrcnt, iLineNum, in.prog + 1);
					return false;
				}
			} else if (sscanf(nextopt, "drum=%d", &iValue) == 1) {
				// MIDI drum
				in.isdrum = 1;
				in.prog = -1;
				in.note = iValue;
				if ((in.note < 0) || (in.note > 127)) {
					fprintf(stderr, "ERROR: Drum instrument #%d (line %d) was set to "
						"drum=%d, but this value must be between 1 and 128 inclusive.\n",
						instrcnt, iLineNum, in.note);
					return false;
				}
			} else if (sscanf(nextopt, "transpose=%d", &iValue) == 1) {
				// MIDI drum
				in.iTranspose = iValue;
			} else if (strcmp(nextopt, "mute") == 0) {
				// Mute this instrument
				in.muted = true;
			} else {
				fprintf(stderr, "Unknown instrument option on line %d: %s\n",
					iLineNum, nextopt);
				return false;
			}
		}

		char cInstTypeText[256];
		switch (in.eRhythmInstrument) {
			case NormalInstrument: cInstTypeText[0] = '\0'; break;
			case BassDrum: strcpy(cInstTypeText, "(OPL BD) "); break;
			case TomTom: strcpy(cInstTypeText, "(OPL TT) "); break;
			case HiHat: strcpy(cInstTypeText, "(OPL HH) "); break;
			case SnareDrum: strcpy(cInstTypeText, "(OPL SD) "); break;
			case TopCymbal: strcpy(cInstTypeText, "(OPL TC) "); break;
		}
		sprintf(in.name, "Inst#%03d %s@ line %3d%s: %s", instrcnt,
			cInstTypeText,
			iLineNum,
			(in.isdrum) ? " (perc)" : "",
			(in.isdrum) ? cPercName[in.note] : cPatchName[in.prog]
		);
		if (in.iTranspose) {
			char cTranspose[256];
			sprintf(cTranspose, " @ %+d semitones", in.iTranspose);
			strcat(in.name, cTranspose);
		}
		if (in.muted) strcat(in.name, " {muted}");
		//printf("%s\n", in.name);

		// Add instrument
		::instr[instrcnt++] = in;
  }
  fclose(f);
  return true;
}

long difference(int a, int b, int importance = 1)
{
  long diff = a - b;
  if (diff < 0) diff = -diff;
  return diff * importance;
}

long compareinstr(INSTRUMENT& a, INSTRUMENT& b, RHYTHM_INSTRUMENT ri)
{
	// Note that we're not using b.eRhythmInstrument below as "b" refers to the
	// OPL channel, and this never has an instrument type set.  The instrument
	// type we want is passed in as "ri", so we compare against that instead.
	//fprintf(stderr, "%d ", ri);
	switch (ri) {
		case NormalInstrument:
		case BassDrum:
			// Compare the full register set (both operators plus the connection
			// byte) for normal instruments (which occupy a whole channel) and the
			// one rhythm mode instrument which also occupies a whole channel.
			return
				difference(a.eRhythmInstrument, ri, 4) +
				difference(a.reg20[0], b.reg20[0], 2) +
				difference(a.reg20[1], b.reg20[1], 2) +
				difference(a.reg40[0], b.reg40[0], 1) +
				difference(a.reg40[1], b.reg40[1], 1) +
				difference(a.reg60[0], b.reg60[0], 2) +
				difference(a.reg60[1], b.reg60[1], 2) +
				difference(a.reg80[0], b.reg80[0], 2) +
				difference(a.reg80[1], b.reg80[1], 2) +
				difference(a.regC0, b.regC0, 3) +
				difference(a.regE0[0], b.regE0[0], 1) +
				difference(a.regE0[1], b.regE0[1], 1);
		case TomTom:
		case HiHat:
			// These two rhythm instruments only use one operator's settings, the
			// settings of the other operator (should) be ignored.  There is also
			// only one Connection byte, but there is no documentation to say whether
			// this applies to these modulator-only rhythm instruments or to the
			// other carrier-only ones (below.)  I'm guessing and putting it here.
			return
				difference(a.eRhythmInstrument, ri, 4) +
				difference(a.reg20[0], b.reg20[0], 2) +
				difference(a.reg40[0], b.reg40[0], 1) +
				difference(a.reg60[0], b.reg60[0], 2) +
				difference(a.reg80[0], b.reg80[0], 2) +
				difference(a.regC0, b.regC0, 3) +
				difference(a.regE0[0], b.regE0[0], 1);
		case SnareDrum:
		case TopCymbal:
			// These instruments only use one operator - but the other one compared
			// to the previous set above.  They also don't use the single Connection
			// byte (I think.)
			return
				difference(a.eRhythmInstrument, ri, 4) +
				difference(a.reg20[1], b.reg20[1], 2) +
				difference(a.reg40[1], b.reg40[1], 1) +
				difference(a.reg60[1], b.reg60[1], 2) +
				difference(a.reg80[1], b.reg80[1], 2) +
				difference(a.regE0[1], b.regE0[1], 1);
	}
}

void writesbi(const char* filename, int instrno, int chanOPL) {
	char fname[100];
	char title[32];
	snprintf(fname, 100, "%s_%03d.sbi", filename, instrno);
	FILE* f_sbi = fopen(fname, WRITE_BINARY);
	if (!f_sbi) {
		fprintf(stderr, "Could not open instrument file %s for writing.\n", filename);
	} else {
		fwrite("SBI\x1a", sizeof(char), 4, f_sbi);
		memset(title, 0, 32);
		snprintf(title, 32, "dro2midi_%03d", instrno);
		fwrite(title, sizeof(char), 32, f_sbi);
		unsigned char instr[16];
		memset(instr, 0, 16);
		instr[0] = (unsigned char)reg[chanOPL].reg20[0];
		instr[1] = (unsigned char)reg[chanOPL].reg20[1];
		instr[2] = (unsigned char)reg[chanOPL].reg40[0];
		instr[3] = (unsigned char)reg[chanOPL].reg40[1];
		instr[4] = (unsigned char)reg[chanOPL].reg60[0];
		instr[5] = (unsigned char)reg[chanOPL].reg60[1];
		instr[6] = (unsigned char)reg[chanOPL].reg80[0];
		instr[7] = (unsigned char)reg[chanOPL].reg80[1];
		instr[8] = (unsigned char)reg[chanOPL].regE0[0];
		instr[9] = (unsigned char)reg[chanOPL].regE0[1];
		instr[10] = (unsigned char)reg[chanOPL].regC0;
		fwrite(instr, sizeof(char), 16, f_sbi);
		fclose(f_sbi);
	}
}

int findinstr(int chanMIDI)
{
	assert((chanMIDI < 9) || ((chanMIDI >= CHAN_BASSDRUM) && (chanMIDI <= CHAN_HIHAT)));

	RHYTHM_INSTRUMENT ri;
	int chanOPL;
	switch (chanMIDI) {
		case CHAN_BASSDRUM:  ri = BassDrum;  chanOPL = 6; break;
		case CHAN_SNAREDRUM: ri = SnareDrum; chanOPL = 7; break;
		case CHAN_TOMTOM:    ri = TomTom;    chanOPL = 8; break;
		case CHAN_TOPCYMBAL: ri = TopCymbal; chanOPL = 8; break;
		case CHAN_HIHAT:     ri = HiHat;     chanOPL = 7; break;
		default: ri = NormalInstrument; chanOPL = chanMIDI; break;
	}

	int besti = -1;
	long bestdiff = -1;
	for (int i = 0; i < instrcnt; i++) {
		long diff = compareinstr(instr[i], reg[chanOPL], ri);
		if (besti < 0 || diff < bestdiff) {
			bestdiff = diff;
			besti = i;
			if (bestdiff == 0) break;
		}
	}

	if (besti >= 0) { // could be -1 if no instruments are loaded
		while (instr[besti].redirect >= 0) { // Could have multiple redirects
			// This instrument was an automatically generated one to avoid printing
			// the instrument definition multiple times, so instead of using the auto
			// one, use the one it originally matched against.
			besti = instr[besti].redirect;
		}
	}

	if (bestdiff != 0) {
		if (::bPerfectMatchesOnly) {
			// User doesn't want "close enough is good enough" instrument guessing
			besti = 0;  // use first instrument
		}
		// Couldn't find an exact match, print the details
		switch (ri) {
			case NormalInstrument:
			case BassDrum:
				// Normal instrument or rhythm Bass Drum use both
				// operators + connection byte
				printf("** New instrument in use on channel %d\n** Copy this into "
					MAPPING_FILE " to assign it a MIDI patch:\n", chanOPL);
				fprintf(stderr, "%s %02X-%02X/%02X-%02X/%02X-%02X/%02X-%02X/%02X/"
					"%02X-%02X: patch=?\n",
					((ri == BassDrum) ? "BD" : "NO"),
					reg[chanOPL].reg20[0], reg[chanOPL].reg20[1],
					reg[chanOPL].reg40[0], reg[chanOPL].reg40[1],
					reg[chanOPL].reg60[0], reg[chanOPL].reg60[1],
					reg[chanOPL].reg80[0], reg[chanOPL].reg80[1],
					reg[chanOPL].regC0,
					reg[chanOPL].regE0[0], reg[chanOPL].regE0[1]
				);
				if (::bWriteSbiInstruments) {
					writesbi(output, instrcnt, chanOPL);
				}
				break;
			case TomTom:
			case HiHat:
				// This instrument is one operator only, but it does use the connection
				// byte (probably)
				printf("** New rhythm instrument in use on OPL channel %d modulator\n"
					"** Copy this into " MAPPING_FILE " to assign it a MIDI patch:\n",
					chanOPL);
				fprintf(stderr, "%s %02X/%02X/%02X/%02X/%02X/%02X: "
					"patch=?\n",
					((ri == TomTom) ? "TT" : "HH"),
					reg[chanOPL].reg20[0],
					reg[chanOPL].reg40[0],
					reg[chanOPL].reg60[0],
					reg[chanOPL].reg80[0],
					reg[chanOPL].regC0,
					reg[chanOPL].regE0[0]
				);
				break;
			case SnareDrum:
			case TopCymbal:
				// This instrument does not uses the connection byte, so read in one
				// byte less.  Also read the values into the other operator.
				// This instrument is one operator only, but it does use the connection
				// byte (probably)
				printf("** New rhythm instrument in use on OPL channel %d carrier\n"
					"** Copy this into " MAPPING_FILE " to assign it a MIDI patch:\n",
					chanOPL);
				fprintf(stderr, "%s %02X/%02X/%02X/%02X/%02X: "
					"patch=?\n",
					((ri == SnareDrum) ? "SD" : "TC"),
					reg[chanOPL].reg20[1],
					reg[chanOPL].reg40[1],
					reg[chanOPL].reg60[1],
					reg[chanOPL].reg80[1],
					reg[chanOPL].regE0[1]
				);
				break;
		}

		printf(">> Using similar match: %s\n", instr[besti].name);
		// Save this unknown instrument as a known one, so the same registers don't get printed again
//		reg[channel].prog = instr[besti].prog;  // but keep the same patch that we've already assigned to the instrument, so it doesn't drop back to a piano for the rest of the song
		// Maybe ^ isn't necessary if we're redirecting?
		instr[instrcnt] = reg[chanOPL];
		instr[instrcnt].eRhythmInstrument = ri;
		if (besti >= 0) {
			instr[instrcnt].redirect = besti;  // Next time this instrument is matched, use the original one instead
		} else {
			instr[instrcnt].redirect = -1;  // Will only happen when no instruments are loaded
		}
		instrcnt++;
	}
	return besti;
}

// Function for processing OPL note on and off events, and generating MIDI
// events in response.  This function is also called when the pitch changes
// while a note is currently being played, causing it to generate MIDI
// pitchbends (if enabled) instead.
// Normally chanOPL and chanMIDI will be the same, except for rhythm mode
// instruments, which uses chanOPL for pitch and instrument patches, but
// chanMIDI for instrument mapping and note on/off events (since there will be
// two instrument maps and notes for a single OPL channel.)
void doNoteOnOff(bool bKeyOn, int chanOPL, int chanMIDI)
{
	double keyFrac = freq2key(curfreq[chanOPL], reg[chanOPL].iOctave);
	int key = (int)round(keyFrac);
	if ((key > 0) && (bKeyOn)) {
		// This is set to true to forcibly stop a MIDI keyon being generated for
		// this note.  This is done when a pitchbend is deemed as having done the
		// job properly.
		bool bKeyonAgain = true;

		if (keyAlreadyOn[chanMIDI]) {
			// There's already a note playing on this channel, just worry about the pitch of that

			if (mapchannel[chanMIDI] != gm_drumchannel) {
				// We're using a normal instrument here

				if (::bUsePitchBends) {
						// It's the same note, but the pitch is off just slightly, use a pitchbend
						//double dbDiff = fabs(keyFrac - key); // should be between -0.9999 and 0.9999
						double dbDiff = keyFrac - (double)(lastkey[chanMIDI] - transpose[chanMIDI]); // hopefully between -PITCHBEND_RANGE and PITCHBEND_RANGE

						if (dbDiff > PITCHBEND_RANGE) {
							fprintf(stderr, "Warning: This song wanted to pitchbend by %.2f notes, but the maximum is %.1f\n", dbDiff, PITCHBEND_RANGE);

							// Turn this note off
							write->noteoff(mapchannel[chanMIDI], lastkey[chanMIDI]);
							::iNotesActive--;
							lastkey[chanMIDI] = -1;
							keyAlreadyOn[chanMIDI] = false;
							// leave bKeyonAgain as true, so that a noteon will be played instead
						} else {
							int iNewBend = (int)(pitchbend_center + (PITCHBEND_ONESEMITONE * dbDiff));
							if (iNewBend != pitchbent[chanMIDI]) {
								//printf("pitchbend to %d/%.2lf (center + %d) (%.2lf "
								//	"semitones)\n", iNewBend, (double)pitchbend_center*2,
								//	(int)(iNewBend - pitchbend_center), (double)dbDiff);
								write->pitchbend(mapchannel[chanMIDI], iNewBend);
//								::iPitchbendCount++;
								pitchbent[chanMIDI] = iNewBend;
							}
							// This pitchbend has done the job, don't play a noteon
							bKeyonAgain = false;
						}
				} else {
					// We're not using pitchbends, so just switch off the note if it's different (the next one will play below)
					if ((::bApproximatePitchbends) && (key != (lastkey[chanMIDI] - transpose[chanMIDI]))) {
						write->noteoff(mapchannel[chanMIDI], lastkey[chanMIDI]);
						::iNotesActive--;
						lastkey[chanMIDI] = -1;
						keyAlreadyOn[chanMIDI] = false;
						//bKeyonAgain = true;
					} else {
						// Same note, different pitch, just pretend like it's not there
						bKeyonAgain = false;
					}
				}
			} else {
				// This has mapped to a percussive MIDI note, so no pitchbends (or
				// we'll bend all the percussive notes on the MIDI percussion channel.)
				// But we don't want to play the note again, 'cos it's already on, so
				// just ignore the keyon event.
				// We'll also end up here if the instrument parameters are changed
				// while the instrument is sounding, e.g. to change the characteristics
				// of a hihat without sounding a new note.  This won't be converted.
				bKeyonAgain = false;
			}
		} // else this is a percussive instrument

		//} else {
		//if ((!bDontKeyonAgain) && ((!keyAlreadyOn[channel]) || (::bUsePitchBends))) {  // If *now* there's no note playing... (or we're using pitchbends, i.e. a portamento has been set up)
		if (bKeyonAgain) {  // If *now* there's no note playing... (or we're using pitchbends, i.e. a portamento has been set up)
			// See if we need to update anything

			// See if the instrument needs to change
			int i = findinstr(chanMIDI);
			if (
				(i >= 0) && (
					(instr[i].prog != lastprog[chanMIDI]) ||
					(
						(instr[i].isdrum) &&
						(drumnote[chanMIDI] != instr[i].note)
					) || (
						// Same instrument mapping, but different mute setting?
						(instr[i].muted != mute[chanMIDI])
					)
				)
			) {
				printf("// Ch%02d <- %s\n", chanMIDI, instr[i].name);
				if (!instr[i].isdrum) {
					// Normal instrument (not MIDI percussion)
					assert(instr[i].prog >= 0);

					if (mapchannel[chanMIDI] == gm_drumchannel) {
						// This was playing drums, now we're back to normal notes

						// make sure this sets things back to what they were in the init
						// section in main()
						mapchannel[chanMIDI] = chanMIDI;
						drumnote[chanMIDI] = -1; // NOTE: This drumnote won't be reset if the drum instrument was muted!  (As it then wouldn't have been assigned to gm_drumchannel)
					}

					transpose[chanMIDI] = instr[i].iTranspose;
					write->program(mapchannel[chanMIDI], lastprog[chanMIDI] = instr[i].prog);
				} else {
					// This new instrument is a drum
					assert(instr[i].prog == -1);

					/*if (instr[i].muted) {
						// This instrument is muted, which means whichever channel we
						// assign it to will become muted.  We can't therefore assign it to
						// the drum channel as we normally would, otherwise all the MIDI
						// percussion will become muted.  So we assign it to its normal
						// channel instead, like we would with a non-percussion instrument.
						mapchannel[chanMIDI] = chanMIDI;
					} else {
						mapchannel[chanMIDI] = gm_drumchannel;
					}*/
					mapchannel[chanMIDI] = gm_drumchannel;
					drumnote[chanMIDI] = instr[i].note;
					lastprog[chanMIDI] = instr[i].prog;
					// drums don't use transpose values
				}
				mute[chanMIDI] = instr[i].muted;
			}

			// Play the note
			//if ((::bUsePitchBends) && (!keyAlreadyOn[channel])) {
			if ((::bUsePitchBends) && (mapchannel[chanMIDI] != gm_drumchannel)) { // If pitchbends are enabled and this isn't a percussion instrument
				double dbDiff = keyFrac - key; // should be between -0.9999 and 0.9999
				assert(dbDiff < PITCHBEND_RANGE); // not really necessary...

				int iNewBend = (int)(pitchbend_center + (PITCHBEND_ONESEMITONE * dbDiff));
				if (iNewBend != pitchbent[chanMIDI]) {
					//printf("new note at pitchbend %d\n", iNewBend);
					write->pitchbend(mapchannel[chanMIDI], iNewBend); // pitchbends are between 0x0000L and 0x2000L
//					::iPitchbendCount++;
					pitchbent[chanMIDI] = iNewBend;
				}
			}

			int level;
			if (!mute[chanMIDI]) {
				if (::bEnableVolume) {
					level = reg[chanOPL].reg40[1] & 0x3f;
					if (level > 0x30) level = 0x30; // don't allow fully silent notes
				} else level = 0; // 0 == loudest
			} else {
				level = 0x3f; // silent
			}

			if (mapchannel[chanMIDI] != gm_drumchannel) {
				// Normal note
				lastkey[chanMIDI] = key + transpose[chanMIDI];
			} else {
				// Percussion
				//write->noteon(gm_drumchannel, drumnote[chanMIDI], (0x3f - level) << 1);
				lastkey[chanMIDI] = drumnote[chanMIDI];
			}

			write->noteon(mapchannel[chanMIDI], lastkey[chanMIDI], (0x3f - level) << 1);
			//printf("note on chan %d, mute is %s\n", chanMIDI, mute[chanMIDI] ? "true" : "false");
			::iNotesActive++;
			::iTotalNotes++;

			// If this note went on with a pitchbend active on the channel, count it
			if (pitchbent[chanMIDI] != pitchbend_center) ::iPitchbendCount++;

			keyAlreadyOn[chanMIDI] = true;

		} // if (not muted)

	} else {
		// There's no note currently playing on this channel, so if we've still got
		// one switch it off.
		if (lastkey[chanMIDI] != -1) {
			write->noteoff(mapchannel[chanMIDI], lastkey[chanMIDI]);
			::iNotesActive--;
			lastkey[chanMIDI] = -1;
			keyAlreadyOn[chanMIDI] = false;
		}
	}

	return;
}

int main(int argc, char**argv)
{
	int c;

	// Defaults
	::dbConversionVal = 49716.0;

  argc--; argv++;
  while (argc > 0 && **argv == '-')
  {
		if (strncasecmp(*argv, "-r", 2) == 0) {
			::bRhythm = false;
			printf("Rhythm-mode instruments disabled.\n");
		} else if (strncasecmp(*argv, "-p", 2) == 0) {
			::bUsePitchBends = false;
			printf("Pitchbends disabled.\n");
		} else if (strncasecmp(*argv, "-a", 2) == 0) {
			::bApproximatePitchbends = true;
		} else if (strncasecmp(*argv, "-i", 2) == 0) {
			::bPerfectMatchesOnly = true;
			printf("Only using exact instrument matches - approximations disabled!\n");
		} else if (strncasecmp(*argv, "-v", 2) == 0) {
			::bEnableVolume = false;
			printf("Note velocity disabled, all notes will be played as loud as possible.\n");
		} else if (strncasecmp(*argv, "-s", 2) == 0) {
			::bWriteSbiInstruments = true;
		} else if (strncasecmp(*argv, "-c", 2) == 0) {
			argc--; argv++;
			if (argc == 0) {
				fprintf(stderr, "-c requires a parameter\n");
		    usage();
			}
			if (strncasecmp(*argv, "alt", 3) == 0) {
				::dbConversionVal = 50000.0;
			} else {
				// Use the given value
				::dbConversionVal = strtod(*argv, NULL);
				if (::dbConversionVal == 0) {
					fprintf(stderr, "-c requires a non-zero parameter\n");
			    usage();
				}
			}
		} else if (strncasecmp(*argv, "--version", 9) == 0) {
			version();
			return 0;
		} else {
			fprintf(stderr, "invalid option %s\n", *argv);
	    usage();
		}
    argc--; argv++;
  }
  if (argc < 2) usage();

	if ((::bUsePitchBends) && (::bApproximatePitchbends)) {
		fprintf(stderr, "ERROR: Pitchbends can only be approximated (-a) if "
			"proper MIDI pitchbends are disabled (-p)\n");
		return 1;
	}

  input = argv[0];
  output = argv[1];
  if (strcmp(input, output) == 0)
  {
    fprintf(stderr, "cannot convert to same file\n");
    return 1;
  }

	if (!loadInstruments()) return 1;


  f = fopen(input, READ_BINARY);
  if (!f)
  {
    perror(input);
    return 1;
  }
  unsigned long imflen = 0;

	unsigned char cSig[9];
	fseek(f, 0, SEEK_SET);
  //fgets(cSig, 9, f);
  fread(cSig, 1, 9, f);
	iSpeed = 0;
	if (strncmp((char *)cSig, "DBRAWOPL", 8) == 0) {
		::iFormat = FORMAT_DRO;
		fseek(f, 8, SEEK_SET);  // Seek to "version" fields.
		unsigned long version = readUINT32LE(f);
		if (version == 0x10000) {
			printf("Input file is in DOSBox DRO v1.0 format.\n");
		} else if (version == 0x2) {
			printf("Input file is in DOSBox DRO v2.0 format, which is not supported.\n");
			return 2;
		} else {
			printf("Input file is in DOSBox DRO format, but an unknown version!\n");
			return 3;
		}
		::iInitialSpeed = 1000;

		fseek(f, 16, SEEK_SET); // seek to "length in bytes" field
	  imflen = readUINT32LE(f);
	} else if (strncmp((char *)cSig, "RAWADATA", 8) == 0) {
		::iFormat = FORMAT_RAW;
		printf("Input file is in Rdos RAW format.\n");

		// Read until EOF (0xFFFF is really the end but we'll check that during conversion)
		fseek(f, 0, SEEK_END);
	  imflen = ftell(f);

		fseek(f, 8, SEEK_SET); // seek to "initial clock speed" field
		::iInitialSpeed = 1000;
		int iClockSpeed = readUINT16LE(f);
		if ((iClockSpeed == 0) || (iClockSpeed == 0xFFFF)) {
			::iSpeed = (int)18.2; // default to 18.2Hz...well, 18Hz thanks to rounding
		} else {
			::iSpeed = (int)(1193180.0 / iClockSpeed);
		}
	} else {
		::iFormat = FORMAT_IMF;
		if ((cSig[0] == 0) && (cSig[1] == 0)) {
			printf("Input file appears to be in IMF type-0 format.\n");
			fseek(f, 0, SEEK_END);
		  imflen = ftell(f);
			fseek(f, 0, SEEK_SET);
		} else {
			printf("Input file appears to be in IMF type-1 format.\n");
		  imflen = cSig[0] + (cSig[1] << 8);
			fseek(f, 2, SEEK_SET); // seek to start of actual OPL data
		}
		if (strcasecmp(&input[strlen(input)-3], "imf") == 0) {
			printf("File extension is .imf - using 560Hz speed (rename to .wlf if "
				"this is too slow)\n");
			::iInitialSpeed = 560;
		} else if (strcasecmp(&input[strlen(input)-3], "wlf") == 0) {
			printf("File extension is .wlf - using 700Hz speed (rename to .imf if "
				"this is too fast)\n");
			::iInitialSpeed = 700;
		} else {
			printf("Unknown file extension - must be .imf or .wlf\n");
			return 3;
		}
	}
	printf("Using conversion constant of %.1lf\n", ::dbConversionVal);

  write = new MidiWrite(output);
  if (!write) {
    fprintf(stderr, "out of memory\n");
    return 1;
  }
  if (!write->getf()) {
    perror(output);
    return 1;
  }
	if (iSpeed == 0) {
		iSpeed = iInitialSpeed;
	}
	resolution = iInitialSpeed / 2;
  write->head(/* version */ 0, /* track count updated later */0, resolution);

  write->track();
  write->tempo((long)(60000000.0 / tempo));
  write->tact(4,4,24,8);

  for (c = 0; c < 10; c++) {
    mapchannel[c] = c;
    write->resetctrlrs(mapchannel[c], 0);  // Reset All Controllers (Ensures default settings upon every playback).
    write->volume(mapchannel[c], 127);
    write->balance(mapchannel[c], 64);  // Usually called 'Pan'.
    write->expression(mapchannel[c], 127);  // Similar to 'Volume', but this is primarily used for volume damping.
  }

  for (c = 0; c <= 8; c++) {
    lastprog[c] = -1;
		reg[c].iOctave = 0;
  }

  int delay = 0;
  int channel;
  int code, param;

  for (c = 0; c < 9; c++) {
    curfreq[c] = 0;
    mapchannel[c] = c;  // This can get reset when playing a drum and then a normal instrument on a channel - see instrument-change code below
		keyAlreadyOn[c] = false;
		lastkey[c] = -1; // last MIDI key pressed on this channel
		pitchbent[c] = (int)pitchbend_center;
		transpose[c] = 0;
		drumnote[c] = 0; // probably not necessary...
		mute[c] = false;

		if (::bUsePitchBends) {
			write->control(mapchannel[c], 100, 0);  // RPN LSB for "Pitch Bend Sensitivity"
			write->control(mapchannel[c], 101, 0);  // RPN MSB for "Pitch Bend Sensitivity"
			write->control(mapchannel[c], 6, (int)PITCHBEND_RANGE); // Data for Pitch Bend Sensitivity (in semitones) - controller 38 can be used for cents in addition
			write->control(mapchannel[c], 100, 0x7F);  // RPN LSB for "Finished"
			write->control(mapchannel[c], 101, 0x7F);  // RPN MSB for "Finished"
		}
//		write->pitchbend(mapchannel[c], pitchbend_center);
  }
  // Rhythm-mode only channels
  for (c = 10; c < 15; c++) {
		keyAlreadyOn[c] = false;
    mapchannel[c] = c; // as above, this will be changed later but must be
			// eventually set back to this value here (or the instrument will jump
			// channels unexpectedly.)
		pitchbent[c] = (int)pitchbend_center;
		lastkey[c] = -1; // last MIDI key pressed on this channel
		transpose[c] = 0;
		drumnote[c] = 0; // probably not necessary...
		mute[c] = false;
  }

	int iMinLen; // Minimum length for valid notes to still be present
	switch (::iFormat) {
		case FORMAT_IMF: iMinLen = 4; break;
		case FORMAT_DRO: iMinLen = 2; break;
		case FORMAT_RAW: iMinLen = 2; break;
	}

	unsigned long iSize = imflen; // sometimes the counter wraps around, need this to stop it from happening
  while ((imflen >= (unsigned long)iMinLen) && (imflen <= iSize)) {

  	// Get the next OPL register and value from the input file

		switch (::iFormat) {
			case FORMAT_IMF:
				// Write the last iteration's delay (since the delay needs to come *after* the note)
		    write->time(delay);

		    code = readByte(f);
		    param = readByte(f);
				delay = readUINT16LE(f);
		    imflen -= 4;
				break;
			case FORMAT_DRO:
		    code = readByte(f);
				imflen--;
				switch (code) {
					case 0x00: // delay (byte)
						delay += 1 + readByte(f);
						imflen--;
						continue;
					case 0x01: // delay (int)
						delay += 1 + readUINT16LE(f);
						imflen -= 2;
						continue;
					case 0x02: // use first OPL chip
					case 0x03: // use second OPL chip
						fprintf(stderr, "Warning: This song uses multiple OPL chips - this isn't yet supported!\n");
						continue;
					case 0x04: // escape
						code = readByte(f);
						imflen--;
						break;
				}
		    param = readByte(f);
				imflen--;

				// Write any delay (as this needs to come *before* the next note)
		    write->time(delay);
				delay = 0;
				break;
			case FORMAT_RAW:
		    param = readByte(f);
		    code = readByte(f);
				imflen -= 2;
				switch (code) {
					case 0x00: // delay
						delay += param;
						continue;
					case 0x02: // control data
						switch (param) {
							case 0x00: {
								if (delay != 0) {
									// See below - we need to write out any delay at the old clock speed before we change it
							    write->time((delay * iInitialSpeed / ::iSpeed));
									delay = 0;
								}
								int iClockSpeed = readUINT16LE(f);
								if ((iClockSpeed == 0) || (iClockSpeed == 0xFFFF)) {
									printf("Speed set to invalid value, ignoring speed change.\n");
								} else {
									::iSpeed = (int)round(1193180.0 / iClockSpeed);
									printf("Speed changed to %dHz\n", iSpeed);
								}
								imflen -= 2;
								break;
							}
							case 0x01:
							case 0x02:
								printf("Switching OPL ports is not yet implemented!\n");
								break;
						}
						continue;
					case 0xFF:
						if (param == 0xFF) {
							// End of song
							imflen = 0;
							continue;
						}
						break;
				}

				// Write any delay (as this needs to come *before* the next note)
				// Since our global clock speed is 1000Hz, we have to multiply this
				// delay accordingly as the delay units are in the current clock speed.
				// This calculation converts them into 1000Hz delay units regardless of
				// the current clock speed.
		    if (delay != 0) write->time((delay * iInitialSpeed / ::iSpeed));
				//printf("delay is %d (ticks %d)\n", (delay * iInitialSpeed / ::iSpeed), delay);
				delay = 0;
				break;

			default: // should never happen
				break;

		} // switch (::iFormat)

		// Convert the OPL register and value into a MIDI event

    if (code >= 0xa0 && code <= 0xa8) { // set freq bits 0-7
      channel = code-0xa0;
      curfreq[channel] = (curfreq[channel] & 0xF00) + (param & 0xff);
			if (keyAlreadyOn[channel]) {
				param = 0x20; // bare noteon for code below
				doNoteOnOff(true, channel, channel);
			}
      continue;
    } else if (code >= 0xB0 && code <= 0xB8) { // set freq bits 8-9 and octave and on/off
      channel = code - 0xb0;
      curfreq[channel] = (curfreq[channel] & 0x0FF) + ((param & 0x03)<<8);
      // save octave so we know what it is if we run 0xA0-0xA8 regs change code
      // next (which doesn't have the octave)
			reg[channel].iOctave = (param >> 2) & 7;
      int keyon = (param >> 5) & 1;
			doNoteOnOff(keyon, channel, channel);
		} else if ((code == 0xBD) && (::bRhythm)) {
			if ((param >> 5) & 1) {
				// Bass Drum
				doNoteOnOff((param >> 4) & 1, channel, CHAN_BASSDRUM);
				doNoteOnOff((param >> 3) & 1, channel, CHAN_SNAREDRUM);
				doNoteOnOff((param >> 2) & 1, channel, CHAN_TOMTOM);
				doNoteOnOff((param >> 1) & 1, channel, CHAN_TOPCYMBAL);
				doNoteOnOff( param       & 1, channel, CHAN_HIHAT);
			}
		} else if (code >= 0x20 && code <= 0x35) {
      channel = GET_CHANNEL(code-0x20);
      reg[channel].reg20[GET_OP(code-0x20)] = param;
    } else if (code >= 0x40 && code <= 0x55) {
      channel = GET_CHANNEL(code-0x40);
      reg[channel].reg40[GET_OP(code-0x40)] = param;
    } else if (code >= 0x60 && code <= 0x75) {
      channel = GET_CHANNEL(code-0x60);
      reg[channel].reg60[GET_OP(code-0x60)] = param;
    } else if (code >= 0x80 && code <= 0x95) {
      channel = GET_CHANNEL(code-0x80);
      reg[channel].reg80[GET_OP(code-0x80)] = param;
    } else if (code >= 0xc0 && code <= 0xc8) {
      channel = code-0xc0;
      reg[channel].regC0 = param;
    } else if (code >= 0xe0 && code <= 0xF5) {
      channel = GET_CHANNEL(code-0xe0);
      reg[channel].regE0[GET_OP(code-0xe0)] = param;
    }
  }

  for (c = 0; c < 10; c++) {
       mapchannel[c] = c;
       write->allnotesoff(mapchannel[c], 0);  // All Notes Off (Ensures that even incomplete Notes will be switched-off per each MIDI channel at the end-of-playback).
  }

  delete write;
  fclose(f);

  // Display completion message and some stats
	printf("\nConversion complete.  Wrote %s\n\n  Total pitchbent notes: %d\n"
		"  Total notes: %d\n  Notes still active at end of song: %d\n\n",
		output, ::iPitchbendCount, ::iTotalNotes, ::iNotesActive);

  return 0;
}
