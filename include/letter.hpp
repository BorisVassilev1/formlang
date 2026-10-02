#pragma once

#include <cstddef>
#include <ranges>
#include <ostream>
#include <format>
#include <ranges>

#include "concepts.hpp"
#include "formatting.hpp"
#include "utils.hpp"

namespace fl {
/**
 * @brief A simple Letter class
 *
 */
class Letter {
	unsigned char val = 0;

   public:
	constexpr Letter(char val) : val(val) {}
	constexpr Letter() : val(0) {}
	constexpr Letter(const Letter &other)			 = default;
	constexpr Letter &operator=(const Letter &other) = default;

	constexpr			operator size_t() const { return val; }
	explicit constexpr	operator char() const { return val; }
	static const Letter eps;
	static const Letter eof;
	static const size_t size;

	void debug_print(std::ostream &out) const;

	static auto all() {
		return std::views::iota(0u, size) | std::views::transform([](size_t i) { return Letter((char)i); });
	}
};

constexpr const Letter Letter::eps	= '\xFF';
constexpr const Letter Letter::eof	= '#';
constexpr const size_t Letter::size = 256;

inline void Letter::debug_print(std::ostream &out) const {
	switch (*this) {
		case Letter::eof: out << "(eof)"; break;
		case Letter::eps: out << "ε"; break;
		default: print_escaped_char(out, (unsigned char)val);
	}
}

inline std::ostream &operator<<(std::ostream &out, Letter l) {
	switch (l) {
		case Letter::eof: out << "(eof)"; break;
		case Letter::eps: out << "ε"; break;
		default: out << (unsigned char)l;
	}
	return out;
}

}	  // namespace fl

template <>
struct std::formatter<fl::Letter> : fl::ostream_formatter {};

static_assert(fl::symbol<fl::Letter>, "fl::Letter does not satisfy isLetter concept");
