//
// gen_test_midi.cpp - by malvineous@shikadi.net
//
// Generate a MIDI file that plays one note from all 128 GM instruments (in
// order), followed by all the percussion notes.
//
// When this file is played through an OPL synthesiser capable of MIDI
// playback, then captured through DOSBox, it will provide a complete set of
// OPL patches that can easily be associated back to MIDI instruments.
//

#include "midiio.hpp"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>

#define WRITE_BINARY  "wb"
#define READ_TEXT     "r"

int main(int argc, char**argv)
{
	if (argc != 2) {
		fprintf(stderr, "no output filename given\n");
		return 1;
	}

	MidiWrite *write = new MidiWrite(argv[1]);
	if (!write) {
		fprintf(stderr, "out of memory\n");
		return 1;
	}
	if (!write->getf()) {
		perror(argv[1]);
		return 1;
	}

	float tempo = 120.0;
	int resolution = 280;
	write->head(/* version */ 0, /* track count updated later */0, resolution);

	write->track();
	write->tempo((long)(60000000.0 / tempo));
	write->tact(4,4,24,8);

  write->volume(1, 127);
  write->volume(9, 127);
  for (int i = 0; i < 128; i++) {
  	write->program(1, i);
  	write->noteon(1, 60, 127);
    write->time(50);
  	write->noteoff(1, 60);
    write->time(50);
  }
/*
  // Need to play a normal note for percussion to get DOSBox to start capturing
  // OPL data!  (Why hasn't my patch to fix that been incorporated yet?!)
  write->noteon(1, 60, 127);
  write->time(50);
	write->noteoff(1, 60);
  write->time(200);
  for (int i = 35; i < 82; i++) {
  	write->noteon(9, i, 127);
    write->time(50);
  	write->noteoff(9, i);
    write->time(50);
  }*/

  delete write;

  return 0;
}
