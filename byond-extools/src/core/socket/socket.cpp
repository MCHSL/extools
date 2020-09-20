#include "socket.h"

static bool InitOnce()
{
	static bool done = false;
	if (done) {
		return true;
	}
	// It doesn't really matter if we call this a couple extra times.
	// It also doesn't really matter if we never call WSACleanup.
#ifdef _WIN32
	WSADATA wsaData;
	int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0)
	{
		printf("WSAStartup failed with error: %d\n", iResult);
		return false;
	}
	done = true;
#endif
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
#ifdef _WIN32
		closesocket(raw_socket);
#else
		::close(raw_socket);
#endif
		raw_socket = INVALID_SOCKET;
	}
}

Socket Socket::from_raw(int raw_socket)
{
	Socket result;
	result.raw_socket = raw_socket;
	return result;
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

bool connect_socket(Socket& socket, const char* port, const char* remote, bool yell = false)
{

	struct addrinfo* result = NULL;
	struct addrinfo hints;
	int iResult;

	memset(&hints, 0, sizeof(hints));

	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	// Resolve the server address and port
	iResult = getaddrinfo(remote, port, &hints, &result);
	if (iResult != 0)
	{
		if(yell) Core::Alert("getaddrinfo failed with error: " + std::to_string(iResult));
		return false;
	}

	// Create a SOCKET for connecting to server
	if (!socket.create(result->ai_family, result->ai_socktype, result->ai_protocol))
	{
#ifdef _WIN32
		if(yell) Core::Alert("socket failed with error: " + std::to_string(WSAGetLastError()));
#endif
		freeaddrinfo(result);
		return false;
	}

	// Setup the TCP listening socket
	iResult = ::connect(socket.raw(), result->ai_addr, (int)result->ai_addrlen);
	if (iResult == SOCKET_ERROR)
	{
#ifdef _WIN32
		if(yell) Core::Alert("connect failed with error: " + std::to_string(WSAGetLastError()));
#endif
		freeaddrinfo(result);
		socket.close();
		return false;
	}

	return true;
}

bool listen_on_socket(Socket& socket, const char* port, const char* iface)
{
	struct addrinfo* result = NULL;
	struct addrinfo hints;
	int iResult;

	memset(&hints, 0, sizeof(hints));

	// Initialize Winsock
	if (!InitOnce())
	{
		Core::Alert("Winsock init failed");
		return false;
	}

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
#ifdef _WIN32
		Core::Alert("socket failed with error: " + std::to_string(WSAGetLastError()));
#endif
		freeaddrinfo(result);
		return false;
	}

	// Set SO_REUSEADDR so if the process is killed, the port becomes reusable
	// immediately rather than after a 60-second delay.
	int opt = 1;
	setsockopt(socket.raw(), SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(int));

	// Setup the TCP listening socket
	iResult = bind(socket.raw(), result->ai_addr, (int)result->ai_addrlen);
	if (iResult == SOCKET_ERROR)
	{
#ifdef _WIN32
		Core::Alert("bind failed with error: " + std::to_string(WSAGetLastError()));
#endif
		freeaddrinfo(result);
		socket.close();
		return false;
	}

	freeaddrinfo(result);

	iResult = ::listen(socket.raw(), SOMAXCONN);
	if (iResult == SOCKET_ERROR)
	{
#ifdef _WIN32
		Core::Alert("listen failed with error: " + std::to_string(WSAGetLastError()));
#endif
		socket.close();
		return false;
	}

	return true;
}

// Listening.
bool JsonListener::listen(const char* port, const char* iface)
{
	if (!InitOnce())
	{
		return false;
	}
	return listen_on_socket(socket, port, iface);
}

JsonStream JsonListener::accept()
{
	return JsonStream(Socket::from_raw(::accept(socket.raw(), NULL, NULL)));
}

bool JsonStream::connect(const char* port, const char* remote)
{
	// Initialize Winsock
	if (!InitOnce())
	{
		return false;
	}

	return connect_socket(socket, port, remote, true);
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
	std::string data = j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
	data.push_back(0);
	while (!data.empty())
	{
		int sent_bytes = ::send(socket.raw(), data.data(), data.size(), 0);
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
		if (size_t zero = recv_buffer.find('\0'); zero != std::string::npos) {
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
		int sent_bytes = ::send(socket.raw(), data.data(), data.size(), 0);
		if (sent_bytes == SOCKET_ERROR)
		{
			return false;
		}
		data.erase(data.begin(), data.begin() + sent_bytes);
	}
	return true;
}

bool TcpListener::listen(const char* port, const char* iface)
{
	if (!InitOnce()) // I feel like both TcpStream and JsonStream should inherit from some BaseStream that implements connecting and listening
	{
		Core::Alert("InitOnce failed!");
		return false;
	}
	return listen_on_socket(socket, port, iface);
}

TcpStream TcpListener::accept()
{
	return TcpStream(Socket::from_raw(::accept(socket.raw(), NULL, NULL)));
}