#pragma once
#include <string>
#include "../core/socket/socket.h"
#include <mutex>
#include "../core/core.h"
#include <queue>

enum class SocketMode
{
	NONE,
	CLIENT,
	SERVER,
};

class DatumSocket
{
public:
	DatumSocket();
	DatumSocket(const DatumSocket& other);
	~DatumSocket();

	bool connect(std::string addr, std::string port);
	bool listen(std::string addr, std::string port);
	void assign_stream(TcpStream&& ts);


	bool send(std::string data);
	std::string recv(int len);
	void close();
	bool has_data() { std::lock_guard<std::mutex> lk(buffer_lock);  return !buffer.empty() || !open; }
	void set_data_awaiter(Core::ResumableProc proc) { data_awaiter = proc; }
	void set_accept_awaiter(Core::ResumableProc proc) { accept_awaiter = proc; }
	bool can_accept_now() { return !accepts.empty(); }
	TcpStream accept() { auto tc = std::move(accepts.front()); accepts.pop(); return tc; }

protected:
	void recv_loop();
	void accept_loop();

	TcpListener listener; //kill me but I just want it done
	std::queue<TcpStream> accepts;
	std::mutex accept_lock;
	std::optional<Core::ResumableProc> accept_awaiter = {};


	TcpStream stream;
	std::string buffer;
	std::mutex buffer_lock;
	std::optional<Core::ResumableProc> data_awaiter = {};
	bool open = false;
	SocketMode mode = SocketMode::NONE;
};

void clean_sockets();
extern std::unordered_map<unsigned int, std::unique_ptr<DatumSocket>> sockets;