#include "Telnet.h"
#include "Shell.h"
#include <ESP8266mDNS.h>

bool Telnet_Initialized = false;
U16 Telnet_Port = 23;
ESPTelnet telnet;

static bool telnetCallbacksConfigured = false;
static bool telnetMdnsPublished = false;
static bool telnetGreetingPending = false;
static U16 telnetActivePort = 0;
static TelnetInputState telnetInputState = TelnetState_Data;
static U8 telnetNegotiationCommand = 0;

static bool TelnetNetworkAvailable()
{
	return
		WiFi.status() == WL_CONNECTED ||
		WiFi.softAPIP().isSet();
}

static void TelnetSendNegotiation(U8 negotiation, U8 option)
{
	const U8 data[] = {TELNET_IAC, negotiation, option};
	telnet.write(data, sizeof(data));
}

static void TelnetNegotiate(U8 negotiation, U8 option)
{
	switch (negotiation) {
		case TELNET_DO:
			if (
				option == TELNET_OPTION_ECHO ||
				option == TELNET_OPTION_SGA
			) {
				TelnetSendNegotiation(TELNET_WILL, option);
			} else {
				TelnetSendNegotiation(TELNET_WONT, option);
			}
			break;

		case TELNET_DONT:
			if (
				option == TELNET_OPTION_ECHO ||
				option == TELNET_OPTION_SGA
			) {
				TelnetSendNegotiation(TELNET_WONT, option);
			}
			break;

		case TELNET_WILL:
			if (option == TELNET_OPTION_SGA)
				TelnetSendNegotiation(TELNET_DO, option);
			else
				TelnetSendNegotiation(TELNET_DONT, option);
			break;

		case TELNET_WONT:
			if (option == TELNET_OPTION_SGA)
				TelnetSendNegotiation(TELNET_DONT, option);
			break;
	}
}

static size_t TelnetShellWrite(
	void *,
	const U8 *data,
	size_t size
)
{
	if (!telnet.isConnected() || data == nullptr || size == 0)
		return 0;

	return telnet.write(data, size);
}

static void TelnetShellClose(void *)
{
	/*
	 * Mark the logical session closed before ESPTelnet invokes its disconnect
	 * callback. This prevents ShellSubmitLine from drawing one last prompt.
	 */
	ShellDisconnect(TelnetShellSession);

	if (telnet.isConnected())
		telnet.disconnectClient(true);
}

void TelnetResetParser()
{
	telnetInputState = TelnetState_Data;
	telnetNegotiationCommand = 0;
}

static void TelnetSendGreeting()
{
	if (!telnetGreetingPending || !telnet.isConnected())
		return;

	telnetGreetingPending = false;

	// Server-side echo and character-at-a-time operation.
	TelnetSendNegotiation(TELNET_WILL, TELNET_OPTION_ECHO);
	TelnetSendNegotiation(TELNET_WILL, TELNET_OPTION_SGA);
	TelnetSendNegotiation(TELNET_DO, TELNET_OPTION_SGA);

	ShellClearScreen(TelnetShellSession);
	String_ToShell(
		TelnetShellSession,
		ansi.setFG(ANSI_BRIGHT_GREEN).c_str()
	);

	String welcome = "Welcome ";
	welcome += telnet.getIP();
	welcome += " to ";
	welcome += FW_PRODUCT_NAME;

	const String hostname = WiFi.hostname();
	if (hostname.length() > 0)
		welcome += " (" + hostname + ")";
	welcome += "!";

	String_NewLine_ToShell(TelnetShellSession, welcome.c_str());
	String_ToShell(TelnetShellSession, ansi.reset().c_str());
	ShellNewPrompt(TelnetShellSession, false);
}

static void onTelnetConnect(String)
{
	TelnetResetParser();
	ShellInitialize(
		TelnetShellSession,
		ShellTransport_Telnet,
		TelnetShellWrite,
		TelnetShellClose
	);

	/*
	 * ESPTelnet invokes onConnect before it marks the socket connected, then
	 * drains all input waiting at that instant. Defer negotiation and visible
	 * output until Telnet_Service regains control after telnet.loop().
	 */
	telnetGreetingPending = true;
}

static void onTelnetDisconnect(String)
{
	telnetGreetingPending = false;
	TelnetResetParser();
	ShellDisconnect(TelnetShellSession);
}

static void onTelnetReconnect(String ip)
{
	onTelnetConnect(ip);
}

static void onTelnetConnectionAttempt(String)
{
}

static void onTelnetInput(String input)
{
	const size_t length = input.length();

	for (size_t i = 0; i < length; ++i)
		TelnetProcessByte(static_cast<U8>(input[i]));
}

void TelnetProcessByte(U8 ch)
{
	switch (telnetInputState) {
		case TelnetState_Data:
			if (ch == TELNET_IAC)
				telnetInputState = TelnetState_IAC;
			else
				ShellService(TelnetShellSession, ch);
			break;

		case TelnetState_IAC:
			switch (ch) {
				case TELNET_IAC:
					ShellService(TelnetShellSession, ch);
					telnetInputState = TelnetState_Data;
					break;

				case TELNET_DO:
				case TELNET_DONT:
				case TELNET_WILL:
				case TELNET_WONT:
					telnetNegotiationCommand = ch;
					telnetInputState = TelnetState_Option;
					break;

				case TELNET_SB:
					telnetInputState =
						TelnetState_Subnegotiation;
					break;

				default:
					// Ignore simple commands such as NOP and GA.
					telnetInputState = TelnetState_Data;
					break;
			}
			break;

		case TelnetState_Option:
			TelnetNegotiate(telnetNegotiationCommand, ch);
			telnetNegotiationCommand = 0;
			telnetInputState = TelnetState_Data;
			break;

		case TelnetState_Subnegotiation:
			if (ch == TELNET_IAC) {
				telnetInputState =
					TelnetState_SubnegotiationIAC;
			}
			break;

		case TelnetState_SubnegotiationIAC:
			if (ch == TELNET_SE)
				telnetInputState = TelnetState_Data;
			else
				telnetInputState = TelnetState_Subnegotiation;
			break;
	}
}

static void TelnetConfigureCallbacks()
{
	if (telnetCallbacksConfigured)
		return;

	telnet.onConnect(onTelnetConnect);
	telnet.onConnectionAttempt(onTelnetConnectionAttempt);
	telnet.onReconnect(onTelnetReconnect);
	telnet.onDisconnect(onTelnetDisconnect);
	telnet.onInputReceived(onTelnetInput);
	telnet.setLineMode(false);
	telnetCallbacksConfigured = true;
}

bool Telnet_Setup()
{
	TelnetConfigureCallbacks();

	if (
		Telnet_Initialized &&
		telnetActivePort == Telnet_Port
	) {
		return true;
	}

	if (Telnet_Initialized)
		Telnet_End();

	if (!TelnetNetworkAvailable())
		return false;

	Telnet_Initialized = telnet.begin(Telnet_Port, false);
	if (!Telnet_Initialized)
		return false;

	telnetActivePort = Telnet_Port;
	MDNS.addService("telnet", "tcp", Telnet_Port);
	telnetMdnsPublished = true;
	return true;
}

void Telnet_End()
{
	const bool wasInitialized = Telnet_Initialized;
	Telnet_Initialized = false;
	telnetActivePort = 0;
	telnetGreetingPending = false;

	if (wasInitialized)
		telnet.stop(true);

	TelnetResetParser();
	ShellDisconnect(TelnetShellSession);

	if (telnetMdnsPublished) {
		MDNS.removeService("telnet");
		telnetMdnsPublished = false;
	}
}

void Telnet_Service()
{
	if (!TelnetNetworkAvailable()) {
		Telnet_End();
		return;
	}

	if (
		!Telnet_Initialized ||
		telnetActivePort != Telnet_Port
	) {
		Telnet_Setup();
		return;
	}

	telnet.loop();
	TelnetSendGreeting();
}
