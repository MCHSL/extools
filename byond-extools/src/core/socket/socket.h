#pragma once

#include "../core.h"
#include "../json.hpp"
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#pragma comment (lib, "Ws2_32")
#endif

class SocketServer
{
#ifdef _WIN32
	SOCKET server_socket = INVALID_SOCKET;
	SOCKET client_socket = INVALID_SOCKET; //only supports one client at a time
#endif

	std::string recv_buffer;

public:
	bool listen(std::string iface = "127.0.0.1", unsigned short port = 2448);
	bool accept();
	bool listen_for_client();

	bool sendall(std::string type, nlohmann::json content);
	nlohmann::json recv_message();
};