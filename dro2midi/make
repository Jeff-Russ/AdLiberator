#!/bin/sh

# Usage:
#
#   ./make
#
# or if you have a cross compiler for i586-pc-mingw32 installed
#
#   ./make i586-pc-mingw32 .exe
#

if [ "$1" != "" ]; then
	PLATFORM="$1"-
	TARGET="dro2midi$2"
else
	PLATFORM=""
	TARGET="dro2midi"
fi

${PLATFORM}g++ -o ${TARGET} dro2midi.cpp midiio.cpp &&
	${PLATFORM}strip ${TARGET}
