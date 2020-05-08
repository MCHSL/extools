#include "datum_socket.h"
#include "../core/core.h"
#include "../core/proc_management.h"
#include <thread>

#ifndef min
#define min(a,b)            (((a) < (b)) ? (a) : (b))
#endif

std::unordered_map<unsigned int, std::unique_ptr<DatumSocket>> sockets;
unsigned int recv_sleep_opcode = -1;
unsigned int accept_sleep_opcode = -1;

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
	if (mode != SocketMode::NONE)
	{
		return false;
	}
	stream = TcpStream();
	bool connected = stream.connect(port.c_str(), addr.c_str());
	if (connected)
	{
		open = true;
		mode = SocketMode::CLIENT;
		std::thread(&DatumSocket::recv_loop, this).detach();
	}
	return connected;
}

void DatumSocket::assign_stream(TcpStream&& ts)
{
	stream = std::move(ts);
	mode = SocketMode::CLIENT;
	open = true;
	std::thread(&DatumSocket::recv_loop, this).detach();
}

bool DatumSocket::listen(std::string addr, std::string port)
{
	if (mode != SocketMode::NONE)
	{
		Core::Alert("Attempting to listen on a socket already in use!");
		return false;
	}
	
	listener = TcpListener();
	bool listening = listener.listen(port.c_str(), addr.c_str());
	if (listening)
	{
		mode = SocketMode::SERVER;
		open = true;
		std::thread(&DatumSocket::accept_loop, this).detach();
	}
	return listening;
}

void DatumSocket::accept_loop()
{
	while (open)
	{
		TcpStream client = listener.accept();
		if (!client.valid())
		{
			break;
		}
		accept_lock.lock();
		accepts.push(std::move(client));
		accept_lock.unlock();
		if (accept_awaiter)
		{
			accept_awaiter->resume();
			accept_awaiter.reset();
		}
	}
}

bool DatumSocket::send(std::string data)
{
	if (mode != SocketMode::CLIENT)
	{
		return false;
	}
	return stream.send(data);
}

std::string DatumSocket::recv(int len)
{
	if (mode != SocketMode::CLIENT)
	{
		return "";
	}
	std::lock_guard<std::mutex> lk(buffer_lock);
	size_t nom = min(len, buffer.size());
	std::string sub = buffer.substr(0, nom);
	buffer.erase(buffer.begin(), buffer.begin() + nom);
	return sub;
}

void DatumSocket::close()
{
	if (mode == SocketMode::CLIENT)
	{
		data_awaiter.reset();
		stream.close();
		buffer = {};
	}
	else if (mode == SocketMode::SERVER)
	{
		accept_awaiter.reset();
		listener.close();
		accepts = {};
	}
	
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
			data_awaiter->resume();
			data_awaiter.reset();
		}
	}
}

trvh register_socket(unsigned int args_len, Value* args, Value src)
{
	sockets.emplace(src.value, std::make_unique<DatumSocket>());
	return Value::Null();
}

trvh connect_socket(unsigned int args_len, Value* args, Value src)
{
	return sockets.at(src.value)->connect(args[0], std::to_string((int)args[1].valuef)) ? Value::True() : Value::False();
}

trvh send_socket(unsigned int args_len, Value* args, Value src)
{
	return sockets.at(src.value)->send(args[0]) ? Value::True() : Value::False();
}

trvh check_socket(unsigned int args_len, Value* args, Value src)
{
	return sockets.at(src.value)->has_data() ? Value::True() : Value::False();
}

trvh retrieve_data(unsigned int args_len, Value* args, Value src)
{
	Value data = Value(sockets.at(src.value)->recv(1024));
	IncRefCount(data.type, data.value);
	return data;
}

trvh deregister_socket(unsigned int args_len, Value* args, Value src)
{
	if (sockets.find(src.value) == sockets.end())
	{
		return Value::Null();
	}
	sockets.at(src.value)->close();
	sockets.erase(src.value);
	return Value::Null();
}

trvh listen_socket(unsigned int args_len, Value* args, Value src)
{
	return sockets.at(src.value)->listen(args[0], args[1]) ? Value::True() : Value::False();
}

trvh check_accept_socket(unsigned int args_len, Value* args, Value src)
{
	return sockets.at(src.value)->can_accept_now() ? Value::True() : Value::False();
}

trvh accept_socket(unsigned int args_len, Value* args, Value src)
{
	TcpStream stream = sockets.at(src.value)->accept();
	Value new_socket = Core::get_proc("/proc/__create_socket").call({});
	std::unique_ptr<DatumSocket>& ds_ptr = sockets.at(new_socket.value);
	ds_ptr->assign_stream(std::move(stream));
	return new_socket;
}

void recv_suspend(ExecutionContext* ctx)
{
	sockets.at(ctx->constants->src.value)->set_data_awaiter(Core::SuspendCurrentProc());
}

void accept_suspend(ExecutionContext* ctx)
{
	sockets.at(ctx->constants->src.value)->set_accept_awaiter(Core::SuspendCurrentProc());
}

bool enable_sockets()
{
	Core::get_proc("/datum/socket/proc/__register_socket").hook(register_socket);
	Core::get_proc("/datum/socket/proc/__check_has_data").hook(check_socket);
	Core::get_proc("/datum/socket/proc/__retrieve_data").hook(retrieve_data);
	Core::get_proc("/datum/socket/proc/connect").hook(connect_socket);
	Core::get_proc("/datum/socket/proc/__listen").hook(listen_socket);
	Core::get_proc("/datum/socket/proc/send").hook(send_socket);
	Core::get_proc("/datum/socket/proc/__deregister_socket").hook(deregister_socket);
	recv_sleep_opcode = Core::register_opcode("RECV_SLEEP", recv_suspend);
	Core::get_proc("/datum/socket/proc/__wait_for_data").set_bytecode({ recv_sleep_opcode, 0, 0, 0 });
	accept_sleep_opcode = Core::register_opcode("RECV_SLEEP", accept_suspend);
	Core::get_proc("/datum/socket/proc/__wait_accept").set_bytecode({ accept_sleep_opcode, 0, 0, 0 });
	Core::get_proc("/datum/socket/proc/__check_can_accept").hook(check_accept_socket);
	Core::get_proc("/datum/socket/proc/__accept").hook(accept_socket);
	return true;
}

void clean_sockets()
{
	sockets.clear();
}

extern "C" EXPORT const char* init_sockets(int a, const char** b)
{
	if (!Core::initialize() || !enable_sockets())
	{
		return Core::FAIL;
	}
	return Core::SUCCESS;
}