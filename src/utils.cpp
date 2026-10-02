#include "utils.hpp"

std::string fl::getString(std::istream &os) {
	std::stringstream str;
	str << os.rdbuf();
	return str.str();
}

std::string fl::gen_random_string(const int len) {
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

std::string fl::gen_random_string(const int min, const int max) {
	if (min > max) { throw std::invalid_argument("min must be less than or equal to max"); }
	int len = min + rand() % (max - min + 1);
	return fl::gen_random_string(len);
}

fl::SlowDown::SlowDown(std::chrono::high_resolution_clock::duration delay) : delay(delay), last() {}

void fl::SlowDown::do_thing(const std::function<void(void)> &f) {
	auto now = std::chrono::high_resolution_clock::now();
	if (now - last > delay) {
		f();
		last = now;
	}
}

fl::SlowDown2::SlowDown2(time_t delay) : delay(delay), last() {}

void fl::SlowDown2::do_thing(const std::function<void(void)> &f) {
	auto now = time(0);
	if (now - last > delay) [[unlikely]] {
		f();
		last = now;
	}
}

void fl::SlowDown3::do_thing(const std::function<void(void)> &f) {
	auto now = __builtin_ia32_rdtsc();
	if (now - last > delay) {
		f();
		last = now;
	}
}

fl::Timer::Timer(const std::string_view &name, std::ostream &out)
	: start(std::chrono::high_resolution_clock::now()), out(&out), name(name) {}
fl::Timer::Timer(std::nullptr_t) : start(std::chrono::high_resolution_clock::now()), out(nullptr) {}

fl::Timer::~Timer() {
	auto end  = std::chrono::high_resolution_clock::now();
	auto diff = end - start;
	if (out)
		(*out) << name << " : " << std::chrono::duration_cast<std::chrono::milliseconds>(diff).count() << " ms"
			   << std::endl;
}

std::chrono::high_resolution_clock::duration fl::Timer::elapsed() const {
	auto end  = std::chrono::high_resolution_clock::now();
	auto diff = end - start;
	return diff;
}

long fl::Timer::elapsed_min() const { return std::chrono::duration_cast<std::chrono::minutes>(elapsed()).count(); }
long fl::Timer::elapsed_s() const { return std::chrono::duration_cast<std::chrono::seconds>(elapsed()).count(); }
long fl::Timer::elapsed_ms() const { return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed()).count(); }
long fl::Timer::elapsed_us() const { return std::chrono::duration_cast<std::chrono::microseconds>(elapsed()).count(); }
long fl::Timer::elapsed_ns() const { return std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed()).count(); }

void fl::print_escaped_char(std::ostream &out, unsigned char c) {
	if (c > 127) {
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

void fl::detail::print_exception(std::ostream &out, const std::exception &e, int level) {
	std::cerr << std::string(level, ' ') << "exception: " << e.what() << '\n';
	try {
		std::rethrow_if_nested(e);
	} catch (const std::exception &nestedException) { print_exception(out, nestedException, level + 1); } catch (...) {
	}
}

std::ostream &fl::operator<<(std::ostream &out, const std::exception &e) {
	fl::detail::print_exception(out, e);
	return out;
}
