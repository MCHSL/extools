#include "socket.h"

bool SocketServer::listen(std::string iface, unsigned short port)
{
	struct addrinfo* result = NULL;
	struct addrinfo hints;

	// Initialize Winsock
	WSADATA wsaData;
	int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0)
	{
		printf("WSAStartup failed with error: %d\n", iResult);
		return 1;
	}

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	// Resolve the server address and port
	iResult = getaddrinfo(NULL, std::to_string(port).c_str(), &hints, &result);
	if (iResult != 0)
	{
		Core::Alert("getaddrinfo failed with error: " + std::to_string(iResult));
		WSACleanup();
		return false;
	}

	// Create a SOCKET for connecting to server
	server_socket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (server_socket == INVALID_SOCKET)
	{
		Core::Alert("socket failed with error: " + std::to_string(WSAGetLastError()));
		freeaddrinfo(result);
		WSACleanup();
		return false;
	}

	// Setup the TCP listening socket
	iResult = bind(server_socket, result->ai_addr, (int)result->ai_addrlen);
	if (iResult == SOCKET_ERROR)
	{
		Core::Alert("bind failed with error: " + std::to_string(WSAGetLastError()));
		freeaddrinfo(result);
		closesocket(server_socket);
		WSACleanup();
		return false;
	}

	freeaddrinfo(result);

	iResult = ::listen(server_socket, SOMAXCONN);
	if (iResult == SOCKET_ERROR)
	{
		Core::Alert("listen failed with error: " + std::to_string(WSAGetLastError()));
		closesocket(server_socket);
		WSACleanup();
		return false;
	}

	return true;
}