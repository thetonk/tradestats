#include "utilities.h"
#include "constants.h"
#include "vector.h"
#include <linux/limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <limits.h>
#include <stdio.h>
#include <time.h>

void print_helptext(char* progname){
    printf("Usage: %s [-l] TOKEN SYMBOLFILE\n",progname);
	printf("-l: (Optional) Enable trade logs\n");
}

void init_trade(Trade* trade){
    trade->price = 0;
    trade->symbolID = 0;
    trade->timestamp = 0;
    trade->volume = 0;
    memset(&trade->insertionTime, 0, sizeof(struct timespec));
}

Candle* init_candle(size_t size){
    Candle* candle = malloc(size*sizeof(Candle));
    for(size_t i = 0; i < size; ++i){
        candle[i].totalVolume = INIT_VOLUME_VALUE;
        init_trade(&candle[i].first);
        init_trade(&candle[i].last);
        init_trade(&candle[i].max);
        init_trade(&candle[i].min);
        candle[i].symbolID = 0;
    }
    return candle;
}

void reset_candle(Candle *candle, Trade *last){
    candle->totalVolume = last->volume;
    candle->first = *last;
    candle->min = *last;
    candle->max = *last;
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
        ma[i].stopTime = 0;
    }
    return ma;
}

void reset_movAvg(MovingAverage *movAvg, Trade *last){
    movAvg->totalVolume = last->volume;
	movAvg->first = *last;
	movAvg->averagePrice = last->price;
	movAvg->tradeCount = 1;
    movAvg->stopTime = last->timestamp;
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
    return UINT32_MAX; //something is wrong,return an invalid value
}

char* replace_char(char* str, char find, char replace){
    char *current_pos = strchr(str,find);
    while (current_pos != NULL) {
        *current_pos = replace;
        current_pos = strchr(current_pos+1,find);
    }
    return str;
}

uint64_t difftimespec_us(const struct timespec *after, const struct timespec *before)
{
    return (uint64_t) ((int64_t)after->tv_sec - (int64_t)before->tv_sec) * (int64_t)1000000
         + ((int64_t)after->tv_nsec - (int64_t)before->tv_nsec) / 1000;
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
    char folderPath[PATH_MAX], symbol[SYMBOL_LENGTH];
    char *normSymbolName;
    memcpy(symbol, symbolName, SYMBOL_LENGTH);
    normSymbolName = replace_char(symbol, ':', '-');
    snprintf(folderPath,PATH_MAX, "%s/candleSticks",OUTPUT_DIRECTORY);
    const size_t filenameLength = strlen(folderPath)+SYMBOL_LENGTH+strlen("_candles.csv")+2; //extra 2 bytes needed, 1 for / and 1 for null char
    char filename[filenameLength];
    mkdir(folderPath, 0755);
    snprintf(filename,filenameLength,"%s/%s_candles.csv",folderPath,normSymbolName);
    struct stat stats;
    bool fileRequiresHeader = (stat(filename, &stats) != 0);
    FILE *fp = fopen(filename, "a");
    if (fp == NULL){
        printf(ERROR_LOG_COLOR"Cannot create file %s. Reason: %s\n"ANSI_RESET, filename, strerror(errno));
        return;
    }
    if(fileRequiresHeader){
        fputs("Symbol,First Timestamp,Last Timestamp,First Price,Last Price,Min Price,Max Price,Total Volume\n", fp);
    }
    fprintf(fp,"%s,%zu,%zu,%lf,%lf,%lf,%lf,%lf\n",symbolName,(size_t)candle->first.timestamp,(size_t)candle->last.timestamp,candle->first.price,candle->last.price,
            candle->min.price,candle->max.price,candle->totalVolume);
    fclose(fp);
}

void writeMovingAverageFile(char *symbolName, MovingAverage *movingAverage){
    //filename format will be SYMBOL_movingAverages
    //Write CSV file
    char folderPath[PATH_MAX], symbol[SYMBOL_LENGTH];
    char *normSymbolName;
    memcpy(symbol,symbolName,SYMBOL_LENGTH);
    normSymbolName = replace_char(symbol, ':', '-');
    snprintf(folderPath,PATH_MAX, "%s/movingAverages",OUTPUT_DIRECTORY);
    const size_t filenameLength = strlen(folderPath)+SYMBOL_LENGTH+strlen("_movingAverages.csv")+2; //extra 2 bytes needed, 1 for / and 1 for null char
    char filename[filenameLength];
    mkdir(folderPath, 0755);
    snprintf(filename,filenameLength,"%s/%s_movingAverages.csv",folderPath,normSymbolName);
    struct stat stats;
    bool fileRequiresHeader = (stat(filename, &stats) != 0);
    FILE *fp = fopen(filename, "a");
    if (fp == NULL){
        printf(ERROR_LOG_COLOR"Cannot create file %s. Reason: %s\n"ANSI_RESET, filename, strerror(errno));
        return;
    }
    if(fileRequiresHeader){
        fputs("Symbol,First Timestamp,Last Timestamp,Total Trades,Average Price,Total Volume\n", fp);
    }
    fprintf(fp,"%s,%zu,%zu,%zu,%lf,%lf\n",symbolName,(size_t)movingAverage->first.timestamp,(size_t)movingAverage->stopTime,movingAverage->tradeCount,movingAverage->averagePrice,
            movingAverage->totalVolume);
    fclose(fp);
}

void writeSymbolTradesFile(char *symbolName, Trade *trade, uint64_t delay){
    //filename format will be SYMBOL_trades
    //Write CSV file
    char folderPath[PATH_MAX], symbol[SYMBOL_LENGTH];
    char *normSymbolName;
    memcpy(symbol,symbolName,SYMBOL_LENGTH);
    normSymbolName = replace_char(symbol, ':', '-');
    snprintf(folderPath,PATH_MAX, "%s/tradeLogs",OUTPUT_DIRECTORY);
    const size_t filenameLength = strlen(folderPath)+SYMBOL_LENGTH+strlen("_trades.csv")+2; //extra 2 bytes needed, 1 for / and 1 for null char
    struct tm timedate = *localtime(&trade->timestamp);
    char filename[filenameLength];
    mkdir(folderPath, 0755);
    snprintf(filename,filenameLength,"%s/%s_trades.csv",folderPath,normSymbolName);
    struct stat stats;
    bool fileRequiresHeader = (stat(filename, &stats) != 0);
    FILE *fp = fopen(filename, "a");
    if (fp == NULL){
        printf(ERROR_LOG_COLOR"Cannot create file %s. Reason: %s\n"ANSI_RESET, filename, strerror(errno));
        return;
    }
    if(fileRequiresHeader){
        fputs("Symbol,Timestamp,Processing Time (us),Price,Volume\n", fp);
    }
    fprintf(fp,"%s,%02d:%02d:%02d,%ld,%lf,%lf\n",symbolName,timedate.tm_hour,timedate.tm_min,timedate.tm_sec,delay,trade->price,trade->volume);
    fclose(fp);
}
