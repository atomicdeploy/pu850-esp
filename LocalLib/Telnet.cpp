#include "Telnet.h"
#include <ESP8266mDNS.h>

#ifndef ShellOnSerial
inline void putchar1(C8 ch)
{
	telnet.print(ch);
}

inline void exitshell()
{
	telnet.disconnectClient(true);
}
#endif

// callback functions for telnet events, such as init connection
void onTelnetConnect(String ip) {
	// disable local echo: FF FB 01, enable local echo: FF FC 01
	telnet.write(0xFF); // IAC
	telnet.write(0xFB); // WILL
	telnet.write(0x01); // ECHO
	
    // Character-at-a-time mode
    telnet.write(IAC); telnet.write(WILL); telnet.write(0x03); // SGA
    telnet.write(IAC); telnet.write(DO);   telnet.write(0x03); // SGA

	telnet.print("\e[H" "\e[2J"); // clear entire screen

	telnet.print(ansi.setFG(ANSI_BRIGHT_GREEN));
	telnet.println("Welcome " + telnet.getIP() + " to " + WiFi.hostname() + "!");
	// telnet.println("Firmware: " + String(ESP.getSketchMD5()));
	telnet.print(ansi.reset());

	#ifdef ShellOnSerial
	telnet.print(ansi.setFG(ANSI_BRIGHT_RED));
	telnet.println("\nShell is on Serial");
	telnet.print(ansi.reset());
	#else
	newPrompt(0);
	#endif
}

void onTelnetDisconnect(String ip) {
	#ifndef ShellOnSerial
	newPrompt(0);
	#endif
}

void onTelnetReconnect(String ip) {
	onTelnetConnect(ip);
}

void onTelnetConnectionAttempt(String ip) { }

void onTelnetInput(String str) {
	#ifdef ShellOnSerial
	return;
	#endif
	
	const U8 len = str.length();

	const char* buff = str.c_str();

	for (U8 i = 0; /*buff[i] != Null_*/ i < len; i++) {
		const C8 ch = buff[i];
		
		TelnetProcessByte(ch);
		// ShellService(ch);
	}
}

void TelnetProcessByte(const C8 ch)
{
	
	switch (telnet_state)
	{
		case STATE_DATA:
			if (ch == IAC) {
				telnet_state = STATE_IAC;
				break;
			}
			// Normal data, forward to shell
			ShellService(ch);
			
			break;

		case STATE_IAC:
			switch (ch) {
				case IAC:
					// Escaped 0xFF (literal 0xFF data)
					ShellService(ch);
					telnet_state = STATE_DATA;
					break;

				case DO:
				case DONT:
				case WILL:
				case WONT:
					telnet_cmd = ch;
					telnet_state = STATE_OPT;
					break;

				default:
					// Simple commands like IAC NOP, ignore ch.
					telnet_state = STATE_DATA;
					break;
			}
			break;

		case STATE_OPT:
			{
				const U8 option = ch;

				// Handle negotiation logic
				switch (option)
				{
					case 0x01: // ECHO
						switch (telnet_cmd)
						{
							case DO:					// Client asks us to ECHO → accept (this is enforced, and the default option, anyway)
							case DONT:
								telnet.write(IAC);
								telnet.write(WILL);
								telnet.write(option);
								break;
								
							case WILL:					// Client says "I WILL ECHO" → agree, let the client handle echoing itself
							case WONT:
								telnet.write(IAC);
								telnet.write(DO);
								telnet.write(option);
								break;
								
						}
						break;
						
					case 0x03: // "Suppress Go-Ahead"
						switch (telnet_cmd)
						{
							case DO:					// Client asks us to "Suppress Go-Ahead" → accept (we are always sending one character-at-a-time)
							case DONT:
								telnet.write(IAC);
								telnet.write(WILL);
								telnet.write(option);
							break;
							
							case WILL:					// Client says "I WILL Suppress Go-Ahead" → agree (this allows receiving one character-at-a-time from the client)
							case WONT:
								telnet.write(IAC);
								telnet.write(DO);
								telnet.write(option);
							break;
						}
						break;
						
					default:
						// Politely refuse unsupported options.
						switch (telnet_cmd) {
							// For every `DO`, respond `WONT`.
							case DO:	telnet.write(IAC); telnet.write(WONT); telnet.write(option); break;
							// For every `WILL`, respond `DONT`.
							case WILL:	telnet.write(IAC); telnet.write(DONT); telnet.write(option); break;
						}
						break;
				}

				telnet_state = STATE_DATA;
			}
			break;
	}
}

bool Telnet_Setup()
{
	telnet.onConnect(onTelnetConnect);
	telnet.onConnectionAttempt(onTelnetConnectionAttempt);
	telnet.onReconnect(onTelnetReconnect);
	telnet.onDisconnect(onTelnetDisconnect);
	telnet.onInputReceived(onTelnetInput);
	telnet.setLineMode(false);

	MDNS.addService("telnet", "tcp", Telnet_Port);

	return telnet.begin(Telnet_Port, false); // Set `false` to prevent checking: WiFi.status() == WL_CONNECTED || WiFi.softAPIP().isSet()
}

void Telnet_End()
{
	telnet.stop();

	MDNS.removeService("telnet");
}

inline void Telnet_Service()
{
	telnet.loop();
}

/*
void Telnet_Setup()
{
	Telnet.begin();
	Telnet.setNoDelay(true);
}

void Telnet_Service()
{

	// check if there are any new clients
	if (Telnet.hasClient())
	{

		U8 i;

		// find free/disconnected spot
		for (i = 0; i < MAX_TELNET_CLIENTS; i++)
		{
			if (!telnetClients[i]) // equivalent to !telnetClients[i].connected()
			{
				// assign the next telnet client to the free/disconnected spot
				telnetClients[i] = Telnet.available();
				break;
			}
		}

		// no free/disconnected spot so reject
		if (i == MAX_TELNET_CLIENTS)
		{
			Telnet.available().println("BUSY!");
			// hints: Telnet.available() is a WiFiClient with short-term scope
			// when out of scope, a WiFiClient will
			// - flush() - all data will be sent
			// - stop() - automatically too
			// Serial.printf("Telnet server is busy with %d active connections\n", MAX_TELNET_CLIENTS);
		}
	}

	// check TCP clients for data
	for (U8 i = 0; i < MAX_TELNET_CLIENTS; i++)
	{
		while (telnetClients[i].available() && Serial.availableForWrite() > 0)
		{
			Serial.write(telnetClients[i].read());
		}
	}

	// determine maximum output size "fair TCP use"
	// client.availableForWrite() returns 0 when !client.connected()
	int maxToTcp = 0;

	for (U16 i = 0; i < MAX_TELNET_CLIENTS; i++)
	{
		if (telnetClients[i])
		{
			int afw = telnetClients[i].availableForWrite();

			if (afw)
			{
				if (!maxToTcp) {
					maxToTcp = afw;
				} else {
					maxToTcp = std::min(maxToTcp, afw);
				}
			} else {
				// warn but ignore congested clients
				// Serial.println("client is congested");
			}
		}
	}

	// check UART for data
	size_t len = std::min(Serial.available(), maxToTcp);
	       len = std::min(len, (size_t)STACK_PROTECTOR);

	if (len)
	{
		U8 sbuf[len];
		U16 serial_got = Serial.readBytes(sbuf, len);

		// push UART data to all connected Telnet clients
		for (size_t i = 0; i < MAX_TELNET_CLIENTS; i++)
		{
			// if client.availableForWrite() was 0 (congested) and increased since then,
			// ensure write space is sufficient:
			if (telnetClients[i].availableForWrite() >= serial_got)
			{
				size_t tcp_sent = telnetClients[i].write(sbuf, serial_got);

				if (tcp_sent != len)
				{
					// Serial.printf("len mismatch: available:%zd serial-read:%zd tcp-write:%zd\n", len, serial_got, tcp_sent);
				}
			}
		}

	}
}
*/
