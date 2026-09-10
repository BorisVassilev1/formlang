#pragma once

#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <string>

namespace fl {

template <class T>
class RangeFromPair {
	T range;

   public:
	template <class U>
	RangeFromPair(U &&range) : range(std::forward<U>(range)) {}

	auto begin() { return range.first; }
	auto end() { return range.second; }
};

template <class T>
RangeFromPair(T &&range) -> RangeFromPair<T>;

#define JOB(name, ...)                     \
	static int _job_##name = []() -> int { \
		__VA_ARGS__;                       \
		return 0;                          \
	}();

#define BENCH(x, n, s)                                                                                    \
	{                                                                                                     \
		auto t1 = std::chrono::high_resolution_clock::now();                                              \
		for (int i = 0; i < n; ++i)                                                                       \
			x;                                                                                            \
		auto t2	  = std::chrono::high_resolution_clock::now();                                            \
		auto diff = t2 - t1;                                                                              \
		std::cout << s << std::chrono::duration_cast<std::chrono::microseconds>(diff / n).count() << "μs" \
				  << std::endl;                                                                           \
	}

#define STR(x) #x

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

}	  // namespace fl
