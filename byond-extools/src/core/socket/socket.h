#pragma once

#include "../core.h"
#include "../json.hpp"
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "Ws2_32")
#endif

const char *const DBG_DEFAULT_PORT = "2448";

class Socket
{
	Socket(const Socket &) = delete;
	Socket &operator=(const Socket &) = delete;

	SOCKET raw_socket = INVALID_SOCKET;

public:
	Socket() {}
	Socket(Socket &&other);
	Socket &operator=(Socket &&other);
	virtual ~Socket();

	explicit Socket(int raw_socket) : raw_socket(raw_socket) {}

	bool create(int family = AF_INET, int socktype = SOCK_STREAM, int protocol = IPPROTO_TCP);
	void close();

	SOCKET raw() { return raw_socket; }
};

class JsonStream
{
	Socket socket;
	std::string recv_buffer;

public:
	JsonStream() {}
	explicit JsonStream(Socket &&socket) : socket(std::move(socket)) {}

	bool connect(const char *port = DBG_DEFAULT_PORT, const char *remote = "127.0.0.1");

	bool send(const char *type, nlohmann::json content);
	bool send(nlohmann::json j);
	nlohmann::json recv_message();
	void close() { socket.close(); }
};

class TcpListener
{
	Socket socket;

public:
	TcpListener() {}
	bool listen(const char *port = DBG_DEFAULT_PORT, const char *iface = "127.0.0.1");
	JsonStream accept();
	void close() { socket.close(); }
};
