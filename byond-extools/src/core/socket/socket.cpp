#include "socket.h"

#ifndef _WIN32 // TODO: linux
bool SocketServer::listen(std::string iface, unsigned short port)
{
	return false;
}

bool SocketServer::accept()
{
	return false;
}

bool SocketServer::listen_for_client()
{
	return false;
}

bool SocketServer::sendall(std::string type, nlohmann::json content)
{
	return false;
}

nlohmann::json SocketServer::recv_message()
{
	return nlohmann::json();
}
#else
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

bool SocketServer::accept()
{
	client_socket = ::accept(server_socket, NULL, NULL);
	return client_socket != INVALID_SOCKET;
}

bool SocketServer::listen_for_client()
{
	if (server_socket != INVALID_SOCKET)
	{
		return accept();
	}
	return listen() && accept();
}

bool SocketServer::send(std::string type, nlohmann::json content)
{
	nlohmann::json j = {
		{"type", type},
		{"content", content},
	};
	return send(j);
}

bool SocketServer::send(nlohmann::json j)
{
	std::string data = j.dump();
	data.push_back(0);
	while (!data.empty())
	{
		int sent_bytes = ::send(client_socket, data.c_str(), data.size(), 0);
		if (sent_bytes == SOCKET_ERROR)
		{
			return false;
		}
		data.erase(data.begin(), data.begin() + sent_bytes);
	}
	return true;
}

nlohmann::json SocketServer::recv_message()
{
	std::vector<char> data(1024);
	while (true)
	{
		size_t zero = recv_buffer.find('\0');
		if (zero != std::string::npos) {
			nlohmann::json json = nlohmann::json::parse({ recv_buffer.data(), recv_buffer.data() + zero });
			recv_buffer.erase(0, zero + 1);
			return json;
		}

		int received_bytes = ::recv(client_socket, data.data(), data.size(), 0);
		if (received_bytes == 0) {
			return nlohmann::json();
		}

		recv_buffer.append(data.begin(), data.begin() + received_bytes);
	}
}
#endif
