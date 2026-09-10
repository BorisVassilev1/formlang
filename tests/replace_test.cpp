#include <iostream>
#include "ReplaceWithMarkerSSFT.hpp"
#include "token.h"

class MySymbol {
   public:
	char value;

	constexpr MySymbol() : value(0) {}
	constexpr MySymbol(char c) : value(c) {}
	constexpr MySymbol(const MySymbol &other)			 = default;
	constexpr MySymbol &operator=(const MySymbol &other) = default;

	constexpr operator size_t() const { return static_cast<size_t>(value); }
	constexpr ~MySymbol() = default;
	constexpr MySymbol operator++() {
		++value;
		return *this;
	}

	constexpr auto operator<=>(std::size_t other) const { return (size_t)value <=> other; }
	constexpr auto operator<=>(const MySymbol &other) const = default;

	constexpr static size_t size = 5;
	const static MySymbol	eps;
	const static MySymbol	eof;
	friend std::ostream	   &operator<<(std::ostream &out, const MySymbol &l) {
		static const char *names[] = {"a", "b", "c", "d", "_", "ε", "eof"};
		if (l.value >= 0 && l.value < 7) return out << names[(int)l.value];
		else return out << "Unknown(" << (int)l.value << ")";
	}

	static MySymbol fromChar(char ch) {
		switch (ch) {
			case 'a': return a;
			case 'b': return b;
			case 'c': return c;
			case 'd': return d;
			case '_': return _;
			default: throw std::invalid_argument("Invalid character for MySymbol");
		}
	}

	const static MySymbol a;
	const static MySymbol b;
	const static MySymbol c;
	const static MySymbol d;
	const static MySymbol _;
};

constexpr inline MySymbol MySymbol::eps = {5};
constexpr inline MySymbol MySymbol::eof = {6};

constexpr inline MySymbol MySymbol::a = 0;
constexpr inline MySymbol MySymbol::b = 1;
constexpr inline MySymbol MySymbol::c = 2;
constexpr inline MySymbol MySymbol::d = 3;
constexpr inline MySymbol MySymbol::_ = 4;

static_assert(fl::symbol<MySymbol>);

std::vector<MySymbol> toMySymbol(std::string_view input) {
	std::vector<MySymbol> output;
	output.reserve(input.size());
	for (const auto &c : input) {
		output.push_back(MySymbol::fromChar(c));
	}
	return output;
}

int main() {
	using ms = MySymbol;
	// fl::ReplaceWithMarkerSSFT<MySymbol> ssft({{{'a'}, {'b'}}, {{'b'}, {'c'}}, {{'a'}, {'c'}}, {{'d'}, {'c'}}}, '_');
	fl::ReplaceWithMarkerSSFT<MySymbol> ssft(
		{
			//
			{{ms::a}, {ms::b, ms::b}},

			{toMySymbol("abd"), toMySymbol("cc")},
			{toMySymbol("abd"), toMySymbol("c")},
			{toMySymbol("abd"), toMySymbol("cb")},
			{toMySymbol("abc"), toMySymbol("c")},
			{toMySymbol("abdd"), toMySymbol("ab")},
			{toMySymbol("abdd"), toMySymbol("cb")},
			{toMySymbol("abc"), toMySymbol("ab")},
			//
		},
		ms::_, true);

	fl::statFSA(ssft);
	fl::drawFSA(ssft);

	std::cout << "Input: ";
	std::vector<ms> input = toMySymbol("_abd_cc_abc_aa_abd_c_abd_abdd_abdd_ab_c_a_bb_a_");
	for (const auto &l : input) {
		std::cout << l;
	}
	std::cout << std::endl;
	std::vector<ms> output = ssft.f(input);
	std::cout << "Output: ";
	for (const auto &l : output) {
		std::cout << l;
	}
	std::cout << std::endl;

	return 0;
}
