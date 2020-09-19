#include "instruction.h"

static const char* const get_mnemonic_raw(Bytecode bytecode)
{
    switch (bytecode) {
#define I(NUMBER, NAME, DIS) \
    case Bytecode::NAME: \
        return #NAME;
#include "instruction_table.inl"
#undef I
    }
    return "???";
}

const char* const get_mnemonic(Bytecode bytecode)
{
    switch (bytecode)
    {
        // These string mnemonics differ from their code names.
        case Bytecode::DBG_FILE:
            return "DBG FILE";
        case Bytecode::DBG_LINENO:
            return "DBG LINENO";
        default:
            return get_mnemonic_raw(bytecode);
    }
}
