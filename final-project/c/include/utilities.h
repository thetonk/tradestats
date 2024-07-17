#ifndef UTILITIES_H_
#define UTILITIES_H_
#include <stdbool.h>
#include <stdlib.h>
#include "constants.h"

typedef struct {
	char symbol[SYMBOL_LENGTH];
	time_t timestamp;
	double price, volume;
} trade;

char** readSymbolsFile(char *filename, size_t linecount);
size_t getFileLineCount(char *filename);
bool writeCandleFile(); //TODO
bool writeMovingAverageFile(); //TODO
bool writeSymbolTradesFile(); //TODO
void quicksortStrings(char **strings, size_t len);
size_t searchString(char **strings, char *findStr,size_t len);
#endif // UTILITIES_H_
