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
#include "include/cJSON.h"
#include "include/utilities.h"
#include "include/vector.h"
#include "include/constants.h"

static bool bExit = false;
static bool tradeLogging = false;
static size_t symbolCount;
static Candle *candles;
static MovingAverage* movAverages;
static bool bDenyDeflate = true;
static char** Symbols;
static pthread_t keyLogger;
static pthread_mutex_t tradeLoggerMutex;
static pthread_cond_t tradeLoggerCondition;
static int callback_test(struct lws* wsi, enum lws_callback_reasons reason, void *user, void* in, size_t len);
static Vector *candleConsVector, *movAvgConsVector, *tradeLogConsVector;

// Escape the loop when a SIGINT signal is received
static void onSigInt(int sig)
{
	printf("\ncaught sigint!\n");
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
				tradeItem.timestamp = timeItem->valuedouble/1000;
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
	struct tm firstTime, lastTime;
	Trade last;
	while(!bExit){
		pthread_mutex_lock(vec->mutex);
		while(vec->size == 0 && !bExit){
			//lwsl_warn("Thread sleeping, vector empty\n");
			pthread_cond_wait(vec->isEmpty,vec->mutex);
		}
		vector_pop(candleConsVector,(Trade *) &last);
		pthread_mutex_unlock(vec->mutex);
		candles[last.symbolID].last = last;
		lastTime = *localtime(&candles[last.symbolID].last.timestamp);
		firstTime = *localtime(&(candles[last.symbolID].first.timestamp));
		//printf("consumerCandle thread got price %.2lf of symbol %s, time: %02d:%02d:%02d, volume: %lf\n",last.price,Symbols[last.symbolID],lastTime.tm_hour,lastTime.tm_min,lastTime.tm_sec,last.volume);
		if ((int64_t) candles[last.symbolID].totalVolume == (int64_t) INIT_VOLUME_VALUE){
			candles[last.symbolID].totalVolume = 0;
			candles[last.symbolID].first = last;
			candles[last.symbolID].min = last;
			candles[last.symbolID].max = last;
			printf("candle %s initialized!\n", Symbols[last.symbolID]);
		}
		else if(firstTime.tm_min < lastTime.tm_min){
			Candle finalCandle = candles[last.symbolID];
			printf("[LAST MINUTE CANDLE %s] max: %.2lf min: %.2lf first: %.2lf, last %.2lf total volume: %.2lf\n",Symbols[last.symbolID],finalCandle.max.price,finalCandle.min.price,finalCandle.first.price,finalCandle.last.price,finalCandle.totalVolume);
			writeCandleFile(Symbols[last.symbolID], &finalCandle);
			candles[last.symbolID].totalVolume = 0;
			candles[last.symbolID].first = last;
			candles[last.symbolID].min = last;
			candles[last.symbolID].max = last;
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
		//printf("%s first %02d:%02d last minute %02d:%02d\n",Symbols[last.symbolID],firstTime.tm_min,firstTime.tm_sec, lastTime.tm_min,lastTime.tm_sec);
	}
	return NULL;
}

void* consumerMovingAverage(void* args){
	Vector* vec = (Vector*) args;
	Trade last;
	while(!bExit){
		pthread_mutex_lock(vec->mutex);
		while(vec->size == 0 && !bExit){
			//lwsl_warn("Thread sleeping, vector empty\n");
			pthread_cond_wait(vec->isEmpty,vec->mutex);
		}
		vector_pop(movAvgConsVector,(Trade *) &last);
		pthread_mutex_unlock(vec->mutex);
		candles[last.symbolID].last = last;
		struct tm lastTime = *localtime(&candles[last.symbolID].last.timestamp);
		//printf("consumerMA thread got price %.2lf of symbol %s, time: %02d:%02d:%02d, volume: %lf\n",last.price,Symbols[last.symbolID],lastTime.tm_hour,lastTime.tm_min,lastTime.tm_sec,last.volume);
		if ((int64_t) movAverages[last.symbolID].totalVolume == (int64_t) INIT_VOLUME_VALUE){
			movAverages[last.symbolID].totalVolume = 0;
			movAverages[last.symbolID].first = last;
			movAverages[last.symbolID].averagePrice = 0;
			movAverages[last.symbolID].tradeCount = 0;
			printf("Moving average %s initialized!\n", Symbols[last.symbolID]);
		}
		else if((int64_t) difftime(last.timestamp,movAverages[last.symbolID].first.timestamp) >= 3*60){ //3 minutes passed
			movAverages[last.symbolID].averagePrice = movAverages[last.symbolID].averagePrice/movAverages[last.symbolID].tradeCount;
			MovingAverage finalMovingAvg = movAverages[last.symbolID];
			printf("[3 MINUTE MA %s] total trades: %zu average price: %.2lf total volume: %.2lf\n",Symbols[last.symbolID],finalMovingAvg.tradeCount,finalMovingAvg.averagePrice,finalMovingAvg.totalVolume);
			writeMovingAverageFile(Symbols[last.symbolID], &finalMovingAvg);
			movAverages[last.symbolID].totalVolume = 0;
			movAverages[last.symbolID].first = last;
			movAverages[last.symbolID].averagePrice = 0;
			movAverages[last.symbolID].tradeCount = 0;
		}
		else{
			movAverages[last.symbolID].tradeCount++;
			movAverages[last.symbolID].totalVolume += last.volume;
			movAverages[last.symbolID].averagePrice += last.price;
		}
		//printf("%s first %02d:%02d last minute %02d:%02d\n",Symbols[last.symbolID],firstTime.tm_min,firstTime.tm_sec, lastTime.tm_min,lastTime.tm_sec);
	}
	return NULL;
}

void *consumerTradeLogger(void *args){
	Vector *vec = (Vector*) args;
	Trade last;
	while(!bExit){
		pthread_mutex_lock(vec->mutex);
		while(vec->size == 0 && !bExit){
			//lwsl_warn("Thread sleeping, vector empty\n");
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
	candles = (Candle*) malloc(symbolCount*sizeof(Candle));
	movAverages = (MovingAverage*) malloc(symbolCount*sizeof(MovingAverage));
	for(size_t i = 0; i < symbolCount; ++i){
		candles[i].totalVolume = INIT_VOLUME_VALUE; //initial dummy value
		movAverages[i].totalVolume = INIT_VOLUME_VALUE; //initial dummy value
	}
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
	pthread_t candleThread,movingAverageThread,tradeLoggerThread;
	pthread_create(&candleThread,NULL,consumerCandle,candleConsVector);
	pthread_create(&movingAverageThread, NULL, consumerMovingAverage, movAvgConsVector);
	pthread_create(&tradeLoggerThread,NULL,consumerTradeLogger,tradeLogConsVector);
	pthread_mutex_init(&tradeLoggerMutex, NULL);
	pthread_cond_init(&tradeLoggerCondition, NULL);
	pthread_create(&keyLogger, NULL, commandListener, NULL);
	// Main loop runs till bExit is true, which forces an exit of this loop
	while (!bExit)
	{
		// LWS' function to run the message loop, which polls in this example every 50 milliseconds on our created context
		lws_service(ctx, 100);
	}
	// Destroy the context
	pthread_join(candleThread,NULL);
	pthread_join(movingAverageThread,NULL);
	pthread_join(tradeLoggerThread,NULL);
	pthread_cond_destroy(&tradeLoggerCondition);
	pthread_mutex_destroy(&tradeLoggerMutex);
	pthread_join(keyLogger,NULL);
	//free resources
	lws_context_destroy(ctx);
	vector_destroy(candleConsVector);
	vector_destroy(movAvgConsVector);
	vector_destroy(tradeLogConsVector);
	for(size_t i = 0; i < symbolCount; ++i){
		free(Symbols[i]);
	}
	free(Symbols);
	free(candles);
	free(movAverages);
	printf("Done executing.\n");
	return 0;
}
