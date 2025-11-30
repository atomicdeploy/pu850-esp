#include "../ASA0002E.h"

// const static C8* headerKeys[] PROGMEM = {"User-Agent", "Authorization", "Cookie"};
// size_t headerKeysSize = count_of(headerKeys);
// server->collectHeaders(headerKeys, headerKeysSize);


AsyncMiddlewareFunction complexAuth([](AsyncWebServerRequest* request, ArMiddlewareNext next) {
	if (!request->authenticate("user", "password")) {
		return request->requestAuthentication();
	}
	request->setAttribute("user", "User");
	request->setAttribute("role", "staff");

	next();

	request->getResponse()->addHeader("X-Rate-Limit", "200");
});

AsyncAuthorizationMiddleware authz([](AsyncWebServerRequest* request) { return request->getAttribute("role") == "staff"; });

server->on("/auth", HTTP_GET, [&](AsyncWebServerRequest *request) {
	if (!request->hasHeader(F("Authorization")) && !request->hasHeader(F("Cookie")))
	{
		request->send(401, "text/plain", "Unauthorized");
		return;
	}

	String authToken = "";

	if (!token.reserve(TOKEN_LENGTH))
	{
		request->send(503, "text/plain", "memory full");
		return false;
	}

	if (authToken.isEmpty())
	{
		request->send(400, "text/plain", "Missing token");
		return;
	}

	// Check if the session token is present in the request attributes
	if (!request->hasAttribute("token")) return;

	if (!isValid)
	{
		request->send(400, "text/plain", "Bad token format");
		return;
	}

	token_bytes = {0};

	request->send(200, "text/plain", convertToHexString(token_bytes, sizeof(token_bytes)) + "\n" + String(result));
});

server->on("/pass", HTTP_POST, [&](AsyncWebServerRequest *request) {
	const String pass = request->hasArg(F("pass")) ? request->arg(F("pass")) : "";

	const U8 result = CheckAccessPass_PU(pass.c_str());

	switch (result)
	{
		case result_Fail:		request->send(500, "text/plain", "Login Failed");	return;
		case result_Not_Found:		request->send(404, "text/plain", "User Not Found");	return;
		case result_Unauthorised: 	request->send(403, "text/plain", "Forbidden");		return;
	}

	request->send(200, "text/plain", String(result) + "\n" + String(E_AccessLevel));
});

bool doLogin(AsyncWebServerRequest *request) {

	// Generate a new session token
	const String token = generateToken();

	// Store the session token in the sessions list
	const bool success = addSession(token.c_str(), 123, request->client()->remoteIP());

	if (success) {
		// Send the session token in the response cookies
		request->addCookie("TOKEN", token.c_str(), 0, "/", "", false, false);

		result = "Login Successful";
	} else {
		result = "Cannot create session";
	}


	if (password.isEmpty())
	{
		AsyncWebServerResponse *response = request->beginResponse(401, "text/plain", "Unauthorized");
		response->addHeader("WWW-Authenticate", "Basic realm=\"Login to " + String(WiFi.hostname()) + "\"");
		request->send(response);
		return;
	}

	const String token = getRandomMD5();

	if (token.isEmpty())
	{
		request->send(400, "text/plain", "Failure in creating token");
		return;
	}

	char token_bytes[TOKEN_LENGTH / 2];

	// memset(token_bytes, Null_, sizeof(token_bytes));

	bool isValid = token.length() == TOKEN_LENGTH && parseHexString(token, token_bytes);

	if (!isValid)
	{
		request->send(400, "text/plain", "Bad token generated");
		return;
	}

	const U8 accessLevel = 123;

	const bool result = addSession(token_bytes, accessLevel, request->client()->remoteIP());

	if (!result)
	{
		request->send(403, "text/plain", "Cannot create session");
		return;
	}

	/*
	// Check if the username and password are valid
	if (username == "admin" && password == "admin") {
		request->send(200, "text/plain", "Login Successful");
		return true;
	}

	request->send(403, "text/plain", "Forbidden");
	*/

	request->send(200, "text/plain", String("username=") + username + String("\npassword=") + String(password) + "\n" + String(result) + "\n" + token);
}

void AuthenticationMiddleware::run(AsyncWebServerRequest *request, ArMiddlewareNext next) {
	if (AuthenticationHandler::isLoginRequest(request)) {
		return false;
	}

	const bool isAuthenticated = checkSession(request);

	if (!isAuthenticated && requiresAuth(request)) {
		handleUnauthorised(request);

		return;
	}

	next();
}

bool AuthenticationMiddleware::requiresAuth(AsyncWebServerRequest *request) {
	// List of URLs to compare against
	const static std::vector<String> urls = {F("/beep")};

	// Check if the request URL matches any of the URLs in the list
	for (const auto& url : urls) {
		if (request->url().equalsIgnoreCase(url)) {
			return true;
		}
	}

	return false;
}

String AuthenticationMiddleware::getAuthToken(AsyncWebServerRequest *request) {
	return "";
}

bool AuthenticationMiddleware::hasAccess(int sessionId) {
	// Check if the session ID is valid
	if (sessionId == -1 || sessionId == UnknownLevel_ || sessionId == NotAccessLevel_)
		return false;

	// Check if the session ID has the required access level
	if (sessions[sessionId].accessLevel >= UnDefinedNum_)
		return false;

	return true;
}

void AuthenticationMiddleware::handleUnauthorised(AsyncWebServerRequest *request) {
	AsyncResponseStream *response = request->beginResponseStream("text/html");
	// request->send(200, "text/plain", "Hello from custom handler!");
	response->setCode(401);
	response->print("<!DOCTYPE html><html><head><title>Captive Portal</title></head><body>");
	response->print("<p>This is out captive portal front page.</p>");
	response->printf("<p>You were trying to reach: http://%s%s</p>", request->host().c_str(), request->url().c_str());
	response->printf("<p>Try opening <a href='http://%s'>this link</a> instead</p>", WiFi.softAPIP().toString().c_str());
	response->print("</body></html>");
	request->send(response);
}
