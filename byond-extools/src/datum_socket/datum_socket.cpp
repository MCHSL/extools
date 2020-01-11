#include "datum_socket.h"
#include "../core/core.h"
#include "../core/proc_management.h"
#include <thread>

#ifndef min
#define min(a,b)            (((a) < (b)) ? (a) : (b))
#endif

std::unordered_map<unsigned int, std::unique_ptr<DatumSocket>> sockets;
unsigned int recv_sleep_opcode = -1;

DatumSocket::DatumSocket()
{
}

DatumSocket::DatumSocket(const DatumSocket& other)
{
}

DatumSocket::~DatumSocket()
{
	close();
}

bool DatumSocket::connect(std::string addr, std::string port)
{
	stream = TcpStream();
	bool connected = stream.connect(port.c_str(), addr.c_str());
	if (connected)
	{
		std::thread(&DatumSocket::recv_loop, this).detach();
	}
	return connected;
}

bool DatumSocket::send(std::string data)
{
	return stream.send(data);
}

std::string DatumSocket::recv(int len)
{
	std::lock_guard<std::mutex> lk(buffer_lock);
	size_t nom = min(len, buffer.size());
	std::string sub = buffer.substr(0, nom);
	buffer.erase(buffer.begin(), buffer.begin() + nom);
	return sub;
}

void DatumSocket::close()
{
	stream.close();
	open = false;
}

void DatumSocket::recv_loop()
{
	while (open)
	{
		std::string data = stream.recv();
		if (data.empty())
		{
			close();
			return;
		}
		buffer_lock.lock();
		buffer += data;
		buffer_lock.unlock();
		if (data_awaiter)
		{
			data_awaiter->time_to_resume = 0;
			data_awaiter = nullptr;
		}
	}
}

trvh register_socket(unsigned int args_len, Value* args, Value src)
{
	sockets[src.value] = std::make_unique<DatumSocket>();
	return Value::Null();
}

trvh connect_socket(unsigned int args_len, Value* args, Value src)
{
	return sockets[src.value]->connect(args[0], std::to_string((int)args[1].valuef)) ? Value::True() : Value::False();
}

trvh send_socket(unsigned int args_len, Value* args, Value src)
{
	return sockets[src.value]->send(args[0]) ? Value::True() : Value::False();
}

trvh check_socket(unsigned int args_len, Value* args, Value src)
{
	return sockets[src.value]->has_data() ? Value::True() : Value::False();
}

trvh retrieve_socket(unsigned int args_len, Value* args, Value src)
{
	return Value(sockets[src.value]->recv(1024));
}

trvh deregister_socket(unsigned int args_len, Value* args, Value src)
{
	if (sockets.find(src.value) == sockets.end())
	{
		return Value::Null();
	}
	sockets[src.value].reset();
	sockets.erase(src.value);
	return Value::Null();
}

void recv_suspend(ExecutionContext* ctx)
{
	ctx->current_opcode++;
	SuspendedProc* proc = Suspend(ctx, 0);
	proc->time_to_resume = 0x7FFFFF;
	StartTiming(proc);
	int datum_id = ctx->constants->src.value;
	sockets[datum_id]->set_awaiter(proc);
	ctx->current_opcode--;
}

bool enable_sockets()
{
	Core::get_proc("/datum/socket/proc/__register_socket").hook(register_socket);
	Core::get_proc("/datum/socket/proc/__check_has_data").hook(check_socket);
	Core::get_proc("/datum/socket/proc/__retrieve_data").hook(retrieve_socket);
	Core::get_proc("/datum/socket/proc/connect").hook(connect_socket);
	Core::get_proc("/datum/socket/proc/send").hook(send_socket);
	Core::get_proc("/datum/socket/proc/__deregister_socket").hook(deregister_socket);
	recv_sleep_opcode = Core::register_opcode("RECV_SLEEP", recv_suspend);
	Core::get_proc("/datum/socket/proc/__wait_for_data").set_bytecode(new std::vector<std::uint32_t>({ recv_sleep_opcode, 0, 0, 0 }));
	return true;
}

extern "C" EXPORT const char* init_sockets(int a, const char** b)
{
	enable_sockets();
	return "ok";
}