// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// DMD Font Tool
//
// This reads a font file in the .dmd format used by PyProcGame,
// an unrelated open-source pinball project you can find at
// pyprocgame.pindev.org.  A few suitable font files in this
// format can be found at pinballmakers.com.
//
// The .dmd format is actually designed for DMD animation cells;
// the font usage is just a special case.  In the general case,
// the byte format of the file is as follows (see the "DMD File
// Format Specifiation" at pyprocgame.pindev.org).  All integer
// fields are stored in little-endian (Intel) format.
//
//   bytes 0..3         - header, unused (typically contains "DMD\0")
//   bytes 4..7         - frame count, INT32
//   bytes 8..11        - width of each frame, in pixels
//   bytes 12..15       - height of each frame, in pixels
//   width*height bytes - frame #0 pixel array
//   width*height bytes - frame #1 pixel array
//   ...
//
// The frame pixel arrays are packed in the obvious way (top row
// first, pixels packed left to right across the row with no padding,
// then each subsequent row top to bottom, no padding between rows).  
// Each pixel is represented by one byte, which is divided into two
// 4-bit fields.  The high 4 bits give the alpha (0 is completely
// transparent, 0xf is completely opaque, and values in between are
// linear N/15 alpha).  The low 4 bits give the brightness on a
// 16-shade gray scale.  (Needless to say, this format was designed
// for traditinoal monochrome pinball DMDs.)
//
// When used to represent a DMD font, the same wrapper format is
// used, and the file contains exactly two frames.  The first frame
// contains the character forms in a 10x10 grid.  The top left cell
// contains ASCII 32 (space), and each cell left to right contains
// the next ASCII character.  Characters from ASCII 32 to 126 are
// represented.  This leaves five empty cells in the last row (for
// code points 127..131).  Note the the alpha value is ignored for
// a font file.
//
// The second frame in a font file doesn't contain pixel data.
// Instead, the first 95 bytes contain the pixel widths of the
// characters at code points 32..126.  The remaining bytes of the
// second frame aren't used.  The per-character pixel widths allow
// for proportional spacing.  When drawing, only the leftmost N
// pixels of a character's cell are used, where N is the width
// given in the second frame data.
//

#include "stdafx.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <memory>


static void errexit(const char *msg, ...)
{
	// display a generic error prefix
	printf_s("DMDFontTool: error: ");

	// display the message
	va_list ap;
	va_start(ap, msg);
	vprintf_s(msg, ap);
	va_end(ap);

	// add a newline
	printf_s("\n");

	// exit with an error code
	exit(2);
}


// use 4-byte alignment for structures, to match the file format
#pragma pack(4)

// specific-size types
typedef unsigned __int8 BYTE;
typedef __int32 INT32;
typedef unsigned __int32 UINT32;

int main(int argc, char **argv)
{
	// check usage
	if (argc != 4)
		errexit("usage: DMDFontTool <input>.dmd <fontName> <output>.h");
	
	// get the arguments
	const char *infile = argv[1];
	const char *fontName = argv[2];
	const char *outfile = argv[3];

	// open files
	FILE *infp, *outfp;
	int err;
	if ((err = fopen_s(&infp, infile, "rb")) != 0)
		errexit("unable to open input file %s (error %d)", infile, err);
	if ((err = fopen_s(&outfp, outfile, "w")) != 0)
		errexit("unable to open output file %s (error %d)", outfile, err);

	// read the fixed header
	struct
	{
		UINT32 signature;
		UINT32 nFrames;
		UINT32 cx;
		UINT32 cy;
	} header;
	if (fread_s(&header, sizeof(header), sizeof(header), 1, infp) != 1)
		errexit("error reading file header");

	// read the first frame
	size_t frameBytes = header.cx * header.cy;
	std::unique_ptr<BYTE> frame(new BYTE[frameBytes]);
	if (fread_s(frame.get(), frameBytes, frameBytes, 1, infp) != 1)
		errexit("error reading character grid frame (frame 0)");

	// Read the character widths.  These are given by the first 95 bytes
	// of the second frame; the rest of the frame is meaningless.
	BYTE widths[95];
	if (fread_s(widths, sizeof(widths), sizeof(widths), 1, infp) != 1)
		errexit("error reading character widths (frame 1)");

	// Figure the source grid's fixed cell height.  The source grid is
	// always 10x10 character cells, with fixed-size cells, so the single
	// cell width/height is simply 1/10 of the overall grid width/height.
	int cellWidth = header.cx / 10;
	int cellHeight = header.cy / 10;

	// Add up the character widths to get the total row width in
	// our rearranged 1x95 varying-width representation used in the
	// generated file.
	int rowWidth = 0;
	for (int i = 0; i < 95; ++i)
		rowWidth += widths[i];

	// Generate the file header
	fprintf_s(outfp, "// This file was generated from %s - do not edit\n\n", infile);

	// Generate the pixel array.  We rearrange this from the 10x10
	// character grid with fixed-size cells used in the .dmd format
	// to a 1x95 grid (that is, one row with all 95 characters) with
	// varying-width cells.  This format is more efficient
	fprintf_s(outfp, "static const BYTE %s_pix[] = {", fontName);
	int nOut = 0;
	int nOutTotal = cellHeight * rowWidth;
	for (int row = 0; row < cellHeight; ++row)
	{
		for (int ch = 0; ch < 95; ++ch)
		{
			// get the pixel position of the upper left of this character cell
			int x0 = (ch % 10) * cellWidth;
			int y0 = (ch / 10) * cellHeight;

			// get the pixel position of the leftmost pixel of the current row
			// in this character cell
			int x = x0;
			int y = y0 + row;

			// get the buffer pointer for this pixel
			BYTE *pix = frame.get() + (y * header.cx) + x;

			// write each pixel of this character row
			for (int col = 0; col < (int)widths[ch]; ++col, ++nOut, ++pix)
			{
				fprintf_s(outfp, "%s%2d%s",
					(nOut & 0x1f) == 0 ? "\n    " : " ",
					*pix,
					nOut + 1 < nOutTotal ? "," : "");
			}
		}
	}
	fprintf_s(outfp, "\n};\n");
	
	// Generate the character width array
	fprintf_s(outfp, "static const BYTE %s_charWidths[] = {", fontName);
	for (int i = 0; i < 95; ++i)
	{
		fprintf_s(outfp, "%s%2d%s",
			(i & 0x1f) == 0 ? "\n    " : " ",
			widths[i],
			i < 94 ? "," : "");
	}
	fprintf_s(outfp, "\n};\n");

	// Generate the character offset array
	fprintf_s(outfp, "static const int %s_charOffsets[] = {", fontName);
	for (int i = 0, charOfs = 0; i < 95; charOfs += widths[i], ++i)
	{
		fprintf_s(outfp, "%s%4d%s",
			(i & 0x0f) == 0 ? "\n    " : " ",
			charOfs,
			i < 94 ? "," : "");
	}
	fprintf_s(outfp, "\n};");

	// Generate the DMDFont object instantiation
	fprintf_s(outfp, "\n"
		"const DMDFont DMDFonts::%s(\n"
		"    %s_pix, %d, %d,\n"
		"    %s_charWidths, %s_charOffsets);\n\n",
		fontName, 
		fontName, rowWidth, cellHeight,
		fontName, fontName);

	// close files
	fclose(infp);
	fclose(outfp);

	// done
	return 0;
}
