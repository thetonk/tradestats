#ifndef UTILITIES_H_
#define UTILITIES_H_
#include <stdbool.h>
#include <stdlib.h>
#include "constants.h"

typedef struct {
	size_t symbolID;
	time_t timestamp;
	double price, volume;
} trade;

typedef struct {
	trade first,last,max,min;
	double totalVolume;
	size_t symbolID;
} Candle;

char** readSymbolsFile(char *filename, size_t linecount);
size_t getFileLineCount(char *filename);
bool writeCandleFile(); //TODO
bool writeMovingAverageFile(); //TODO
bool writeSymbolTradesFile(); //TODO
void quicksortStrings(char **strings, size_t len);
size_t searchString(char **strings, char *findStr,size_t len);
#endif // UTILITIES_H_
