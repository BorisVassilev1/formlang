#pragma once

#include <cstddef>
#include <ostream>
#include "concepts.hpp"
#include "formatting.hpp"

namespace fl {
/**
 * @brief A simple Symbol class
 *
 */
template <class Symbol>
class State {
	size_t val;

   public:
	constexpr State(size_t val) : val(val) {}
	constexpr State() : val(0) {};

	constexpr operator size_t() const { return val; }
};

template <class Symbol>
std::ostream &operator<<(std::ostream &out, State<Symbol> s) {
	if (s >= Symbol::size) {
		out << "f" << Symbol(s - Symbol::size);
	} else out << size_t(s);
	return out;
}
}	  // namespace fl

template <fl::symbol Symbol>
struct std::formatter<fl::State<Symbol>> : fl::ostream_formatter {};
