#pragma once

#include "../core.h"
#include "../../third_party/json.hpp"
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment (lib, "Ws2_32")
#else
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netdb.h>  /* Needed for getaddrinfo() and freeaddrinfo() */
#include <unistd.h> /* Needed for close() */
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#endif

const char* const DBG_DEFAULT_PORT = "2448";

class Socket
{
	Socket(const Socket&) = delete;
	Socket& operator=(const Socket&) = delete;

	SOCKET raw_socket = INVALID_SOCKET;
	static Socket from_raw(int raw_socket);
	friend class JsonListener;
	friend class TcpListener;

public:
	Socket() {}
	Socket(Socket&& other);
	Socket& operator=(Socket&& other);
	virtual ~Socket();

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
	explicit JsonStream(Socket&& socket) : socket(std::move(socket)) {}

	bool connect(const char* port = DBG_DEFAULT_PORT, const char* remote = "127.0.0.1");

	bool send(const char* type, nlohmann::json content);
	bool send(nlohmann::json j);
	nlohmann::json recv_message();
	void close() { socket.close(); }
};

class JsonListener
{
	Socket socket;
public:
	JsonListener() {}
	bool listen(const char* port = DBG_DEFAULT_PORT, const char* iface = "127.0.0.1");
	JsonStream accept();
	void close() { socket.close(); }
};

class TcpStream
{
	Socket socket;
public:
	TcpStream() {}
	explicit TcpStream(Socket&& socket) : socket(std::move(socket)) {}

	bool connect(const char* port, const char* remote); //augh, why port first?! damn it spaceman

	bool send(std::string data);
	std::string recv();
	void close() { socket.close(); }
	bool valid() { return socket.raw() != INVALID_SOCKET; }
};

class TcpListener
{
	Socket socket;
public:
	TcpListener() {}
	bool listen(const char* port, const char* iface);
	TcpStream accept();
	void close() { socket.close(); }
	bool valid() { return socket.raw() != INVALID_SOCKET; }
};
