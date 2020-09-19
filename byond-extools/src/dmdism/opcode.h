#pragma once

#include "opcodes.h"


class Opcode
{
public:
	Opcode() : Opcode(Bytecode::UNK) {}
	Opcode(Bytecode opcode) : opcode_(opcode) {}

	Bytecode opcode() const { return opcode_; }
	std::string mnemonic() const { return get_mnemonic(opcode_); }
	std::string info() const { return info_; }
	void set_info(std::string info) { info_ = info; }
	void add_info(std::string info) { info_ += info; }
	std::string tostring() const { return mnemonic() + info_; }

private:
	Bytecode opcode_;
	std::string info_;
};
