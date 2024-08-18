#ifndef CONSTANTS_H_
#define CONSTANTS_H_

#define SYMBOL_LENGTH 50
#define SUBSCRIBE_MESSAGE_LENGTH 200
#define VECTOR_INIT_SIZE 10
#define WS_DOMAIN_NAME "ws.finnhub.io"
#define INIT_VOLUME_VALUE -1
#ifndef OUTPUT_DIRECTORY
#define OUTPUT_DIRECTORY "out"
#endif
#ifndef MOVING_AVERAGE_INTERVAL_MINUTES
#define MOVING_AVERAGE_INTERVAL_MINUTES 3
#endif
#define ANSI_RESET "\x1b[0m"
#define CANDLE_LOG_COLOR "\x1b[36m" //cyan
#define MA_LOG_COLOR "\x1b[33m" //yellow
#define TICKER_LOG_COLOR "\x1b[35m" //magenta
#define ERROR_LOG_COLOR "\x1b[31;1m" //bold red
#endif // CONSTANTS_H_
