#include <utils.h>
#include <dpda.h>
#include <iomanip>

namespace fl {

using bits = std::vector<bool>;

static void print_exception(std::ostream &out, const std::exception &e, int level = 0) {
	std::cerr << std::string(level, ' ') << "exception: " << e.what() << '\n';
	try {
		std::rethrow_if_nested(e);
	} catch (const std::exception &nestedException) { print_exception(out, nestedException, level + 1); } catch (...) {
	}
}

std::ostream &operator<<(std::ostream &out, const std::exception &e) {
	print_exception(out, e);
	return out;
}

std::string getString(std::istream &os) {
	std::stringstream str;
	str << os.rdbuf();
	return str.str();
}

std::string gen_random_string(const int len) {
	static const char alphanum[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz";
	std::string tmp_s;
	tmp_s.reserve(len);

	if (len > 0) { tmp_s += alphanum[rand() % ((sizeof(alphanum) / 2) - 1)]; }
	for (int i = 1; i < len; ++i) {
		tmp_s += alphanum[rand() % (sizeof(alphanum) - 1)];
	}

	return tmp_s;
}

std::string gen_random_string(const int min, const int max) {
	if (min > max) { throw std::invalid_argument("min must be less than or equal to max"); }
	int len = min + rand() % (max - min + 1);
	return gen_random_string(len);
}

void print_escaped_char(std::ostream &out,unsigned char c) {
	if(c > 127) {
		out << "\\x" << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << int(c) << std::dec;
	} else {
		switch (c) {
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
			case '\0': out << "(NULL)"; break;
			default: out << char(c);
		}
	}
}

}	  // namespace fl
