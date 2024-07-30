#include "utilities.h"
#include "constants.h"
#include <linux/limits.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <limits.h>
#include <stdio.h>
#include <time.h>

void init_trade(Trade* trade){
    trade->price = 0;
    trade->symbolID = 0;
    trade->timestamp = 0;
    trade->volume = 0;
}

Candle* init_candle(size_t size){
    Candle* candle = malloc(size*sizeof(Candle));
    for(size_t i = 0; i < size; ++i){
        candle[i].totalVolume = INIT_VOLUME_VALUE;
        init_trade(&candle[i].first);
        init_trade(&candle[i].last);
        init_trade(&candle[i].max);
        init_trade(&candle[i].max);
        init_trade(&candle[i].min);
        candle[i].symbolID = 0;
    }
    return candle;
}

void destroy_candle(Candle *candle){
    free(candle);
}

MovingAverage* init_movAvg(size_t size){
    MovingAverage* ma = malloc(size*sizeof(MovingAverage));
    for(size_t i = 0; i< size; ++i){
        ma[i].totalVolume = INIT_VOLUME_VALUE;
        init_trade(&ma[i].first);
        ma[i].tradeCount = 0;
        ma[i].averagePrice = 0;
        ma[i].symbolID = 0;
    }
    return ma;
}

void destroy_movAvg(MovingAverage *ma){
    free(ma);
}

void swapStrings(char **str1, char **str2)
{
    char *tmp = *str1;
    *str1 = *str2;
    *str2 = tmp;
}

void quicksortStrings(char **strings, size_t len){
    unsigned int i, pvt=0;
    if (len <= 1)
        return;
    // swap a randomly selected value to the last node
    swapStrings(strings+((unsigned int)rand() % len), strings+len-1);
    // reset the pivot index to zero, then scan
    for (i=0;i<len-1;++i)
    {
        if (strcmp(strings[i], strings[len-1]) < 0)
            swapStrings(strings+i, strings+pvt++);
    }
    // move the pivot value into its place
    swapStrings(strings+pvt, strings+len-1);
    // and invoke on the subsequences. does NOT include the pivot-slot
    quicksortStrings(strings, pvt++);
    quicksortStrings(strings+pvt, len - pvt);
}

size_t searchString(char **strings, char *findStr, size_t len){
    size_t first = 0, last = len-1, middle;
    int value;
    while(first <= last){
        middle = first + (last-first)/2;
        value = strcmp(strings[middle], findStr);
        if(value == 0){
            //found it
            return middle;
        }
        else if (value > 0){
            //middle percedes
            last = middle - 1;
        }
        else{
            first = middle + 1;
        }
    }
    return UINT64_MAX; //something is wrong,return an invalid value
}

size_t getFileLineCount(char *filename){
    FILE *file = fopen(filename, "r");
    if (file == NULL){
        return 0;
    }
    int8_t ch = 0;
    size_t lines = 0;
    while((ch = fgetc(file)) != EOF){
        if(ch == '\n')
            lines++;
    }
    fclose(file);
    return lines;
}

char** readSymbolsFile(char *filename, size_t linecount){
    char **strings = (char**) malloc(linecount*sizeof(char*));
    FILE *file = fopen(filename,"r");
    if(file == NULL){
        return NULL;
    }
    for(size_t i = 0; i < linecount; ++i){
        strings[i] = (char*) malloc(SYMBOL_LENGTH*sizeof(char));
        fscanf(file, "%s", strings[i]);
    }
    fclose(file);
    return strings;
}

void writeCandleFile(char *symbolName, Candle *candle){
    //filename format will be SYMBOL_candles
    //Write CSV file
    char folderPath[PATH_MAX];
    snprintf(folderPath,PATH_MAX, "%s/candleSticks",OUTPUT_DIRECTORY);
    const size_t filenameLength = strlen(folderPath)+SYMBOL_LENGTH+strlen("_candles.csv");
    char filename[filenameLength];
    mkdir(folderPath, 0755);
    snprintf(filename,filenameLength,"%s/%s_candles.csv",folderPath,symbolName);
    struct stat stats;
    bool fileRequiresHeader = (stat(filename, &stats) != 0);
    FILE *fp = fopen(filename, "a");
    if (fp == NULL){
        printf("Cannot create file %s. Reason: %s\n", filename, strerror(errno));
        return;
    }
    if(fileRequiresHeader){
        fputs("Symbol,Timestamp,First,Last,Min,Max,Total Volume\n", fp);
    }
    fprintf(fp,"%s,%zu,%lf,%lf,%lf,%lf,%lf\n",symbolName,candle->first.timestamp,candle->first.price,candle->last.price,
            candle->min.price,candle->max.price,candle->totalVolume);
    fclose(fp);
}

void writeMovingAverageFile(char *symbolName, MovingAverage *movingAverage){
    //filename format will be SYMBOL_movingAverages
    //Write CSV file
    char folderPath[PATH_MAX];
    snprintf(folderPath,PATH_MAX, "%s/movingAverages",OUTPUT_DIRECTORY);
    const size_t filenameLength = strlen(folderPath)+SYMBOL_LENGTH+strlen("_movingAverages.csv");
    char filename[filenameLength];
    mkdir(folderPath, 0755);
    snprintf(filename,filenameLength,"%s/%s_movingAverages.csv",folderPath,symbolName);
    struct stat stats;
    bool fileRequiresHeader = (stat(filename, &stats) != 0);
    FILE *fp = fopen(filename, "a");
    if (fp == NULL){
        printf("Cannot create file %s. Reason: %s\n", filename, strerror(errno));
        return;
    }
    if(fileRequiresHeader){
        fputs("Symbol,Timestamp,Total Trades,Average Price,Total Volume\n", fp);
    }
    fprintf(fp,"%s,%zu,%zu,%lf,%lf\n",symbolName,movingAverage->first.timestamp,movingAverage->tradeCount,movingAverage->averagePrice,
            movingAverage->totalVolume);
    fclose(fp);
}

void writeSymbolTradesFile(char *symbolName, Trade *trade){
    //filename format will be SYMBOL_trades
    //Write CSV file
    char folderPath[PATH_MAX];
    snprintf(folderPath,PATH_MAX, "%s/tradeLogs",OUTPUT_DIRECTORY);
    const size_t filenameLength = strlen(folderPath)+SYMBOL_LENGTH+strlen("_trades.csv");
    struct tm timedate = *localtime(&trade->timestamp);
    char filename[filenameLength];
    mkdir(folderPath, 0755);
    snprintf(filename,filenameLength,"%s/%s_trades.csv",folderPath,symbolName);
    struct stat stats;
    bool fileRequiresHeader = (stat(filename, &stats) != 0);
    FILE *fp = fopen(filename, "a");
    if (fp == NULL){
        printf("Cannot create file %s. Reason: %s\n", filename, strerror(errno));
        return;
    }
    if(fileRequiresHeader){
        fputs("Symbol,Timestamp,Price,Volume\n", fp);
    }
    fprintf(fp,"%s,%02d:%02d:%02d,%lf,%lf\n",symbolName,timedate.tm_hour,timedate.tm_min,timedate.tm_sec,trade->price,trade->volume);
    fclose(fp);
}
