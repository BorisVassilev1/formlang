module;

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#include <unordered_map>

#include "utils_macros.hpp"

export module formlang:utils;

import :pipes;
import :formatting;

export namespace fl {

template <class T>
class RangeFromPair : public std::ranges::view_interface<RangeFromPair<T>> {
	T range;

   public:
	using iterator		  = decltype(std::declval<T>().first);
	using sentinel		  = decltype(std::declval<T>().second);
	using value_type	  = std::remove_reference_t<decltype(*std::declval<iterator>())>;
	using difference_type = std::ptrdiff_t;
	using reference		  = value_type &;
	using const_reference = const value_type &;

	template <class U>
	RangeFromPair(U &&range) : range(std::forward<U>(range)) {}

	auto begin() { return range.first; }
	auto end() { return range.second; }
};

template <class T>
RangeFromPair(T &&range) -> RangeFromPair<T>;

template <auto N>
struct string_literal {
	constexpr string_literal(const char (&str)[N]) { std::ranges::copy_n(str, N, value); }

	char value[N];

	constexpr operator const char *() const { return value; }
};

inline std::string getString(std::istream &os) {
	std::stringstream str;
	str << os.rdbuf();
	return str.str();
}

inline std::string gen_random_string(const int len) {
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

inline std::string gen_random_string(const int min, const int max) {
	if (min > max) { throw std::invalid_argument("min must be less than or equal to max"); }
	int len = min + rand() % (max - min + 1);
	return gen_random_string(len);
}

class SlowDown {
	std::chrono::high_resolution_clock::duration delay = std::chrono::milliseconds(100);

   public:
	std::chrono::high_resolution_clock::time_point last;

   public:
	SlowDown(std::chrono::high_resolution_clock::duration delay = std::chrono::milliseconds(100))
		: delay(delay), last() {}

	void do_thing(const std::function<void(void)> &f) {
		auto now = std::chrono::high_resolution_clock::now();
		if (now - last > delay) {
			f();
			last = now;
		}
	}
};

class SlowDown2 {
	time_t delay;

   public:
	time_t last;

   public:
	SlowDown2(time_t delay) : delay(delay), last() {}

	void do_thing(const std::function<void(void)> &f) {
		auto now = time(0);
		if (now - last > delay) [[unlikely]] {
			f();
			last = now;
		}
	}
};

class SlowDown3 {
	uint64_t delay;

   public:
	uint64_t last;

   public:
	SlowDown3(auto delay = std::chrono::milliseconds(100)) : delay(delay.count() * 4000000), last(0) {}

	void do_thing(const std::function<void(void)> &f) {
		auto now = __builtin_ia32_rdtsc();
		if (now - last > delay) {
			f();
			last = now;
		}
	}
};

class Timer {
	std::chrono::high_resolution_clock::time_point start;
	std::ostream								  *out;
	std::string									   name;

   public:
	Timer(const std::string_view &name, std::ostream &out = std::cerr)
		: start(std::chrono::high_resolution_clock::now()), out(&out), name(name) {}
	Timer(std::nullptr_t) : start(std::chrono::high_resolution_clock::now()), out(nullptr) {}

	~Timer() {
		auto end  = std::chrono::high_resolution_clock::now();
		auto diff = end - start;
		if (out)
			(*out) << name << " : " << std::chrono::duration_cast<std::chrono::milliseconds>(diff).count() << " ms"
				   << std::endl;
	}

	std::chrono::high_resolution_clock::duration elapsed() const {
		auto end  = std::chrono::high_resolution_clock::now();
		auto diff = end - start;
		return diff;
	}

	auto elapsed_min() const { return std::chrono::duration_cast<std::chrono::minutes>(elapsed()).count(); }
	auto elapsed_s() const { return std::chrono::duration_cast<std::chrono::seconds>(elapsed()).count(); }
	auto elapsed_ms() const { return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed()).count(); }
	auto elapsed_us() const { return std::chrono::duration_cast<std::chrono::microseconds>(elapsed()).count(); }
	auto elapsed_ns() const { return std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed()).count(); }
};

template <class T>
void drawFSA(const T &fsa) {
	ShellProcess p("dot -Tsvg > a.svg && feh ./a.svg");
	fsa.print(p.in());
	p.in() << std::endl;
	p.in().close();
	p.wait();
	std::cout << getString(p.out()) << std::endl;
	std::cout << getString(p.err()) << std::endl;
}

inline void print_escaped_char(std::ostream &out, unsigned char c) {
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

namespace detail {
inline void print_exception(std::ostream &out, const std::exception &e, int level = 0) {
	std::cerr << std::string(level, ' ') << "exception: " << e.what() << '\n';
	try {
		std::rethrow_if_nested(e);
	} catch (const std::exception &nestedException) { print_exception(out, nestedException, level + 1); } catch (...) {
	}
}
}	  // namespace detail

inline std::ostream &operator<<(std::ostream &out, const std::exception &e) {
	detail::print_exception(out, e);
	return out;
}

}	  // namespace fl

static_assert(std::ranges::view<fl::RangeFromPair<decltype(std::declval<std::unordered_map<int, int>>().equal_range(0))>>,
			  "RangeFromPair should be a view");
