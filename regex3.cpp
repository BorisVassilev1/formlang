#include <format>
#include <iostream>
#include "utils.h"
#include "FST.hpp"
#include "SSFT.hpp"
#include "ExpandedFST.hpp"
#include "ambiguity.hpp"
#include "functionality.hpp"
#include "letter.hpp"

using namespace std::string_literals;

auto ones =
	"<'I','1'> + <'II','2'> + <'III','3'> + <'IV','4'> + <'V','5'> + <'VI','6'> + <'VII','7'> + <'VIII','8'> + <'IX','9'> "s;
auto ones0 = ones + "+ <'','0'>";

auto tens =
	"<'X','1'> + <'XX','2'> + <'XXX','3'> + <'XL','4'> + <'L','5'> + <'LX','6'> + <'LXX','7'> + <'LXXX','8'> + <'XC','9'> "s;
auto tens0 = tens + "+ <'','0'>";

auto hundreds =
	"<'C','1'> + <'CC','2'> + <'CCC','3'> + <'CD','4'> + <'D','5'> + <'DC','6'> + <'DCC','7'> + <'DCCC','8'> + <'CM','9'> "s;
auto hundreds0 = hundreds + " + <'','0'>";

auto thousands =
	"<'M','1'> + <'MM','2'> + <'MMM','3'>  + <'MF','4'>  + <'F','5'>  + <'FM','6'>  + <'FMM','7'>  + <'FT','9'>"s;
auto thousands0 = thousands + "+ <'','0'>";

auto tthousands = "<'T','1'> + <'TT','2'> + <'TTT','3'>"s;

auto N1_99	= std::format("({}) + (({}).({}))", ones, tens, ones0);
auto N00_99 = std::format("({}) . ({})", tens0, ones0);

auto N1_999	  = std::format("({}) + (({}).({}))", N1_99, hundreds, N00_99);
auto N000_999 = std::format("({}) . ({})", hundreds0, N00_99);

auto N1_9999	= std::format("({}) + (({}).({}))", N1_999, thousands, N000_999);
auto N0000_9999 = std::format("({}) . ({})", thousands0, N000_999);

auto N1_39999 = std::format("({}) + (({}).({}))", N1_9999, tthousands, N0000_9999);

auto R = std::format("({})!", N1_39999);

auto B = std::format("({}).(<' ', ''>.({}))*", N1_39999, N1_39999);

auto S =
	std::format("((<'T','1'>.<' ',''>)*.({})) + ((<'T','1'>.<' ',' '>)*.(({}).({})))", N1_9999, tthousands, N0000_9999);

auto S1 = std::format(
	" ( ( (<'T','1'>+<'M','1'>).<' ',''>)* . ({}) ) + ( ( (<'T','1'>+<'M','1'>).<' ',' '>)* .  (({}) . ({})) ) ",
	N1_9999, tthousands, N0000_9999);

auto S2 = std::format(
	" ( ( (<'T','1'>+<'M','2'>).<' ',''>)* . ({}) ) + ( ( (<'T','1'>+<'M','2'>).<' ',' '>)* .  (({}) . ({})) ) ",
	N1_9999, tthousands, N0000_9999);

int main() {
	using namespace fl;
	std::cout << "regex: " << S << std::endl;

	auto regex = rgx::parseRegex(S);
	auto fst   = (FST<Letter>)makeFSA_BerriSethi<Letter>(*regex);
	fst		   = trimFSA<Letter>(std::move(fst));

	std::cout << "FSA has " << fst.N << " states and " << fst.transitions.size() << " transitions." << std::endl;

	bool infAmb = testInfiniteAmbiguity(fst);
	// drawFSA(fst);
	std::cout << "infinite ambiguity: " << infAmb << std::endl;
	if (infAmb) {
		std::cerr << "The FSA is infinitely ambiguous!" << std::endl;
		return 1;
	}

	auto realtime = realtimeFST(std::move(fst));
	std::cout << "realtime FST has " << realtime.N << " states and " << realtime.transitions.size() << " transitions."
			  << std::endl;
	// drawFSA(realtime);

	std::cout << "testing functionality..." << std::endl;
	bool func = isFunctional(realtime);
	std::cout << "functionality: " << func << std::endl;
	if (!func) {
		std::cerr << "The FST is not functional!" << std::endl;
		return 1;
	}

	bool bvar = testBoundedVariation(realtime);
	std::cout << "bounded variation: " << bvar << std::endl;
	if (!bvar) {
		std::cerr << "The FST does not satisfy bounded variation!" << std::endl;
		return 1;
	}

	try {
		std::cout << "converting to SSFT..." << std::endl;
		auto ssfst = SSFT<fl::Letter>(std::move(realtime));

		std::cout << "SSFT has " << ssfst.N << " states and " << ssfst.transitions.size() << " transitions."
				  << std::endl;

		std::string input;
		std::getline(std::cin, input);
		auto [result, b] = ssfst.f(toSymbol<Letter>(input));
		if (b) {
			std::cout << "output len: " << result.size() << std::endl;
			std::cout << "Input accepted: " << result << std::endl;
		} else {
			std::cout << "Input rejected." << std::endl;
		}
	} catch (const std::exception &e) { std::cerr << "Error: " << e.what() << std::endl; }

	return 0;
}
