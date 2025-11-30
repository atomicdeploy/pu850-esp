#pragma once
#include "../ASA0002E.h"

class LoginHandler : public AsyncWebHandler {
public:
	friend class AuthenticationMiddleware;
	friend class AuthorizationMiddleware;

	bool canHandle(AsyncWebServerRequest *request) const override {
		return isLoginRequest(request);
	}

	void handleRequest(AsyncWebServerRequest *request) override {
		return handleLogin(request);
	}

	static inline bool isLoginRequest(AsyncWebServerRequest *request) {
		return request->isHTTP() && request->url() == "/login" && request->method() == HTTP_POST;
	}

	static void handleLogin(AsyncWebServerRequest *request);

	bool isRequestHandlerTrivial() const override final { return false; } // required in order to parse the request body
};

class AuthenticationMiddleware : public AsyncMiddleware {
public:
	AuthenticationMiddleware() {}
	virtual ~AuthenticationMiddleware() {}

	friend class LoginHandler;
	friend class AuthorizationMiddleware;

	void run(AsyncWebServerRequest *request, ArMiddlewareNext next) override {
		collectCreds(request);

		checkSession(request);

		next();
        }

	void collectCreds(AsyncWebServerRequest *request);

	int checkSession(AsyncWebServerRequest *request) const;

	static void storeAttribute(AsyncWebServerRequest *request, const char *key, const String value) {
		if (value.isEmpty()) return;

		request->setAttribute(key, value.c_str());
	}
};

class AuthorizationMiddleware : public AsyncMiddleware {
public:
	AuthorizationMiddleware() {}
	virtual ~AuthorizationMiddleware() {}

	friend class LoginHandler;
	friend class AuthenticationMiddleware;

	void run(AsyncWebServerRequest *request, ArMiddlewareNext next) override {

		next();
        }

	bool requiresAuthz(AsyncWebServerRequest *request);

	// static bool hasAccess(int sessionId);

	void handleUnauthorised(AsyncWebServerRequest *request);
};
