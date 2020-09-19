#include "opcodes.h"

enum class Bytecode : uint32_t
{
    UNK = 0xFFFFFFFF,
#define I(NUMBER, NAME, DIS) \
    NAME = NUMBER,
#include "opcodes_table.inl"
#undef I
};
