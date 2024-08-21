#ifndef UTILITIES_H_
#define UTILITIES_H_
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include "constants.h"
#include "vector.h"

typedef struct {
	size_t symbolID;
	time_t timestamp;
	double price, volume;
	struct timespec insertionTime;
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
	time_t stopTime;
} MovingAverage;

void init_trade(Trade* trade);
Candle* init_candle(size_t size);
void reset_candle(Candle* candle, Trade* last);
void destroy_candle(Candle *candle);
MovingAverage *init_movAvg(size_t size);
void reset_movAvg(MovingAverage* movAvg,Trade* last);
void destroy_movAvg(MovingAverage* ma);
uint64_t difftimespec_us(const struct timespec *after, const struct timespec *before);
char** readSymbolsFile(char *filename, size_t linecount);
size_t getFileLineCount(char *filename);
void writeCandleFile(char *symbolName, Candle* candle);
void writeMovingAverageFile(char* symbolName, MovingAverage* movingAverage);
void writeSymbolTradesFile(char* symbolName, Trade* trade);
void writeDetentionTimesFile(char *threadName, Vector* data, size_t itemCount);
void quicksortStrings(char **strings, size_t len);
size_t searchString(char **strings, char *findStr,size_t len);
#endif // UTILITIES_H_
