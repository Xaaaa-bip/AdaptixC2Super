#pragma once
#include <windows.h>

typedef struct {
	char *original;
	char *buffer;
	int length;
	int size;
} datap;

DECLSPEC_IMPORT void BeaconDataParse(datap *parser, char *buffer, int size);
DECLSPEC_IMPORT int BeaconDataInt(datap *parser);

#define CALLBACK_OUTPUT 0x0
#define CALLBACK_ERROR  0x0d

DECLSPEC_IMPORT void BeaconPrintf(int type, char *fmt, ...);

typedef struct {
	char *original;
	char *buffer;
	int length;
	int size;
} formatp;

DECLSPEC_IMPORT void BeaconFormatAlloc(formatp *format, int maxsz);
DECLSPEC_IMPORT void BeaconFormatReset(formatp *format);
DECLSPEC_IMPORT void BeaconFormatFree(formatp *format);
DECLSPEC_IMPORT void BeaconFormatPrintf(formatp *format, char *fmt, ...);
DECLSPEC_IMPORT char *BeaconFormatToString(formatp *format, int *size);
