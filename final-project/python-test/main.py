#!/usr/bin/env python3
import websocket,rel,os
from dotenv import load_dotenv

load_dotenv()
FINNHUB_TOKEN = os.getenv("FINNHUB-TOKEN")
def on_message(ws: websocket.WebSocket, message):
    outmsg = "="*100+"\n"+message
    print(outmsg)
    msg_size = len(message.encode("ascii"))
    with open("tradelog.log","a") as file:
        file.write(f"{outmsg}\n")
        file.write(f"BYTE SIZE: {msg_size}\n")
    print("BYTE SIZE: ", msg_size)

def on_error(ws: websocket.WebSocket, error):
    print(error)

def on_close(ws: websocket.WebSocket):
    print("### closed ###")

def on_open(ws: websocket.WebSocket):
    ws.send('{"type":"subscribe","symbol":"NVDA"}')
    ws.send('{"type":"subscribe","symbol":"KRAKEN:XXMRZEUR"}')
    ws.send('{"type":"subscribe","symbol":"BINANCE:BTCUSDT"}')

if __name__ == "__main__":
    websocket.enableTrace(False)
    ws = websocket.WebSocketApp(f"wss://ws.finnhub.io?token={FINNHUB_TOKEN}",
                              on_message = on_message,
                              on_error = on_error,
                              on_close = on_close)
    ws.on_open = on_open
    ws.run_forever(dispatcher=rel,reconnect=5)
    rel.signal(2, rel.abort)
    rel.dispatch()
