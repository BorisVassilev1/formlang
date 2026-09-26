#include "../include/letter.hpp"
#include <ostream>

namespace fl {
std::ostream &operator<<(std::ostream &out, Letter l) {
	//if(l < 0) return out << (int)l;
	return out << int(l);
	switch (l) {
		case Letter::eof: out << "(eof)"; break;
		case Letter::eps: out << "ε"; break;
		case '\\': out << "\\\\"; break;
		case ' ': out << "(WS)"; break;
		case '\n': out << "(NL)"; break;
		case '\t': out << "(TAB)"; break;
		case '\r': out << "(CR)"; break;
		case '\"': out << "\\\""; break;
		case '\v': out << "(VTAB)"; break;
		case '\f': out << "(FF)"; break;
		case '\a': out << "(BELL)"; break;
		case '\b': out << "(BS)"; break;
		default: out << char(l);
	}
	return out;
}
}	  // namespace fl
