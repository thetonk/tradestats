#include "utilities.h"
#include "constants.h"
#include <linux/limits.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <limits.h>
#include <stdio.h>

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
    return 0; //something is wrong
}

size_t getFileLineCount(char *filename){
    FILE *file = fopen(filename, "r");
    if (file == NULL){
        return 0;
    }
    char ch = 0;
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
    size_t bytes = snprintf(filename,filenameLength,"out/candleSticks/%s_candles.csv",symbolName);
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
