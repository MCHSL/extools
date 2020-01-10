#include "socket.h"

static bool InitOnce()
{
	static bool done = false;
	if (done) {
		return true;
	}
	// It doesn't really matter if we call this a couple extra times.
	// It also doesn't really matter if we never call WSACleanup.
	WSADATA wsaData;
	int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0)
	{
		printf("WSAStartup failed with error: %d\n", iResult);
		return false;
	}
	done = true;
	return true;
}

// Important ownership stuff to prevent leaks or double-closes.
Socket::Socket(Socket&& other)
	: raw_socket(other.raw_socket)
{
	other.raw_socket = INVALID_SOCKET;
}
Socket& Socket::operator=(Socket&& other)
{
	close();
	raw_socket = other.raw_socket;
	other.raw_socket = INVALID_SOCKET;
	return *this;
}
Socket::~Socket()
{
	close();
}

void Socket::close()
{
	if (raw_socket != INVALID_SOCKET)
	{
		closesocket(raw_socket);
		raw_socket = INVALID_SOCKET;
	}
}

// Socket creation.
bool Socket::create(int family, int socktype, int protocol)
{
	if (!InitOnce())
	{
		return false;
	}
	close();
	raw_socket = socket(family, socktype, protocol);
	return raw_socket != INVALID_SOCKET;
}

bool connect_socket(Socket& socket, const char* port, const char* remote)
{

	struct addrinfo* result = NULL;
	struct addrinfo hints;
	int iResult;

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	// Resolve the server address and port
	iResult = getaddrinfo(remote, port, &hints, &result);
	if (iResult != 0)
	{
		Core::Alert("getaddrinfo failed with error: " + std::to_string(iResult));
		return false;
	}

	// Create a SOCKET for connecting to server
	if (!socket.create(result->ai_family, result->ai_socktype, result->ai_protocol))
	{
		Core::Alert("socket failed with error: " + std::to_string(WSAGetLastError()));
		freeaddrinfo(result);
		return false;
	}

	// Setup the TCP listening socket
	iResult = ::connect(socket.raw(), result->ai_addr, (int)result->ai_addrlen);
	if (iResult == SOCKET_ERROR)
	{
		Core::Alert("connect failed with error: " + std::to_string(WSAGetLastError()));
		freeaddrinfo(result);
		socket.close();
		return false;
	}

	return true;
}

// Listening.
bool JsonListener::listen(const char* port, const char* iface)
{
	struct addrinfo* result = NULL;
	struct addrinfo hints;
	int iResult;

	// Initialize Winsock
	if (!InitOnce())
	{
		return false;
	}

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	// Resolve the server address and port
	iResult = getaddrinfo(iface, port, &hints, &result);
	if (iResult != 0)
	{
		Core::Alert("getaddrinfo failed with error: " + std::to_string(iResult));
		return false;
	}

	// Create a SOCKET for connecting to server
	if (!socket.create(result->ai_family, result->ai_socktype, result->ai_protocol))
	{
		Core::Alert("socket failed with error: " + std::to_string(WSAGetLastError()));
		freeaddrinfo(result);
		return false;
	}

	// Set SO_REUSEADDR so if the process is killed, the port becomes reusable
	// immediately rather than after a 60-second delay.
	int opt = 1;
	setsockopt(socket.raw(), SOL_SOCKET, SO_REUSEADDR, (const char*) &opt, sizeof(int));

	// Setup the TCP listening socket
	iResult = bind(socket.raw(), result->ai_addr, (int)result->ai_addrlen);
	if (iResult == SOCKET_ERROR)
	{
		Core::Alert("bind failed with error: " + std::to_string(WSAGetLastError()));
		freeaddrinfo(result);
		socket.close();
		return false;
	}

	freeaddrinfo(result);

	iResult = ::listen(socket.raw(), SOMAXCONN);
	if (iResult == SOCKET_ERROR)
	{
		Core::Alert("listen failed with error: " + std::to_string(WSAGetLastError()));
		socket.close();
		return false;
	}

	return true;
}

JsonStream JsonListener::accept()
{
	return JsonStream(Socket(::accept(socket.raw(), NULL, NULL)));
}

bool JsonStream::connect(const char* port, const char* remote)
{
	// Initialize Winsock
	if (!InitOnce())
	{
		return false;
	}

	return connect_socket(socket, port, remote);
}

bool JsonStream::send(const char* type, nlohmann::json content)
{
	nlohmann::json j = {
		{"type", type},
		{"content", content},
	};
	return send(j);
}

bool JsonStream::send(nlohmann::json j)
{
	std::string data = j.dump();
	data.push_back(0);
	while (!data.empty())
	{
		int sent_bytes = ::send(socket.raw(), data.c_str(), data.size(), 0);
		if (sent_bytes == SOCKET_ERROR)
		{
			return false;
		}
		data.erase(data.begin(), data.begin() + sent_bytes);
	}
	return true;
}

nlohmann::json JsonStream::recv_message()
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

		int received_bytes = ::recv(socket.raw(), data.data(), data.size(), 0);
		if (received_bytes == 0) {
			return nlohmann::json();
		}

		recv_buffer.append(data.begin(), data.begin() + received_bytes);
	}
}

bool TcpStream::connect(const char* port, const char* remote)
{
	if (!InitOnce())
	{
		return false;
	}

	return connect_socket(socket, port, remote);
}

std::string TcpStream::recv()
{
	std::vector<char> data(1024);
	int received_bytes = ::recv(socket.raw(), data.data(), data.size(), 0);
	if (received_bytes <= 0)
	{
		return "";
	}
	return std::string(data.begin(), data.begin() + received_bytes);
}

bool TcpStream::send(std::string data)
{
	while (!data.empty())
	{
		int sent_bytes = ::send(socket.raw(), data.c_str(), data.size(), 0);
		if (sent_bytes == SOCKET_ERROR)
		{
			return false;
		}
		data.erase(data.begin(), data.begin() + sent_bytes);
	}
	return true;
}