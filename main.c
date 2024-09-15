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
static bool force_reconnect = false;
static size_t symbolCount;
static Candle *candles, *prev_candles;
static MovingAverage* movAverages, *prev_movAverages;
static bool bDenyDeflate = true;
static char** Symbols;
static pthread_mutex_t tradeLoggerMutex, candleMutex, movAvgMutex;
static pthread_cond_t tradeLoggerCondition;
static size_t *totalTrades;
static double *totalVolumes, *totalPrices;
static Queue** movAvgQueuePtr = NULL; //this pointer enables ticker thread to check whether queues are full or not
static int callback_test(struct lws* wsi, enum lws_callback_reasons reason, void *user, void* in, size_t len);
static Vector *candleConsVector, *movAvgConsVector, *tradeLogConsVector;

// Escape the loop when a SIGINT signal is received
static void onSigInt(int sig)
{
	lwsl_warn("Caught SIGINT! Exiting!\n");
	bExit = true;
	//wake up any threads sleeping
	pthread_cond_signal(candleConsVector->isEmpty);
	pthread_cond_signal(movAvgConsVector->isEmpty);
	if(tradeLogging){
		pthread_cond_signal(&tradeLoggerCondition);
		pthread_cond_signal(tradeLogConsVector->isEmpty);
	}
}

// The registered protocols
static struct lws_protocols protocols[] = {
	{
		"test-protocol", // Protocol name
		callback_test,   // Protocol callback
		0,
		0,
		0,
		NULL,
		0
	},
	LWS_PROTOCOL_LIST_TERM // Always needed at the end { NULL, NULL, 0, 0, 0, NULL, 0 }
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
	char sending_message[SUBSCRIBE_MESSAGE_LENGTH];
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
		cJSON* jsonResponse = cJSON_Parse(in);
		if(jsonResponse == NULL){
			lwsl_err("Error parsing JSON response!\n");
			const char *error_ptr = cJSON_GetErrorPtr();
			if (error_ptr != NULL){
				lwsl_err("Error before: %s\n", error_ptr);
			}
			bExit = true;
			return 5;
		}
		//printf("========================================================================\n");
		//printf("[Test Protocol] Received data: \"%s\", length: %zu bytes\n", (char*)in,len);
		cJSON *data = cJSON_GetObjectItemCaseSensitive(jsonResponse, "data");
		size_t symbolID;
		if (data != NULL){
			cJSON *item;
			cJSON_ArrayForEach(item, data){
				if(item != NULL){
					cJSON *priceItem, *timeItem, *volumeItem, *symbolItem;
					Trade tradeItem;
					time_t currentTimestamp;
					struct tm tradeDate, currentDate;
					timeItem = cJSON_GetObjectItem(item, "t");
					priceItem = cJSON_GetObjectItem(item, "p");
					volumeItem = cJSON_GetObjectItem(item, "v");
					symbolItem = cJSON_GetObjectItemCaseSensitive(item, "s");
					if(symbolItem != NULL && priceItem != NULL && volumeItem != NULL && timeItem != NULL){
						symbolID = searchString(Symbols, symbolItem->valuestring, symbolCount);
						if(symbolID == UINT32_MAX){
							lwsl_err("Symbol ID %s not found!\n", symbolItem->valuestring);
						}
						tradeItem.symbolID = symbolID;
						tradeItem.price = priceItem->valuedouble;
						tradeItem.timestamp = (uint64_t) timeItem->valuedouble / 1000;
						tradeItem.volume = volumeItem->valuedouble;
						tradeDate = *localtime(&tradeItem.timestamp);
						clock_gettime(CLOCK_MONOTONIC, &tradeItem.insertionTime);
						time(&currentTimestamp);
						currentDate = *localtime(&currentTimestamp);
						// If trade timestamp is on the same minute, pass it to other threads, else ignore it
						if(currentDate.tm_min == tradeDate.tm_min){
							pthread_mutex_lock(candleConsVector->mutex);
							vector_push_back(candleConsVector, &tradeItem);
							pthread_cond_signal(candleConsVector->isEmpty);
							pthread_mutex_unlock(candleConsVector->mutex);
							pthread_mutex_lock(movAvgConsVector->mutex);
							vector_push_back(movAvgConsVector, &tradeItem);
							pthread_cond_signal(movAvgConsVector->isEmpty);
							pthread_mutex_unlock(movAvgConsVector->mutex);
							if(tradeLogging){ //avoid wasting memory when logging is disabled
								pthread_mutex_lock(tradeLogConsVector->mutex);
								vector_push_back(tradeLogConsVector, &tradeItem);
								pthread_cond_signal(tradeLogConsVector->isEmpty);
								pthread_mutex_unlock(tradeLogConsVector->mutex);
							}
						}
					}
					else{
						if(symbolItem != NULL){
							lwsl_err("Missing fields for symbol %s!\n",symbolItem->valuestring);
						}
					}
				}
				else {
					lwsl_err("Invalid data JSON object\n");
				}
				//printf("Time: %02d:%02d:%02d, symbol: %s, price: %.2lf, volume: %lf\n",ctimestamp.tm_hour,ctimestamp.tm_min,ctimestamp.tm_sec,Symbols[symbolID],priceItem->valuedouble, volumeItem->valuedouble);
			}
		}
		cJSON_Delete(jsonResponse);
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
		sleep(1);
		lws_callback_on_writable(wsi); //notify when client is able to write
		break;

	// Now we can write data
	case LWS_CALLBACK_CLIENT_WRITEABLE:
		lwsl_info("[Test Protocol] The client is able to write.\n");
		for(size_t i = 0; i < symbolCount;++i){
			memset(sending_message, 0, SUBSCRIBE_MESSAGE_LENGTH);
			snprintf(sending_message, SUBSCRIBE_MESSAGE_LENGTH,"{\"type\":\"subscribe\", \"symbol\":\"%s\"}",Symbols[i]);
			lwsl_info("[Test Protocol] Writing \"%s\" to server.\n", sending_message);
			lws_write(wsi, (unsigned char*)sending_message, strlen(sending_message), LWS_WRITE_TEXT);
		}
		lwsl_user("Just subscribed to all specified symbols!\n");
		break;

		// There was an error connecting to the server
	case LWS_CALLBACK_CLIENT_CLOSED:
	case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
		force_reconnect = true;
		lwsl_err("[Test Protocol] There was a connection error: %s\n", in ? (char*)in : "(no error information)");
		break;

	default:
		break;
	}

	return 0;
}

void *consumerCandle(void *q){
	Vector *vec = (Vector*) q;
	Trade last;
	struct tm firstTime, lastTime;
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
		//printf("consumerCandle thread got price %.2lf of symbol %s, time: %02d:%02d:%02d, volume: %lf\n",last.price,Symbols[last.symbolID],lastTime.tm_hour,lastTime.tm_min,lastTime.tm_sec,last.volume);
		if ((int64_t) candles[last.symbolID].totalVolume == (int64_t) INIT_VOLUME_VALUE){
			reset_candle(&candles[last.symbolID], &last);
		}
		else if(firstTime.tm_min == lastTime.tm_min -1 || (firstTime.tm_min == 59 && lastTime.tm_min == 0)){
			printf(CANDLE_LOG_COLOR"FINAL CANDLE for %s initialized! Time range: %02d:%02d:%02d -> %02d:%02d:%02d\n"ANSI_RESET, Symbols[last.symbolID],firstTime.tm_hour,
				       firstTime.tm_min,firstTime.tm_sec,lastTime.tm_hour,lastTime.tm_min,lastTime.tm_sec);
			pthread_mutex_lock(&candleMutex);
			prev_candles[last.symbolID] = candles[last.symbolID];
			reset_candle(&candles[last.symbolID], &last);
			pthread_mutex_unlock(&candleMutex);
		}
		else if(firstTime.tm_min == lastTime.tm_min){
				if(last.price < candles[last.symbolID].min.price){
					candles[last.symbolID].min = last;
				}
				if(last.price > candles[last.symbolID].max.price){
					candles[last.symbolID].max = last;
				}
				candles[last.symbolID].totalVolume += last.volume;
		}
	}
	return NULL;
}

void* consumerMovingAverage(void* args){
	Vector* vec = (Vector*) args;
	Queue** totalTradesPerMinute = (Queue**) malloc(symbolCount*sizeof(Queue*));
	Queue** totalPricePerMinute = (Queue**) malloc(symbolCount*sizeof(Queue*));
	Queue** timestampsPerMinute = (Queue**) malloc(symbolCount*sizeof(Queue*));
	Queue** totalVolumePerMinute = (Queue**) malloc(symbolCount*sizeof(Queue*));
	bool oldDataFound;
	int8_t diffMinutes = 0;
	time_t currentTimestamp;
	movAvgQueuePtr = totalPricePerMinute; //enables ticker thread to check whether queues are full or not
	for(size_t i = 0; i < symbolCount;++i){
		totalPricePerMinute[i] = queue_init(MOVING_AVERAGE_INTERVAL_MINUTES, sizeof(double));
		totalTradesPerMinute[i] = queue_init(MOVING_AVERAGE_INTERVAL_MINUTES, sizeof(size_t));
		timestampsPerMinute[i] = queue_init(MOVING_AVERAGE_INTERVAL_MINUTES, sizeof(time_t));
		totalVolumePerMinute[i] = queue_init(MOVING_AVERAGE_INTERVAL_MINUTES, sizeof(double));
	}
	struct tm firstTime, lastTime, currentTime;
	Trade last;
	while(!bExit){
		oldDataFound = false;
		pthread_mutex_lock(vec->mutex);
		while(vec->size == 0 && !bExit){
			pthread_cond_wait(vec->isEmpty,vec->mutex);
		}
		vector_pop(movAvgConsVector,(Trade *) &last);
		pthread_mutex_unlock(vec->mutex);
		lastTime = *localtime(&last.timestamp);
		firstTime = *localtime(&movAverages[last.symbolID].first.timestamp);
		diffMinutes = lastTime.tm_min - firstTime.tm_min;
		time(&currentTimestamp);
		currentTime = *localtime(&currentTimestamp);
		if(!totalPricePerMinute[last.symbolID]->isEmpty){
			time_t timestamp;
			for(size_t i = 0; i < totalPricePerMinute[last.symbolID]->size;++i){
				queue_peek_head(timestampsPerMinute[last.symbolID], (time_t*) &timestamp);
				if((int64_t) difftime(last.timestamp, timestamp) >= MOVING_AVERAGE_INTERVAL_MINUTES*60 + 20){
					double oldPrice, oldVolume;
					size_t oldTrades;
					lwsl_warn("Too old data found in MA queue for symbol %s, cleaning up!", Symbols[last.symbolID]);
					queue_pop(totalPricePerMinute[last.symbolID], &oldPrice);
					queue_pop(totalVolumePerMinute[last.symbolID], &oldVolume);
					queue_pop(timestampsPerMinute[last.symbolID], &timestamp);
					queue_pop(totalTradesPerMinute[last.symbolID], &oldTrades);
					totalTrades[last.symbolID] -= oldTrades;
					totalPrices[last.symbolID] -= oldPrice;
					totalVolumes[last.symbolID] -= oldVolume;
					oldDataFound = true;
				}
				else break;
			}
		}
		if ((int64_t) movAverages[last.symbolID].totalVolume == (int64_t) INIT_VOLUME_VALUE){
			reset_movAvg(&movAverages[last.symbolID], &last);
		}
		else if(diffMinutes > 0 || ( diffMinutes < 0 && currentTime.tm_min == lastTime.tm_min)){
			totalPrices[last.symbolID] += movAverages[last.symbolID].averagePrice;
			totalTrades[last.symbolID] += movAverages[last.symbolID].tradeCount;
			totalVolumes[last.symbolID] += movAverages[last.symbolID].totalVolume;
			queue_insert(totalTradesPerMinute[last.symbolID], &movAverages[last.symbolID].tradeCount);
			queue_insert(totalPricePerMinute[last.symbolID], &movAverages[last.symbolID].averagePrice);
			queue_insert(timestampsPerMinute[last.symbolID], &movAverages[last.symbolID].first.timestamp);
			queue_insert(totalVolumePerMinute[last.symbolID], &movAverages[last.symbolID].totalVolume);
			pthread_mutex_lock(&movAvgMutex);
			if(totalPricePerMinute[last.symbolID]->isFull || oldDataFound){
				queue_peek_head(timestampsPerMinute[last.symbolID],(time_t*) &movAverages[last.symbolID].first.timestamp);
				firstTime = *localtime(&movAverages[last.symbolID].first.timestamp);
				printf(MA_LOG_COLOR"%s %d FINAL MA triggered! Time range: %02d:%02d:%02d -> %02d:%02d:%02d\n"ANSI_RESET,Symbols[last.symbolID],MOVING_AVERAGE_INTERVAL_MINUTES,
                     firstTime.tm_hour,firstTime.tm_min,firstTime.tm_sec,lastTime.tm_hour,lastTime.tm_min,lastTime.tm_sec);
				movAverages[last.symbolID].tradeCount = totalTrades[last.symbolID];
				movAverages[last.symbolID].averagePrice = totalPrices[last.symbolID];
				movAverages[last.symbolID].totalVolume = totalVolumes[last.symbolID];
				movAverages[last.symbolID].stopTime = last.timestamp;
				prev_movAverages[last.symbolID] = movAverages[last.symbolID];
			}
			reset_movAvg(&movAverages[last.symbolID], &last);
			pthread_mutex_unlock(&movAvgMutex);
		}
		else{
				movAverages[last.symbolID].tradeCount++;
				movAverages[last.symbolID].totalVolume += last.volume;
				movAverages[last.symbolID].averagePrice += last.price;
				movAverages[last.symbolID].stopTime = last.timestamp;
		}
	}
	for(size_t i = 0; i < symbolCount;++i){
		queue_destroy(totalPricePerMinute[i]);
		queue_destroy(totalTradesPerMinute[i]);
		queue_destroy(timestampsPerMinute[i]);
		queue_destroy(totalVolumePerMinute[i]);
	}
	free(totalTradesPerMinute);
	free(totalPricePerMinute);
	free(timestampsPerMinute);
	free(totalVolumePerMinute);
	return NULL;
}

void *consumerTradeLogger(void *args){
	Vector *vec = (Vector*) args;
	Trade last;
	struct timespec popTime;
	uint64_t processingTime = 0;
	while(!bExit){
		while(!tradeLogging){
			pthread_cond_wait(&tradeLoggerCondition, &tradeLoggerMutex);
		}
		pthread_mutex_lock(vec->mutex);
		while(vec->size == 0 && !bExit){
			pthread_cond_wait(vec->isEmpty,vec->mutex);
		}
		if (vec->size > 0){
			vector_pop(vec,(Trade *) &last);
			clock_gettime(CLOCK_MONOTONIC, &popTime);
			pthread_mutex_unlock(vec->mutex);
			processingTime = difftimespec_us(&popTime, &last.insertionTime);
			writeSymbolTradesFile(Symbols[last.symbolID], &last, processingTime);
		}
		else{
			pthread_mutex_unlock(vec->mutex);
		}
	}
	return NULL;
}

void *ticker(void *arg){
	time_t firstTime, lastTime;
	struct tm firstDate,lastDate;
	time(&lastTime);
	firstTime = lastTime;
	firstDate = *localtime(&firstTime);
	while(!bExit){
		time(&lastTime);
		lastDate = *localtime(&lastTime);
		if(lastDate.tm_min != firstDate.tm_min && lastDate.tm_sec >= 15){ //give a grace period of 15 seconds for late trades
			printf(TICKER_LOG_COLOR "[TICKER] tick! Grace time for data generation is over!\n" ANSI_RESET);
			firstTime = lastTime;
			firstDate = lastDate;
			//save candlesticks to files
			for(size_t i = 0; i < symbolCount; ++i){
				pthread_mutex_lock(&candleMutex);
				if((int64_t) prev_candles[i].totalVolume != (int64_t) INIT_VOLUME_VALUE){
					writeCandleFile(Symbols[i], &prev_candles[i]);
					prev_candles[i].totalVolume = INIT_VOLUME_VALUE;
				}
				else{
					struct tm candleFirstDate = *localtime(&candles[i].first.timestamp);
					struct tm candleLastDate = *localtime(&candles[i].last.timestamp);
					if((int64_t) candles[i].totalVolume != INIT_VOLUME_VALUE && (candleFirstDate.tm_min == lastDate.tm_min - 1 ||
                                      (candleFirstDate.tm_min == 59 && lastDate.tm_min == 0 ))){
						printf(TICKER_LOG_COLOR"Older candle data for symbol %s can be used! Time range: %02d:%02d:%02d -> %02d:%02d:%02d\n"ANSI_RESET,
							   Symbols[i],candleFirstDate.tm_hour,candleFirstDate.tm_min,candleFirstDate.tm_sec,
                               candleLastDate.tm_hour,candleLastDate.tm_min,candleLastDate.tm_sec);
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
					writeMovingAverageFile(Symbols[i], &prev_movAverages[i]);
					prev_movAverages[i].totalVolume = INIT_VOLUME_VALUE;
				}
				else if((int64_t) movAverages[i].totalVolume != INIT_VOLUME_VALUE && movAvgQueuePtr != NULL){
					if(movAvgQueuePtr[i]->isFull){
						struct tm MAFirstDate = *localtime(&movAverages[i].first.timestamp);
						struct tm MALastDate = *localtime(&movAverages[i].stopTime);
						printf(TICKER_LOG_COLOR"Older MA data for symbol %s can be used! Time range: %02d:%02d:%02d -> %02d:%02d:%02d\n"ANSI_RESET,
							   Symbols[i],MAFirstDate.tm_hour,MAFirstDate.tm_min,MAFirstDate.tm_sec,
                               MALastDate.tm_hour,MALastDate.tm_min,MALastDate.tm_sec);
						movAverages[i].averagePrice = totalPrices[i] / totalTrades[i];
						movAverages[i].tradeCount = totalTrades[i];
						movAverages[i].totalVolume = totalVolumes[i];
						writeMovingAverageFile(Symbols[i], &movAverages[i]);
						movAverages[i].totalVolume = INIT_VOLUME_VALUE;
					}
					else{
						printf(TICKER_LOG_COLOR"No MA data for symbol %s yet!\n"ANSI_RESET,Symbols[i]);
					}
				}
				else{
					printf(TICKER_LOG_COLOR"No MA data for symbol %s!\n"ANSI_RESET,Symbols[i]);
				}
				pthread_mutex_unlock(&movAvgMutex);
			}
		}
		sleep(1);
	}
	return NULL;
}

void setup_context(struct lws_context_creation_info *ctxCreationInfo){
	memset(ctxCreationInfo, 0, sizeof(*ctxCreationInfo));
    ctxCreationInfo->options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
	// Set up the context creation info
	ctxCreationInfo->port = CONTEXT_PORT_NO_LISTEN; // We don't want this client to listen
	ctxCreationInfo->protocols = protocols; // Use our protocol list
    ctxCreationInfo->ssl_ca_filepath = "/etc/ssl/certs/ca-certificates.crt";
	ctxCreationInfo->gid = -1; // Set the gid and uid to -1, isn't used much
	ctxCreationInfo->uid = -1;
	ctxCreationInfo->extensions = extensions; // Use our extensions list
}

void setup_ws_connection(struct lws_client_connect_info* clientConnectInfo, struct lws_context *ctx,struct lws** wsi, char path[80]){
	// Set up the client creation info
	memset(clientConnectInfo,0, sizeof(*clientConnectInfo));
	clientConnectInfo->address = WS_DOMAIN_NAME;
	clientConnectInfo->context = ctx; // Use our created context
    clientConnectInfo->ssl_connection = LCCSCF_USE_SSL;
    clientConnectInfo->port = 443;
	clientConnectInfo->path = path;
	clientConnectInfo->host = clientConnectInfo->address; // Set the connections host to the address
	clientConnectInfo->origin = clientConnectInfo->address; // Set the conntections origin to the address
	clientConnectInfo->ietf_version_or_minus_one = -1; // IETF version is -1 (the latest one)
	clientConnectInfo->protocol = protocols[PROTOCOL_TEST].name; // We use our test protocol
	clientConnectInfo->pwsi = wsi; // The created client should be fed inside the wsi_test variable
}

// Main application entry
int main(int argc, char *argv[])
{
	char path[80];
	char *fileName;
	switch(argc){
		case 4:
			if(strcmp(argv[1], "-l") == 0){
				tradeLogging = true;
				fileName = argv[3];
				snprintf(path,80,"?token=%s",argv[2]);
				printf("Trade logging enabled!\n");
			}
			else{
				printf("Invalid option! Try again!\n");
				print_helptext(argv[0]);
				return 3;
			}
			break;
		case 3:
			fileName = argv[2];
			snprintf(path,80,"?token=%s",argv[1]);
			break;
		case 2:
			if(strcmp(argv[1], "--help") == 0){
				print_helptext(argv[0]);
			}
			else{
				printf("Invalid option! Try again!\n");
				print_helptext(argv[0]);
			}
			return 3;
		default:
			printf("Invalid option! Try again!\n");
			print_helptext(argv[0]);
			return 3;
	}
	setvbuf(stdout,NULL,_IONBF,0); //disable printf buffering
	mkdir(OUTPUT_DIRECTORY,0755); //create outputFolder
	symbolCount = getFileLineCount(fileName);
	printf("Found %zu symbols!\n", symbolCount);
	Symbols = readSymbolsFile(fileName, symbolCount);
	quicksortStrings(Symbols, symbolCount);
	candles = init_candle(symbolCount);
	prev_candles = init_candle(symbolCount);
	movAverages = init_movAvg(symbolCount);
	prev_movAverages = init_movAvg(symbolCount);
	totalTrades = (size_t*) malloc(symbolCount*sizeof(size_t));
	totalPrices = (double*) malloc(symbolCount*sizeof(double));
	totalVolumes = (double*) malloc(symbolCount*sizeof(double));
	memset(totalPrices, 0, symbolCount*sizeof(double));
	memset(totalTrades,0,symbolCount*sizeof(size_t));
	memset(totalVolumes,0,symbolCount*sizeof(double));
	#ifdef DEBUG
	lws_set_log_level(LLL_ERR| LLL_WARN | LLL_NOTICE | LLL_INFO | LLL_USER, lwsl_emit_stderr);
	#else
	lws_set_log_level(LLL_ERR| LLL_WARN | LLL_NOTICE | LLL_USER, lwsl_emit_stderr);
	#endif
	signal(SIGINT, onSigInt); // Register the SIGINT handler
	// Connection info
	//char inputURL[] = "wss://ws.finnhub.io";
	struct lws_context_creation_info ctxCreationInfo; // Context creation info
	struct lws_client_connect_info clientConnectInfo; // Client creation info
	struct lws_context *ctx; // The context to use
	struct lws *wsiTest; // WebSocket interface
	// Create the context with the info
	setup_context(&ctxCreationInfo);
	ctx = lws_create_context(&ctxCreationInfo);
	if (ctx == NULL)
	{
		lwsl_err("Error creating context\n");
		return 1;
	}
	// Set up the client creation info
	setup_ws_connection(&clientConnectInfo, ctx, &wsiTest, path);
	//initialize consumer queues
	candleConsVector = vector_init(VECTOR_INIT_SIZE,sizeof(Trade));
	movAvgConsVector = vector_init(VECTOR_INIT_SIZE, sizeof(Trade));
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
	if(tradeLogging){
		tradeLogConsVector = vector_init(VECTOR_INIT_SIZE, sizeof(Trade));
		pthread_mutex_init(&tradeLoggerMutex, NULL);
		pthread_cond_init(&tradeLoggerCondition, NULL);
		pthread_create(&tradeLoggerThread,NULL,consumerTradeLogger,tradeLogConsVector);
	}
	pthread_create(&tickerThread, NULL, ticker, NULL);
	// Main loop runs till bExit is true, which forces an exit of this loop
	while (!bExit)
	{
		if(wsiTest == NULL || force_reconnect){
			force_reconnect = false;
			lwsl_err("Connection destroyed! Attempting reconnect!\n");
			sleep(3);
			// Clean up previous WebSocket interface
			if(ctx != NULL){
				lws_context_destroy(ctx);
			}
			sleep(10);
			// Create the context with the info
			setup_context(&ctxCreationInfo);
			ctx = lws_create_context(&ctxCreationInfo);
			// Set up the client creation info
			setup_ws_connection(&clientConnectInfo, ctx, &wsiTest, path);
			lws_client_connect_via_info(&clientConnectInfo);
		}
		// LWS' function to run the message loop, which polls in this example every 100 milliseconds on our created context
		if(ctx != NULL && wsiTest != NULL){
			lws_service(ctx, 100);
		}
	}
	pthread_join(candleThread,NULL);
	pthread_join(movingAverageThread,NULL);
	if(tradeLogging){
		pthread_join(tradeLoggerThread,NULL);
		pthread_cond_destroy(&tradeLoggerCondition);
		pthread_mutex_destroy(&tradeLoggerMutex);
		vector_destroy(tradeLogConsVector);
	}
	pthread_join(tickerThread, NULL);
	pthread_mutex_destroy(&candleMutex);
	pthread_mutex_destroy(&movAvgMutex);
	//free resources
	lws_context_destroy(ctx);
	vector_destroy(candleConsVector);
	vector_destroy(movAvgConsVector);
	for(size_t i = 0; i < symbolCount; ++i){
		free(Symbols[i]);
	}
	free(Symbols);
	destroy_candle(candles);
	destroy_candle(prev_candles);
	destroy_movAvg(movAverages);
	destroy_movAvg(prev_movAverages);
	free(totalTrades);
	free(totalPrices);
	free(totalVolumes);
	printf("Done executing.\n");
	return 0;
}
