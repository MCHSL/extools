#pragma once

#include "opcodes.h"


class Opcode
{
public:
	Opcode() : Opcode(UNK) {}
	Opcode(Bytecode opcode) : opcode_(opcode)
	{
		if (mnemonics.find(opcode) == mnemonics.end())
		{
			mnemonic_ = "UNK";
		}
		else
		{
			mnemonic_ = mnemonics.at(opcode);
		}
		
	}

	Bytecode opcode() const { return opcode_; }
	std::string mnemonic() const { return mnemonic_; }
	std::string info() const { return info_; }
	void set_info(std::string info) { info_ = info; }
	void add_info(std::string info) { info_ += info; }
	std::string tostring() const { return mnemonic_ + info_; }

private:
	Bytecode opcode_;
	std::string mnemonic_;
	std::string info_;
};
