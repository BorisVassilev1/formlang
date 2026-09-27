#include "../include/letter.hpp"
#include <iomanip>
#include <ostream>
#include "utils.h"

namespace fl {

void Letter::debug_print(std::ostream &out) const {
	switch (*this) {
		case Letter::eof: out << "(eof)"; break;
		case Letter::eps: out << "ε"; break;
		default: print_escaped_char(out, (unsigned char)val);
	}
}

std::ostream &operator<<(std::ostream &out, Letter l) {
	switch (l) {
		case Letter::eof: out << "(eof)"; break;
		case Letter::eps: out << "ε"; break;
		default: out << (unsigned char)l;
	}
	return out;
}
}	  // namespace fl
