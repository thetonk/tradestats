#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <libwebsockets.h>
#include <pthread.h>
#include <time.h>
#include "include/cJSON.h"
#include "include/utilities.h"
#include "include/vector.h"
#include "include/constants.h"

static bool bExit = false;
static size_t symbolCount;
static Candle *candles;
static bool bDenyDeflate = true;
static char** Symbols;
static int callback_test(struct lws* wsi, enum lws_callback_reasons reason, void *user, void* in, size_t len);
static Vector* myVector;

// Escape the loop when a SIGINT signal is received
static void onSigInt(int sig)
{
	bExit = true;
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
		uint8_t candleStartMinute = 0;
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
				trade tradeItem;
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
				pthread_mutex_lock(myVector->mutex);
				vector_push_back(myVector, &tradeItem);
				pthread_cond_signal(myVector->isEmpty);
				pthread_mutex_unlock(myVector->mutex);
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
	trade last;
	while(!bExit){
		pthread_mutex_lock(vec->mutex);
		while(vec->size == 0 && !bExit){
			//lwsl_warn("Thread sleeping, vector empty\n");
			pthread_cond_wait(vec->isEmpty,vec->mutex);
		}
		vector_pop(myVector,(trade *) &last);
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
	symbolCount = getFileLineCount(fileName);
	printf("Found %zu symbols!\n", symbolCount);
	Symbols = readSymbolsFile(fileName, symbolCount);
	quicksortStrings(Symbols, symbolCount);
	candles = (Candle*) malloc(symbolCount*sizeof(Candle));
	for(size_t i = 0; i < symbolCount; ++i){
		candles[i].totalVolume = INIT_VOLUME_VALUE; //initial dummy value
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
	myVector = vector_init(10,sizeof(trade));
	// Connect with the client info
	lws_client_connect_via_info(&clientConnectInfo);
	if (wsiTest == NULL)
	{
		lwsl_err("Error creating the client\n");
		return 1;
	}
	pthread_t thread;
	pthread_create(&thread,NULL,consumerCandle,myVector);
	// Main loop runs till bExit is true, which forces an exit of this loop
	while (!bExit)
	{
		// LWS' function to run the message loop, which polls in this example every 50 milliseconds on our created context
		lws_service(ctx, 100);
	}
	// Destroy the context, and wake up any threads sleeping
	pthread_cond_signal(myVector->isEmpty);
	pthread_join(thread,NULL);
	//free resources
	lws_context_destroy(ctx);
	vector_destroy(myVector);
	for(size_t i = 0; i < symbolCount; ++i){
		free(Symbols[i]);
	}
	free(Symbols);
	free(candles);
	printf("Done executing.\n");
	return 0;
}
