// DualExtrude.cpp : Generate a "both extruders on" gcode file from a single extruder file
// Copyright (c) 2012, Joe Cabana    Joey@JSConsulting.com

/*
Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/


/* 1.0
  The basic idea here is to just duplicate the speed/temp/on/off commands
  sent to one extruder to both extruders. The result should be two prints
  for the time of one!
*/

/* 1.1
  Of course the basic idea didn't quite make it. Both extruders came on, but the
  second extruder speed wasn't correctly controlled. I added code derived from
  "Dual Extrude Both Extruders at Once for Replicator" from user thorstadg
  on thingiverse.com.  That did it!

  That means that Dimension has to be enabled in skeinforge for this to work
  since his code basically duplicated the extruder distance 'E' added by
  Dimension so it worked for both extruders.
*/

/* 1.2
  Minor bug fix in error messages and changes to eliminate deprecated conversion warnings
*/

/* 2.0
  Added ability to adjust for differing filament diameters
*/

/* 2.1
  Fixed a bug that generated an error on retracts early in the file.
*/

/* 2.2
  Fixed a bug that showed the input file name when an error occured
  creating the output file.
*/

// Include standard libs
#include <stdio.h>
#include <string.h>
#include <math.h>

// Local function prototypes
int ConvFile(char *infile, char *outfile);
int CheckFile(char *infile);
int CheckCode(char *buf);

// Local defines
#define NUMCODES 7  // Number of g/m codes we care about
#define NOTOKENS -1  // Code for no tokens found
#define ERROR_BOTH "ERROR: File already uses both extruders.\n\n"
#define M101	0	// Extruder on fwd
#define M102	1	// Extruder on rev
#define M103	2	// Extruder off
#define M104	3	// Set temp
#define M108	4	// Set extruder max speed
#define M6		5	// Tool change
#define G1		6	// Coordinated Motion

// Command list  Should be same order as above defines
const char *CODES[NUMCODES] = { "M101", "M102", "M103", "M104", "M108", "M6", "G1" };

// Parse tokens
char Tokens[] = " \012";

// Extruder used flags
int LeftUsed, RightUsed;  // Toolhead used indicators
double FirstE;  // First 'E' position from source file
double Ratio;  // Ratio of filament areas


// Main() function
// Input:  Command line args
// Output: Success/Failure code
int main(int argc, char* argv[])
{
	double D1, D2;  // Filament diameters
	int InfileArg, OutFileArg;

	// Clear varibles
	LeftUsed = 0;
	RightUsed = 0;
	FirstE = 0;
	Ratio = 1.0;

	printf("DualExtrude version 2.2\n\n");

	// Check args
	switch (argc) {
	case 3:  // Just file names
		InfileArg = 1;  //  Set file name args
		OutFileArg = 2;
		break;
	case 5:  // Also using diameters
		InfileArg = 1;  // Set file name args
		OutFileArg = 3;

		// Get diameter ratio
		sscanf(argv[2],"%lf",&D1);  // Get first diameter
		if (D1 < 1.5 || D1 > 2.2) {
			printf("ERROR: Filament diameter: %s too big/small!\n\n",argv[2]);
			return (0);
		}

		sscanf(argv[4],"%lf",&D2);  // Get second diameter
		if (D2 < 1.5 || D2 > 2.2) {
			printf("ERROR: Filament diameter: %s too big/small!\n\n",argv[4]);
			return (0);
		}

		// Calculate the ratio of the squares of the radius'
		Ratio = ((D1 / 2) * (D1 / 2)) / ((D2 / 2) * (D2 / 2));
		break;
	default:   // Show usage
		printf("Usage:  DualExtrude infile [DiaIn] outfile [DiaNew]\n\n");
		printf("          infile - Input single extruder gcode file\n");
		printf("          DiaIn - Diameter of filament used to generate the input file.\n");
		printf("          outfile - Output both extruder gcode file\n");
		printf("          DiaNew - Diameter of filament used on the second extruder.\n\n");
		printf("    NOTE: If you are using different diameter filaments,\n");
		printf("          BOTH DiaIn and DiaNew must be given!\n\n");
		return (0);
		break;
	}

	// Check/parse input file
	printf("Checking file...\n");
	if (!CheckFile(argv[InfileArg]))
		return (-1);

	if (LeftUsed)
		printf("File uses left extruder, adding right...\n");
	else
		printf("File uses right extruder, adding left...\n");

	if (argc == 5)
		printf("Input file diameter: %s   Added extruder diameter: %s\n",argv[2],argv[4]);

	// Generate new file
	if (!ConvFile(argv[InfileArg],argv[OutFileArg]))
		return (-1);

	return (0);
}

// ConvFile() Function
//   Reads single extruder input file
//   and converts it two a "both on" file.
//
// Inputs: infile - File to convert
//         outfile - Name for converted file
//
// Outputs: Sucess/Failure
//
int ConvFile(char *infile, char *outfile)
{
	FILE *in;  // Input file
	FILE *out;  // Output file
	int cnt = 0; // Line counter
	char buf[1002];  // File buffer
	char bufP[1002];  // Parse buffer
	char *Token;  // Pointer for next token
	const char *NotUsed;  // Toolhead code for the one
					// that's not being used in the input file
					// Used to drop lines we don't need
	int Code;  // ID of the M code used in the current linw
	int Temp;  // Arg from a set temp command
	char Speed[16]; // Speed setting from speed command
	double CurrentE;  // Current 'E' value
	double NewE;  // 'E' for second extruder

	// Set not used toolhead
	if (RightUsed)
		NotUsed = "T1";
	else
		NotUsed = "T0";

	// Open input file
	if ((in = fopen(infile,"r")) == NULL)
	{
		printf("ERROR: Can't open input file: %s\n\n",infile);
		return (0);
	}

	// Open output file
	if ((out = fopen(outfile,"wb")) == NULL)
	{
		fclose(in);
		printf("ERROR: Can't create output file: %s\n\n",outfile);
		return (0);
	}

	// Loop thru file
	while (NULL != fgets(buf,1000,in))
	{
		++cnt;  // Increment line counter
		strcpy(bufP,buf);  // Copy buffer for parsing

		// Look for a command that we will need to deal with
		switch (Code = CheckCode(strtok(bufP,Tokens)))
		{
		case M101:  // On/Off commands
		case M102:
		case M103:
		case M6:  // Tool change
			// Check for parameters
			if (NULL != (Token = strtok(NULL,Tokens)))
			{  // Has a second token, should be a "T0" or "T1"
				if (!strcmp(NotUsed,Token)) // Check for used extruder
					break;  // Command for unused extruder, drop it
			}

			// Needed command, duplicate for both extruders
			sprintf(buf,"%s T1\012%s T0\012",CODES[Code],CODES[Code]);
			break;
		case M104:  // Temp command
			Temp = 0;  // Zero temp

			// Check for parameters
			while (NULL != (Token = strtok(NULL,Tokens)))
			{ // Got one!
				if (!strcmp(NotUsed,Token)) // Check for unused extruder
					break;  // Command for the "not used" extruder, drop it.

				if ('S' == Token[0])  // Check for temp value
					sscanf(Token,"S%d",&Temp);  // Get temp
			}

			// Needed temp command, output duplicates
			sprintf(buf,"%s S%d T1\012%s S%d T0\012",CODES[Code],Temp,CODES[Code],Temp);
			break;
		case M108:  // Speed command
			Speed[0] = 0;  // Clear speed

			// Check for parameters
			while (NULL != (Token = strtok(NULL,Tokens)))
			{ // Got one!
				if (!strcmp(NotUsed,Token)) // Check for unused extruder
					break;  // Command for the "not used" extruder, drop it.

				if ('R' == Token[0])  // Check for speed value
				{
					if (strlen(Token) > 15)  // Check for speed command that fits in buffer
					{
						fclose(in);
						fclose(out);
						printf("ERROR: Speed command too long in line %d\n\n",cnt);
						return (0);
					}
					strcpy(Speed,Token);  // Get speed
				}
			}

			if (!strlen(Speed))  // Check for a speed value
			{
				fclose(in);
				fclose(out);
				printf("ERROR: No speed in command in line %d\n\n",cnt);
				return (0);
			}

			// Needed speed command, output duplicates
			sprintf(buf,"%s %s T1\012%s %s T0\012",CODES[Code],Speed,CODES[Code],Speed);
			break;
		case G1:  // Coordinated Motion
			sprintf(buf,"%s",CODES[G1]);  // Start command

			// Check for parameters
			while (NULL != (Token = strtok(NULL,Tokens)))
			{ // Got one!
				// The following is derived from "Dual Extrude Both Extruders at Once for Replicator"
				// from user thorstadg on thingiverse.com
				if ('E' == Token[0] || 'A' == Token[0] || 'B' == Token[0])  // Check for 'E', 'A' or 'B'
				{
					if (strlen(Token) > 15)  // Check for parameter that fits in buffer
					{
						fclose(in);
						fclose(out);
						printf("ERROR: E Parameter too long in line %d\n\n",cnt);
						return (0);
					}

					// Get current 'E' value
					CurrentE = 0.0;
					sscanf(Token+1,"%lf",&CurrentE);

/* Commented out to allow for retract on first/early moves
					if (CurrentE < FirstE)  // Check for errors
					{
						fclose(in);
						fclose(out);
						printf("ERROR: E Parameter direction error in line %d\n\n",cnt);
						return (0);
					}
*/

					if (FirstE > 0) // Did we see an 'E' before?
					{  // Yes, figure new value for second extruder
						NewE = ((CurrentE - FirstE) * Ratio) + FirstE;

						// Round to the nearest .001
						NewE = floor((NewE * 100000.0) + 0.5);
						NewE = NewE / 100000.0;

						if (RightUsed)  // Check witch one is the new one
						{
							// Replace with A/B
							sprintf(buf+strlen(buf)," B%.5f A%s", NewE, Token+1);
						}
						else
						{
							// Replace with A/B
							sprintf(buf+strlen(buf)," A%.5f B%s", NewE, Token+1);
						}
					}
					else
					{  // No, just output what we got, and save the first 'E'
						// Replace with A/B
						sprintf(buf+strlen(buf)," A%s B%s",Token+1, Token+1);

						FirstE = CurrentE; // Save first one
					}
				}
				else  // Just output
					sprintf(buf+strlen(buf)," %s",Token);
			}
			sprintf(buf+strlen(buf),"\012");
			break;
		}

		// Output new/old line
		fprintf(out,"%s",buf);
	}

	// Close flles
	fclose(in);
	fclose(out);

	printf("%d Lines processed\n",cnt);

	return (1);
}

// CheckFile() Function
//   Reads single extruder input file
//   and checks it to make sure one and
//   only one extruder is used.
//
//   Sets RightUsed & Left Used globals
//   as needed.
//
// Inputs: infile - File to convert
//
// Outputs: Sucess/Failure
//
int CheckFile(char *infile)
{
	FILE *in;  // Input file
	int cnt = 0; // Line counter
	char buf[1024];  // File buffer
	char *Token;  // Pointer for next token
	int UsedLeft, UsedRight, Temp;

	// Open file
	if ((in = fopen(infile,"r")) == NULL)
	{
		printf("ERROR: Can't open input file: %s\n\n",infile);
		return (0);
	}

	// Loop thru file
	while (NULL != fgets(buf,sizeof(buf),in))
	{
		++cnt;  // Increment line counter

		// Look for a command that will tell us which toolhead is active
		switch (CheckCode(strtok(buf,Tokens)))
		{
		case M101:  // On commands
		case M102:
			// Check for parameters
			if (NULL != (Token = strtok(NULL,Tokens)))
			{
				if (!strcmp("T0",Token)) // Check for Right extruder
				{
					if (LeftUsed) {
						printf(ERROR_BOTH);
						fclose(in);
						return (0);
					}
					RightUsed = 1;
					break;
				}
				if (!strcmp("T1",Token)) // Check for Left extruder
				{
					if (RightUsed) {
						printf(ERROR_BOTH);
						fclose(in);
						return (0);
					}
					LeftUsed = 1;
					break;
				}
			}
			break;
		case M104:  // Temp command
			UsedLeft = 0;
			UsedRight = 0;
			Temp = 0;
			// Check for parameters
			while (NULL != (Token = strtok(NULL,Tokens)))
			{
				if (!strcmp("T0",Token)) // Check for Right extruder
					UsedRight = 1;

				if (!strcmp("T1",Token)) // Check for Left extruder
					UsedLeft = 1;

				if ('S' == Token[0])  // Check for temp value
					sscanf(Token,"S%d",&Temp);
			}

			// Check for right extruder active
			if (UsedRight && Temp > 0)
			{
				if (LeftUsed) {
					printf(ERROR_BOTH);
					fclose(in);
					return (0);
				}
				RightUsed = 1;
				break;
			}

			// Check for left extruder active
			if (UsedLeft && Temp > 0)
			{
				if (RightUsed) {
					printf(ERROR_BOTH);
					fclose(in);
					return (0);
				}
				LeftUsed = 1;
				break;
			}
			break;
		}
	}

	// Close flle
	fclose(in);

	// Check to see if we found one
	if (!RightUsed && !LeftUsed)
	{
		printf("ERROR: Couldn't find a used extruder!\n\n");
		return (0);
	}

	printf("%d Lines checked...\n",cnt);

	return (1);
}

// CheckCode() Function
//   Checks a character string to see if it
//   matches one of the known G/M codes.
//
// Inputs: buf - Input string
//
// Outputs: Code ID, or NOTOKENS if none found
//
int CheckCode(char *buf)
{
	int cnt;

	if (NULL == buf) // Check for no mcodes
		return (-1);

	for (cnt = 0; cnt < NUMCODES; ++cnt)
	{
		// Check for G/M Code
		if (!strcmp(buf,CODES[cnt]))
			return (cnt);
	}

	return (NOTOKENS);
}
