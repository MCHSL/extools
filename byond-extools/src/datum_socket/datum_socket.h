#pragma once
#include <string>
#include "../core/socket/socket.h"
#include <mutex>

class DatumSocket
{
public:
	DatumSocket();
	DatumSocket(const DatumSocket& other);
	~DatumSocket();

	bool connect(std::string addr, std::string port);
	bool send(std::string data);
	std::string recv(int len);
	void close();
	bool has_data() { std::lock_guard<std::mutex> lk(buffer_lock);  return !buffer.empty() || !open; }
	void set_awaiter(SuspendedProc* proc) { data_awaiter = proc; }
protected:
	void recv_loop();

#ifdef _WIN32
	TcpStream stream;
#endif
	std::string buffer;
	std::mutex buffer_lock;
	SuspendedProc* data_awaiter = nullptr;
	bool open = false;
};

extern std::unordered_map<unsigned int, std::unique_ptr<DatumSocket>> sockets;
extern std::vector<SuspendedProc*> timers_to_reset;