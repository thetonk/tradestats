#ifndef UTILITIES_H_
#define UTILITIES_H_
#include <stdlib.h>
#include "constants.h"

typedef struct {
	size_t symbolID;
	time_t timestamp;
	double price, volume;
} Trade;

typedef struct {
	Trade first,last,max,min;
	double totalVolume;
	size_t symbolID;
} Candle;

typedef struct {
	Trade first;
	double totalVolume;
	double averagePrice;
	size_t symbolID,tradeCount;
} MovingAverage;

int tradeCompareByTime(const void *a,const void *b);
char** readSymbolsFile(char *filename, size_t linecount);
size_t getFileLineCount(char *filename);
void writeCandleFile(char *symbolName, Candle* candle);
void writeMovingAverageFile(char* symbolName, MovingAverage* movingAverage);
void writeSymbolTradesFile(char* symbolName, Trade* trade); //TODO
void quicksortStrings(char **strings, size_t len);
size_t searchString(char **strings, char *findStr,size_t len);
#endif // UTILITIES_H_
