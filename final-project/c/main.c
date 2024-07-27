#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <libwebsockets.h>
#include <pthread.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include "include/cJSON.h"
#include "include/utilities.h"
#include "include/vector.h"
#include "include/queue.h"
#include "include/constants.h"

static bool bExit = false;
static bool tradeLogging = false;
static size_t symbolCount;
static Candle *candles, *prev_candles;
static MovingAverage* movAverages, *prev_movAverages;
static bool bDenyDeflate = true;
static char** Symbols;
static pthread_t keyLogger;
static pthread_mutex_t tradeLoggerMutex, candleMutex, movAvgMutex;
static pthread_cond_t tradeLoggerCondition;
static int callback_test(struct lws* wsi, enum lws_callback_reasons reason, void *user, void* in, size_t len);
static Vector *candleConsVector, *movAvgConsVector, *tradeLogConsVector;

// Escape the loop when a SIGINT signal is received
static void onSigInt(int sig)
{
	lwsl_warn("Caught SIGINT! Exiting!\n");
	bExit = true;
	pthread_cancel(keyLogger); //stop keyLogger
	//wake up any threads sleeping
	pthread_cond_signal(candleConsVector->isEmpty);
	pthread_cond_signal(movAvgConsVector->isEmpty);
	pthread_cond_signal(tradeLogConsVector->isEmpty);
	pthread_cond_signal(&tradeLoggerCondition);
	tradeLogging = true;
}

// The registered protocols
static struct lws_protocols protocols[] = {
	{
		"test-protocol", // Protocol name
		callback_test,   // Protocol callback
		0,
		0
	},
	{ NULL, NULL, 0,0 } // Always needed at the end
};

// The extensions LWS supports, without them some requests may not be able to work
static const struct lws_extension extensions[] = {
	{
		"permessage-deflate",
		lws_extension_callback_pm_deflate,
		"permessage-deflate; client_max_window_bits"
	},
	{
		"deflate-frame",
		lws_extension_callback_pm_deflate,
		"deflate_frame"
	},
	{ NULL, NULL, NULL } // Always needed at the end
};

// List to identify the indices of the protocols by name
enum protocolList {
	PROTOCOL_TEST,
	PROTOCOL_LIST_COUNT // Needed
};

// Callback for the test protocol
static int callback_test(struct lws* wsi, enum lws_callback_reasons reason, void *user, void* in, size_t len)
{
	// For which reason was this callback called?
	switch (reason)
	{
		// The connection closed
	case LWS_CALLBACK_CLOSED:
		lwsl_info("[Test Protocol] Connection closed.\n");
		break;
	// Our client received something
	case LWS_CALLBACK_CLIENT_RECEIVE:
	{
		cJSON *jsonResponse = cJSON_Parse(in);
		if(jsonResponse == NULL){
			lwsl_err("Error parsing JSON response!\n");
			const char *error_ptr = cJSON_GetErrorPtr();
			if (error_ptr != NULL){
				lwsl_err("Error before: %s\n", error_ptr);
			}
			bExit = true;
			return -1;
		}
		//printf("========================================================================\n");
		//printf("[Test Protocol] Received data: \"%s\", length: %zu bytes\n", (char*)in,len);
		cJSON *data = cJSON_GetObjectItemCaseSensitive(jsonResponse, "data");
		size_t symbolID;
		if (data != NULL){
			cJSON *item;
			cJSON_ArrayForEach(item, data){
				cJSON *priceItem, *timeItem, *volumeItem, *symbolItem;
				Trade tradeItem;
				struct tm ctimestamp;
				timeItem = cJSON_GetObjectItem(item, "t");
				priceItem = cJSON_GetObjectItem(item, "p");
				volumeItem = cJSON_GetObjectItem(item, "v");
				symbolItem = cJSON_GetObjectItemCaseSensitive(item, "s");
				symbolID = searchString(Symbols, symbolItem->valuestring, symbolCount);
				tradeItem.symbolID = symbolID;
				tradeItem.price = priceItem->valuedouble;
				tradeItem.timestamp = (uint64_t) timeItem->valuedouble / 1000;
				ctimestamp = *localtime(&tradeItem.timestamp);
				tradeItem.volume = volumeItem->valuedouble;
				pthread_mutex_lock(candleConsVector->mutex);
				vector_push_back(candleConsVector, &tradeItem);
				pthread_cond_signal(candleConsVector->isEmpty);
				pthread_mutex_unlock(candleConsVector->mutex);
				pthread_mutex_lock(movAvgConsVector->mutex);
				vector_push_back(movAvgConsVector, &tradeItem);
				pthread_cond_signal(movAvgConsVector->isEmpty);
				pthread_mutex_unlock(movAvgConsVector->mutex);
				pthread_mutex_lock(tradeLogConsVector->mutex);
				vector_push_back(tradeLogConsVector, &tradeItem);
				pthread_cond_signal(tradeLogConsVector->isEmpty);
				pthread_mutex_unlock(tradeLogConsVector->mutex);
				//printf("Time: %02d:%02d:%02d, symbol: %s, price: %.2lf, volume: %lf\n",ctimestamp.tm_hour,ctimestamp.tm_min,ctimestamp.tm_sec,Symbols[symbolID],priceItem->valuedouble, volumeItem->valuedouble);
			}
		}
	}
		break;
	// Here the server tries to confirm if a certain extension is supported by the server
	case LWS_CALLBACK_CLIENT_CONFIRM_EXTENSION_SUPPORTED:
		if (strcmp((char*)in, "deflate-stream") == 0)
		{
			if (bDenyDeflate)
			{
				lwsl_warn("[Test Protocol] Denied deflate-stream extension\n");
				return 1;
			}
		}
		break;
	// The connection was successfully established
	case LWS_CALLBACK_CLIENT_ESTABLISHED:
		lwsl_info("[Test Protocol] Connection to server established.\n");
		char msg[SUBSCRIBE_MESSAGE_LENGTH];
		for(size_t i = 0; i < symbolCount;++i){
			memset(msg, 0, SUBSCRIBE_MESSAGE_LENGTH);
			snprintf(msg, SUBSCRIBE_MESSAGE_LENGTH,"{\"type\":\"subscribe\", \"symbol\":\"%s\"}",Symbols[i]);
			lwsl_info("[Test Protocol] Writing \"%s\" to server.\n", msg);
			lws_write(wsi, (unsigned char*)msg, strlen(msg), LWS_WRITE_TEXT);
		}
		break;

		// The server notifies us that we can write data
	case LWS_CALLBACK_CLIENT_WRITEABLE:
		lwsl_info("[Test Protocol] The client is able to write.\n");
		break;

		// There was an error connecting to the server
	case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
		lwsl_err("[Test Protocol] There was a connection error: %s\n", in ? (char*)in : "(no error information)");
		break;

	default:
		break;
	}

	return 0;
}

void *consumerCandle(void *q){
	Vector *vec = (Vector*) q;
	struct tm firstTime, lastTime,currentTime;
	Trade last;
	while(!bExit){
		pthread_mutex_lock(vec->mutex);
		while(vec->size == 0 && !bExit){
			pthread_cond_wait(vec->isEmpty,vec->mutex);
		}
		vector_pop(candleConsVector,(Trade *) &last);
		pthread_mutex_unlock(vec->mutex);
		candles[last.symbolID].last = last;
		lastTime = *localtime(&candles[last.symbolID].last.timestamp);
		firstTime = *localtime(&(candles[last.symbolID].first.timestamp));
		time_t currentTimestamp = time(NULL);
		currentTime = *localtime(&currentTimestamp);
		//printf("consumerCandle thread got price %.2lf of symbol %s, time: %02d:%02d:%02d, volume: %lf\n",last.price,Symbols[last.symbolID],lastTime.tm_hour,lastTime.tm_min,lastTime.tm_sec,last.volume);
		if ((int64_t) candles[last.symbolID].totalVolume == (int64_t) INIT_VOLUME_VALUE){
			candles[last.symbolID].totalVolume = last.volume;
			candles[last.symbolID].first = last;
			candles[last.symbolID].min = last;
			candles[last.symbolID].max = last;
		}
		else if(firstTime.tm_min == lastTime.tm_min -1 || (firstTime.tm_min == 59 && lastTime.tm_min == 0)){
			printf(CANDLE_LOG_COLOR"FINAL CANDLE for %s initialized! Start time: %02d:%02d:%02d -> %02d:%02d:%02d\n"ANSI_RESET, Symbols[last.symbolID],firstTime.tm_hour,
				       firstTime.tm_min,firstTime.tm_sec,lastTime.tm_hour,lastTime.tm_min,lastTime.tm_sec);
			pthread_mutex_lock(&candleMutex);
			prev_candles[last.symbolID] = candles[last.symbolID];
			candles[last.symbolID].totalVolume = last.volume;
			candles[last.symbolID].first = last;
			candles[last.symbolID].min = last;
			candles[last.symbolID].max = last;
			pthread_mutex_unlock(&candleMutex);
		}
		else{
			if(firstTime.tm_min > lastTime.tm_min && !(firstTime.tm_min == 59 && lastTime.tm_min == 0)){ //when an older trade appears out of order later
				pthread_mutex_lock(&candleMutex);
				prev_candles[last.symbolID].last = last;
				if((int64_t) prev_candles[last.symbolID].totalVolume == (int64_t) INIT_VOLUME_VALUE){ //rare occasion
					printf(CANDLE_LOG_COLOR"Retarded symbol initialization %s\n"ANSI_RESET,Symbols[last.symbolID]);
					prev_candles[last.symbolID].totalVolume = last.volume;
					prev_candles[last.symbolID].first = last;
					prev_candles[last.symbolID].min = last;
					prev_candles[last.symbolID].max = last;
				}
				else{
					//printf("An older trade arrived late of symbol %s!\n",Symbols[last.symbolID]);
					if(last.price < prev_candles[last.symbolID].min.price){
						prev_candles[last.symbolID].min = last;
					}
					if(last.price > prev_candles[last.symbolID].max.price){
						prev_candles[last.symbolID].max = last;
					}
					prev_candles[last.symbolID].totalVolume += last.volume;
				}
				pthread_mutex_unlock(&candleMutex);
			}
			else{
				if(last.price < candles[last.symbolID].min.price){
					candles[last.symbolID].min = last;
				}
				if(last.price > candles[last.symbolID].max.price){
					candles[last.symbolID].max = last;
				}
				candles[last.symbolID].totalVolume += last.volume;
			}
		}
		//printf("%s first %02d:%02d last minute %02d:%02d\n",Symbols[last.symbolID],firstTime.tm_min,firstTime.tm_sec, lastTime.tm_min,lastTime.tm_sec);
	}
	return NULL;
}

void* consumerMovingAverage(void* args){
	Vector* vec = (Vector*) args;
	Queue** totalTradesPerMinute = (Queue**) malloc(symbolCount*sizeof(Queue*));
	Queue** totalPricePerMinute = (Queue**) malloc(symbolCount*sizeof(Queue*));
	Queue** timestampsPerMinute = (Queue**) malloc(symbolCount*sizeof(Queue*));
	size_t* totalTrades = (size_t*) malloc(symbolCount*sizeof(size_t));
	double* totalPrices = (double*) malloc(symbolCount*sizeof(double));
	memset(totalPrices, 0, symbolCount*sizeof(double));
	memset(totalTrades,0,symbolCount*sizeof(totalTrades));
	for(size_t i = 0; i < symbolCount;++i){
		totalPricePerMinute[i] = queue_init(MOVING_AVERAGE_INTERVAL_MINUTES, sizeof(double));
		totalTradesPerMinute[i] = queue_init(MOVING_AVERAGE_INTERVAL_MINUTES, sizeof(size_t));
		timestampsPerMinute[i] = queue_init(MOVING_AVERAGE_INTERVAL_MINUTES, sizeof(time_t));
	}
	struct tm firstTime, lastTime;
	Trade last;
	while(!bExit){
		pthread_mutex_lock(vec->mutex);
		while(vec->size == 0 && !bExit){
			pthread_cond_wait(vec->isEmpty,vec->mutex);
		}
		vector_pop(movAvgConsVector,(Trade *) &last);
		pthread_mutex_unlock(vec->mutex);
		lastTime = *localtime(&last.timestamp);
		firstTime = *localtime(&movAverages[last.symbolID].first.timestamp);
		//printf("consumerMA thread got price %.2lf of symbol %s, time: %02d:%02d:%02d, volume: %lf\n",last.price,Symbols[last.symbolID],lastTime.tm_hour,lastTime.tm_min,lastTime.tm_sec,last.volume);
		if ((int64_t) movAverages[last.symbolID].totalVolume == (int64_t) INIT_VOLUME_VALUE){
			movAverages[last.symbolID].totalVolume = last.volume;
			movAverages[last.symbolID].first = last;
			movAverages[last.symbolID].averagePrice = last.price;
			movAverages[last.symbolID].tradeCount = 1;
		}
		else if(firstTime.tm_min == lastTime.tm_min -1 || (firstTime.tm_min == 59 && lastTime.tm_min == 0)){ //3 minutes passed
			totalPrices[last.symbolID] += movAverages[last.symbolID].averagePrice;
			totalTrades[last.symbolID] += movAverages[last.symbolID].tradeCount;
			if(totalPricePerMinute[last.symbolID]->isFull){
				size_t oldestTotalTrades;
				double oldestTotalPrice;
				//remove oldest value
				queue_pop(totalPricePerMinute[last.symbolID], &oldestTotalPrice);
				queue_pop(totalTradesPerMinute[last.symbolID], &oldestTotalTrades);
				//remove oldest values from MA calculation
				movAverages[last.symbolID].averagePrice -= oldestTotalPrice;
				movAverages[last.symbolID].tradeCount -= oldestTotalTrades;
			}
			queue_insert(totalTradesPerMinute[last.symbolID], &movAverages[last.symbolID].tradeCount);
			queue_insert(totalPricePerMinute[last.symbolID], &movAverages[last.symbolID].averagePrice);
			queue_insert(timestampsPerMinute[last.symbolID], &movAverages[last.symbolID].first.timestamp);
			pthread_mutex_lock(&movAvgMutex);
			if(totalPricePerMinute[last.symbolID]->isFull){
				queue_pop(timestampsPerMinute[last.symbolID],(time_t*) &movAverages[last.symbolID].first.timestamp);
				firstTime = *localtime(&movAverages[last.symbolID].first.timestamp);
				printf(MA_LOG_COLOR"%s %d FINAL MA triggered! Start time: %02d:%02d:%02d -> %02d:%02d:%02d\n"ANSI_RESET,Symbols[last.symbolID],MOVING_AVERAGE_INTERVAL_MINUTES,
                     firstTime.tm_hour,firstTime.tm_min,firstTime.tm_sec,lastTime.tm_hour,lastTime.tm_min,lastTime.tm_sec);
				movAverages[last.symbolID].tradeCount = totalTrades[last.symbolID];
				movAverages[last.symbolID].averagePrice = totalPrices[last.symbolID];
				prev_movAverages[last.symbolID] = movAverages[last.symbolID];
			}
			movAverages[last.symbolID].first = last;
			movAverages[last.symbolID].tradeCount = 1;
			movAverages[last.symbolID].totalVolume = last.volume;
			movAverages[last.symbolID].averagePrice = last.price;
			pthread_mutex_unlock(&movAvgMutex);
		}
		else{
			if(firstTime.tm_min > lastTime.tm_min && !(firstTime.tm_min == 59 && lastTime.tm_min == 0)){ //when an older trade appears out of order later
				pthread_mutex_lock(&movAvgMutex);
				if(prev_movAverages[last.symbolID].totalVolume == INIT_VOLUME_VALUE){ //rare occasion
					prev_movAverages[last.symbolID].first = last;
					prev_movAverages[last.symbolID].averagePrice = last.price;
					prev_movAverages[last.symbolID].totalVolume = last.volume;
					prev_movAverages[last.symbolID].tradeCount = 1;
				}
				else{
					prev_movAverages[last.symbolID].tradeCount++;
					prev_movAverages[last.symbolID].averagePrice += last.price;
					prev_movAverages[last.symbolID].totalVolume += last.volume;
				}
				pthread_mutex_unlock(&movAvgMutex);
			}
			else{
				movAverages[last.symbolID].tradeCount++;
				movAverages[last.symbolID].totalVolume += last.volume;
				movAverages[last.symbolID].averagePrice += last.price;
			}
		}
		//printf("%s first %02d:%02d last minute %02d:%02d\n",Symbols[last.symbolID],firstTime.tm_min,firstTime.tm_sec, lastTime.tm_min,lastTime.tm_sec);
	}
	for(size_t i = 0; i < symbolCount;++i){
		queue_destroy(totalPricePerMinute[i]);
		queue_destroy(totalTradesPerMinute[i]);
		queue_destroy(timestampsPerMinute[i]);
	}
	free(totalTradesPerMinute);
	free(totalPricePerMinute);
	free(timestampsPerMinute);
	free(totalTrades);
	free(totalPrices);
	return NULL;
}

void *consumerTradeLogger(void *args){
	Vector *vec = (Vector*) args;
	Trade last;
	while(!bExit){
		pthread_mutex_lock(vec->mutex);
		while(vec->size == 0 && !bExit){
			pthread_cond_wait(vec->isEmpty,vec->mutex);
		}
		while(!tradeLogging){
			pthread_mutex_unlock(vec->mutex);
			pthread_cond_wait(&tradeLoggerCondition, &tradeLoggerMutex);
		}
		if (vec->size > 0){
			vector_pop(vec,(Trade *) &last);
			pthread_mutex_unlock(vec->mutex);
			writeSymbolTradesFile(Symbols[last.symbolID], &last);
		}
		else{
			pthread_mutex_unlock(vec->mutex);
		}
	}
	return NULL;
}

void *commandListener(void *arg){
	char *command;
	size_t len = 15;
	while(!bExit){
		getline(&command, &len, stdin);
		if(strcmp(command, "LOGSTART\n") == 0){
			tradeLogging = true;
			pthread_cond_signal(&tradeLoggerCondition);
			printf("enable trade log!\n");
		}
		else if (strcmp(command, "LOGSTOP\n") == 0){
			tradeLogging = false;
			printf("disable trade log!\n");
		}
	}
	return NULL;
}

void *ticker(void *arg){
	time_t firstTime, lastTime;
	struct tm firstDate,lastDate;
	//size_t minuteCounter = 0;
	time(&lastTime);
	firstTime = lastTime;
	firstDate = *localtime(&firstTime);
	while(!bExit){
		time(&lastTime);
		lastDate = *localtime(&lastTime);
		if((lastDate.tm_min > firstDate.tm_min || (lastDate.tm_min == 0 && firstDate.tm_min == 59)) && lastDate.tm_sec == 15){ //give a grace period of 15 seconds for late trades
			printf(TICKER_LOG_COLOR "[TICKER] tick! Grace time for candle generation is over!\n" ANSI_RESET);
			firstTime = lastTime;
			firstDate = lastDate;
			//minuteCounter++;
			//save candlesticks to files
			for(size_t i = 0; i < symbolCount; ++i){
				pthread_mutex_lock(&candleMutex);
				if((int64_t) prev_candles[i].totalVolume != (int64_t) INIT_VOLUME_VALUE){
					//printf("[LAST MINUTE CANDLE %s] max: %.2lf min: %.2lf first: %.2lf, last %.2lf total volume: %.2lf\n",Symbols[i],prev_candles[i].max.price,prev_candles[i].min.price,
					//   prev_candles[i].first.price,prev_candles[i].last.price,prev_candles[i].totalVolume);
					writeCandleFile(Symbols[i], &prev_candles[i]);
					prev_candles[i].totalVolume = INIT_VOLUME_VALUE;
				}
				else{
					struct tm candleFirstDate = *localtime(&candles[i].first.timestamp);
					if(candleFirstDate.tm_min == lastDate.tm_min - 1){
						printf(TICKER_LOG_COLOR"Older candle data for symbol %s can be used!\n"ANSI_RESET,Symbols[i]);
						writeCandleFile(Symbols[i], &candles[i]);
						candles[i].totalVolume = INIT_VOLUME_VALUE;
					}
					else{
						printf(TICKER_LOG_COLOR"No candle data for symbol %s!\n"ANSI_RESET,Symbols[i]);
					}
				}
				pthread_mutex_unlock(&candleMutex);
				pthread_mutex_lock(&movAvgMutex);
				if((int64_t) prev_movAverages[i].totalVolume != (int64_t) INIT_VOLUME_VALUE){
					prev_movAverages[i].averagePrice = prev_movAverages[i].averagePrice / prev_movAverages[i].tradeCount;
					//printf("[%d MINUTE MA %s] total trades: %zu average price: %.2lf total volume: %.2lf\n",MOVING_AVERAGE_INTERVAL_MINUTES,Symbols[i],
					//	   prev_movAverages[i].tradeCount,prev_movAverages[i].averagePrice,prev_movAverages[i].totalVolume);
					writeMovingAverageFile(Symbols[i], &prev_movAverages[i]);
					prev_movAverages[i].totalVolume = INIT_VOLUME_VALUE;
				}
				pthread_mutex_unlock(&movAvgMutex);
			}
		}
		sleep(1);
	}
	return NULL;
}

// Main application entry
int main(int argc, char *argv[])
{
	char path[80];
	char *fileName;
	if(argc == 3){
		fileName = argv[2];
		snprintf(path,80,"?token=%s",argv[1]);
	}
	else{
		printf("You must specify your token first and the trading symbol, (e.g COINBASE:BTC-EUR)!\n");
		printf("Usage: %s TOKEN SYMBOLFILE\n",argv[0]);
		return 3;
	}
	mkdir(OUTPUT_DIRECTORY,0755); //create outputFolder
	symbolCount = getFileLineCount(fileName);
	printf("Found %zu symbols!\n", symbolCount);
	Symbols = readSymbolsFile(fileName, symbolCount);
	quicksortStrings(Symbols, symbolCount);
	candles = init_candle(symbolCount);
	prev_candles = init_candle(symbolCount);
	movAverages = init_movAvg(symbolCount);
	prev_movAverages = init_movAvg(symbolCount);
	lws_set_log_level(LLL_ERR| LLL_WARN | LLL_NOTICE | LLL_INFO | LLL_USER, lwsl_emit_stderr);
	signal(SIGINT, onSigInt); // Register the SIGINT handler
	// Connection info
	char inputURL[] = "wss://ws.finnhub.io";
	struct lws_context_creation_info ctxCreationInfo; // Context creation info
	struct lws_client_connect_info clientConnectInfo; // Client creation info
	struct lws_context *ctx; // The context to use
	struct lws *wsiTest; // WebSocket interface
	const char *urlProtocol, *urlTempPath; // the protocol of the URL, and a temporary pointer to the path
	// Set both information to empty and allocate it's memory
	memset(&ctxCreationInfo, 0, sizeof(ctxCreationInfo));
	memset(&clientConnectInfo, 0, sizeof(clientConnectInfo));
	if (lws_parse_uri(inputURL, &urlProtocol, &clientConnectInfo.address, &clientConnectInfo.port, &urlTempPath))
	{
		printf("Couldn't parse URL\n");
	}
    ctxCreationInfo.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
	// Set up the context creation info
	ctxCreationInfo.port = CONTEXT_PORT_NO_LISTEN; // We don't want this client to listen
	ctxCreationInfo.protocols = protocols; // Use our protocol list
    ctxCreationInfo.ssl_ca_filepath = "/etc/ssl/certs/ca-certificates.crt";
	ctxCreationInfo.gid = -1; // Set the gid and uid to -1, isn't used much
	ctxCreationInfo.uid = -1;
	ctxCreationInfo.extensions = extensions; // Use our extensions list
	// Create the context with the info
	ctx = lws_create_context(&ctxCreationInfo);
	if (ctx == NULL)
	{
		printf("Error creating context\n");
		return 1;
	}
	// Set up the client creation info
	clientConnectInfo.context = ctx; // Use our created context
    clientConnectInfo.ssl_connection = LCCSCF_USE_SSL;
    clientConnectInfo.port = 443;
	clientConnectInfo.path = path;
	clientConnectInfo.host = clientConnectInfo.address; // Set the connections host to the address
	clientConnectInfo.origin = clientConnectInfo.address; // Set the conntections origin to the address
	clientConnectInfo.ietf_version_or_minus_one = -1; // IETF version is -1 (the latest one)
	clientConnectInfo.protocol = protocols[PROTOCOL_TEST].name; // We use our test protocol
	clientConnectInfo.pwsi = &wsiTest; // The created client should be fed inside the wsi_test variable
	lwsl_notice("Connecting to %s://%s:%d%s \n\n", urlProtocol, clientConnectInfo.address, clientConnectInfo.port, clientConnectInfo.path);
	//initialize consumer queues
	candleConsVector = vector_init(10,sizeof(Trade));
	movAvgConsVector = vector_init(10, sizeof(Trade));
	tradeLogConsVector = vector_init(10, sizeof(Trade));
	// Connect with the client info
	lws_client_connect_via_info(&clientConnectInfo);
	if (wsiTest == NULL)
	{
		lwsl_err("Error creating the client\n");
		return 1;
	}
	pthread_t candleThread,movingAverageThread,tradeLoggerThread,tickerThread;
	pthread_mutex_init(&candleMutex, NULL);
	pthread_mutex_init(&movAvgMutex, NULL);
	pthread_create(&candleThread,NULL,consumerCandle,candleConsVector);
	pthread_create(&movingAverageThread, NULL, consumerMovingAverage, movAvgConsVector);
	pthread_create(&tradeLoggerThread,NULL,consumerTradeLogger,tradeLogConsVector);
	pthread_mutex_init(&tradeLoggerMutex, NULL);
	pthread_cond_init(&tradeLoggerCondition, NULL);
	pthread_create(&keyLogger, NULL, commandListener, NULL);
	pthread_create(&tickerThread, NULL, ticker, NULL);
	// Main loop runs till bExit is true, which forces an exit of this loop
	while (!bExit)
	{
		// LWS' function to run the message loop, which polls in this example every 100 milliseconds on our created context
		lws_service(ctx, 100);
	}
	pthread_join(candleThread,NULL);
	pthread_join(movingAverageThread,NULL);
	pthread_join(tradeLoggerThread,NULL);
	pthread_cond_destroy(&tradeLoggerCondition);
	pthread_mutex_destroy(&tradeLoggerMutex);
	pthread_join(keyLogger,NULL);
	pthread_join(tickerThread, NULL);
	pthread_mutex_destroy(&candleMutex);
	pthread_mutex_destroy(&movAvgMutex);
	//free resources
	lws_context_destroy(ctx);
	vector_destroy(candleConsVector);
	vector_destroy(movAvgConsVector);
	vector_destroy(tradeLogConsVector);
	for(size_t i = 0; i < symbolCount; ++i){
		free(Symbols[i]);
	}
	free(Symbols);
	destroy_candle(candles);
	destroy_candle(prev_candles);
	destroy_movAvg(movAverages);
	destroy_movAvg(prev_movAverages);
	printf("Done executing.\n");
	return 0;
}
