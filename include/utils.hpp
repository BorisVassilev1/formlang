#pragma once

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

#include "pipes.hpp"
#include "formatting.hpp"

namespace fl {

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

std::string getString(std::istream &os);

std::string gen_random_string(const int len);

std::string gen_random_string(const int min, const int max);

class SlowDown {
	std::chrono::high_resolution_clock::duration delay = std::chrono::milliseconds(100);

   public:
	std::chrono::high_resolution_clock::time_point last;

   public:
	SlowDown(std::chrono::high_resolution_clock::duration delay = std::chrono::milliseconds(100));

	void do_thing(const std::function<void(void)> &f);
};

class SlowDown2 {
	time_t delay;

   public:
	time_t last;

   public:
	SlowDown2(time_t delay);

	void do_thing(const std::function<void(void)> &f);
};

class SlowDown3 {
	uint64_t delay;

   public:
	uint64_t last;

   public:
	SlowDown3(auto delay = std::chrono::milliseconds(100)) : delay(delay.count() * 4000000), last(0) {}

	void do_thing(const std::function<void(void)> &f);
};

class Timer {
	std::chrono::high_resolution_clock::time_point start;
	std::ostream								  *out;
	std::string									   name;

   public:
	Timer(const std::string_view &name, std::ostream &out = std::cerr);
	Timer(std::nullptr_t);

	~Timer();

	std::chrono::high_resolution_clock::duration elapsed() const;

	long elapsed_min() const;
	long elapsed_s() const;
	long elapsed_ms() const;
	long elapsed_us() const;
	long elapsed_ns() const;
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

void print_escaped_char(std::ostream &out, unsigned char c);

namespace detail {
void print_exception(std::ostream &out, const std::exception &e, int level = 0);
}	  // namespace detail

std::ostream &operator<<(std::ostream &out, const std::exception &e);

}	  // namespace fl

static_assert(
	std::ranges::view<fl::RangeFromPair<decltype(std::declval<std::unordered_map<int, int>>().equal_range(0))>>,
	"RangeFromPair should be a view");
