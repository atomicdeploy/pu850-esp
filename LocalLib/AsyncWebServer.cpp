#include "../ASA0002E.h"

#include "AsyncWebServer.h"

inline void handleWebSocketMessage(AsyncWebSocket *server, AsyncWebSocketClient *client, void *arg, uint8_t *data, size_t len)
{
	AwsFrameInfo *info = (AwsFrameInfo*)arg;

	// the whole message is in a single frame and we got all of its data
	if (info->final && info->index == 0 && info->len == len)
	{
		if (info->opcode == WS_TEXT) {
			data[len] = 0;
			// os_printf("%s\n", (char*)data);
		} else {
			// for (size_t i = 0; i < info->len; i++)
			// {
			// 	os_printf("%02x ", data[i]);
			// }
			// os_printf("\n");
		}
	}

	// message is comprised of multiple frames or the frame is split into multiple packets
	else
	{
		if (info->index == 0)
		{
			// if(info->num == 0)
			// 	os_printf("ws[%s][%u] %s-message start\n", server->url(), client->id(), (info->message_opcode == WS_TEXT)?"text":"binary");
			// os_printf("ws[%s][%u] frame[%u] start[%llu]\n", server->url(), client->id(), info->num, info->len);
		}

		// os_printf("ws[%s][%u] frame[%u] %s[%llu - %llu]: ", server->url(), client->id(), info->num, (info->message_opcode == WS_TEXT)?"text":"binary", info->index, info->index + len);

		if (info->message_opcode == WS_TEXT) {
			data[len] = 0;
			// os_printf("%s\n", (char*)data);
		} else {
			// for (size_t i = 0; i < len; i++)
			// {
			// 	os_printf("%02x ", data[i]);
			// }
			// os_printf("\n");
		}

		if ((info->index + len) == info->len)
		{
			// os_printf("ws[%s][%u] frame[%u] end[%llu]\n", server->url(), client->id(), info->num, info->len);
			if (info->final)
			{
				// os_printf("ws[%s][%u] %s-message end\n", server->url(), client->id(), (info->message_opcode == WS_TEXT)?"text":"binary");
			}
		}
	}

	onWsReceivedCommand(client, reinterpret_cast<const char*>(data));
}

inline void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
	switch (type)
	{
		case WS_EVT_CONNECT:
			os_printf("WebSocket client #%u connected from IP: %s\n", client->id(), client->remoteIP().toString().c_str());
			client->ping();
			break;

		case WS_EVT_DISCONNECT:
			os_printf("WebSocket client #%u disconnected\n", client->id());
			break;

		case WS_EVT_DATA:
			handleWebSocketMessage(server, client, arg, data, len);
			break;

		case WS_EVT_PING:
			os_printf("WebSocket [%s] client #%u ping(%u): %s\n", server->url(), client->id(), len, (len)?(char*)data:"");
			break;

		case WS_EVT_PONG:
			os_printf("WebSocket [%s] client #%u pong(%u): %s\n", server->url(), client->id(), len, (len)?(char*)data:"");
			break;

		case WS_EVT_ERROR:
			os_printf("WebSocket [%s] client #%u error(%u): %s\n", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
			break;
	}
}

AsyncMiddlewareFunction wsMiddleware([](AsyncWebServerRequest* request, ArMiddlewareNext next) {
	if (ws.count() >= MAX_WS_CLIENTS) {
		// too many clients - answer back immediately and stop processing next middlewares and handler
		request->send(503, "text/plain", "device is busy");
	} else {
		// process next middleware and at the end the handler
		next();
	}
});

void initWebSocket()
{
	ws.onEvent(onWebSocketEvent);
	server->addHandler(&ws).addMiddleware(&wsMiddleware);
}

/*
String processor(const String& var)
{
	if (var == "HELLO_FROM_TEMPLATE")
	{
		return F("Hello world!");
	}

	return String();
}
*/
