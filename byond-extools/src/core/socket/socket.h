#pragma once

#include "../core.h"

#include <Windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>

#pragma comment (lib, "Ws2_32.lib")

class SocketServer
{
	SOCKET server_socket = INVALID_SOCKET;
	SOCKET client_socket = INVALID_SOCKET; //only supports one client at a time

	bool listen(std::string iface = "127.0.0.1", unsigned short port = 2448);
};