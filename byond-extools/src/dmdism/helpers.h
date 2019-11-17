#pragma once

#include <string>
#include <sstream>
#include "../core/core.h"
#include "../core/internal_functions.h"

std::string byond_tostring(int idx);
int intern_string(std::string str);
std::string tohex(int numero);
std::string todec(int numero);