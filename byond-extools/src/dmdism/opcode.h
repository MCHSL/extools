#pragma once

#include "opcodes.h"

class Opcode
{
public:
	Opcode() : Opcode(BYTECODE_UNK) {}
	Opcode(Bytecode opcode) : opcode_(opcode) {}

	Bytecode opcode() const { return opcode_; }
	const char* mnemonic() const { return get_mnemonic(opcode_); }

	std::string info() const { return info_; }
	void add_info(std::string info) { info_ += info; }

	std::string tostring() const { return mnemonic() + info_; }

private:
	Bytecode opcode_;
	std::string info_;
};
