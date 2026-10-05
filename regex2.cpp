#include <iostream>
#include <string>

#include "formlang.hpp"
#include "ssft.hpp"

using namespace std::string_literals;

auto ones =
	" <'I','1'> + <'II','2'> + <'III','3'> + <'IV','4'> + <'V','5'> + <'VI','6'> + <'VII','7'> + <'VIII','8'> + <'IX','9'> "s;

auto ones0 = ones + " + <'','0'>";
auto tens =
	" <'X','1'> + <'XX','2'> + <'XXX','3'> + <'XL','4'> + <'L','5'> + <'LX','6'> + <'LXX','7'> + <'LXXX','8'> + <'XC','9'>"s;
auto tens0 = tens + " + <'','0'>";

auto hundreds =
	" <'C','1'> + <'CC','2'> + <'CCC','3'> + <'CD','4'> + <'D','5'> + <'DC','6'> + <'DCC','7'> + <'DCCC','8'> + <'CM','9'>"s;
auto hundreds0 = hundreds + " + <'','0'>";

auto thousands	= " <'M','1'> + <'MM','2'> + <'MMM','3'>"s;
auto thousands1 = "<'M','1'> + <'M', ''>.<'','0'>*.<'M', '2'> + <'MMM', '3'>"s;
auto thousands2 = "<'M','1'> + <'MM', '2'> + <'MM', ''>.<'','0'>*.<'MMM', '3'>"s;

auto N1_99	  = std::format("({}) + (({}). ({}))", ones, tens, ones0);
auto N00_99	  = std::format("({}) . ({})", tens0, ones0);
auto N1_999	  = std::format("({}) + (({}). ({}))", N1_99, hundreds, N00_99);
auto N000_999 = std::format("({}) . ({})", hundreds0, N00_99);
auto N		  = std::format("({}) + (({}). ({}))", N1_999, thousands, N000_999);

auto N1 = std::format("({}) + ({}). ({})", N1_999, thousands1, N000_999);
auto N2 = std::format("({}) + ({}). ({}) + ({}).<'','00'>.({})", N1_999, thousands2, N000_999, thousands1, ones);

auto R = std::format("({})!", N);
auto B = std::format("({}).(<' ', ''>.({}))*", N, N);

auto S = std::format("( ( <'M','1'>.<' ',''>)* . ({}) ) + ( ( <'M','1'>.<' ',' '>)* .  (({}) . ({})) )", N1_999,
					 thousands, N000_999);

auto S1 = std::format(
	"( ( (<'M','1'>+ <'D','1'>).<' ',''>)* . ({}) ) + ( ((<'M','1'> + <'D', '1'>).<' ',' '>)* .  (({}) . ({})) )",
	N1_999, thousands, N000_999);

auto S2 = std::format(
	"( ( (<'M','1'>+ <'D','2'>).<' ',''>)* . ({}) ) + ( ((<'M','1'> + <'D', '2'>).<' ',' '>)* .  (({}) . ({})) )",
	N1_999, thousands, N000_999);

auto K	= "(" + rgx::identity("abcde") + ")*.<'abcabcaab', ':))'>"s;
auto R2 = std::format("({})!", N1);
auto R3 = std::format("({})!", N2);

inline std::optional<fl::SparseSSFST<fl::Letter>> test_regex(const std::string &reg) {
	using namespace fl;
	// std::cout << "regex: " << reg << std::endl;

	auto regex = rgx::parseRegex(reg);
	auto fst   = (StringFST<Letter>)makeFSA_BerriSethi<Letter>(*regex);
	fst		   = trimFSA(std::move(fst));

	statFSA(fst);

	bool infAmb = testInfiniteAmbiguity(fst);
	// drawFSA(fst);
	if (infAmb) {
		std::cerr << "The FSA is infinitely ambiguous!" << std::endl;
		return std::nullopt;
	} else std::cout << "The FSA is not infinitely ambiguous." << std::endl;

	auto realtime = realtimeFST(std::move(fst));
	realtime	  = crochemorePseudoMinimizeFST(pseudoDeterminizeFST(realtime));
	//  drawFSA(realtime);
	statFSA(realtime);

	bool func = isFunctional(realtime);
	if (!func) {
		std::cerr << "The FST is not functional!" << std::endl;
		return std::nullopt;
	} else std::cout << "The FST is functional." << std::endl;

	bool bvar = testBoundedVariation(realtime);
	if (!bvar) {
		std::cerr << "The FST has not bounded variation!" << std::endl;
		return std::nullopt;
	} else std::cout << "The FST has bounded variation." << std::endl;

	try {
		std::cout << "converting to SSFT..." << std::endl;
		auto ssfst = subsequentializeFST<SparseSSFST<Letter>>(realtime);
		// drawFSA(ssfst);
		statFSA(ssfst);

		return ssfst;
	} catch (const std::exception &e) { std::cerr << "Error: " << e.what() << std::endl; }
	return std::nullopt;
}

int main() {
	if constexpr (dbg::enabled) { std::cout << "DEBUG MODE ENABLED" << std::endl; }

	std::cout << "\nN: " << std::endl;
	test_regex(N);
	std::cout << "\nR: " << std::endl;
	test_regex(R);
	std::cout << "\nB: " << std::endl;
	auto res = test_regex(B);
	std::cout << "\nS: " << std::endl;
	test_regex(S);
	std::cout << "\nN1+: " << std::endl;
	test_regex(R2);
	std::cout << "\nN2+: " << std::endl;
	test_regex(R3);

	const auto &ssfst = *res;

	auto [result, b] = ssfst.f(fl::toSymbol<fl::Letter>("M MC MMMI MD MM MCML MMMCMXCIX"));
	if (b) {
		std::cout << "output len: " << result.size() << std::endl;
		std::cout << "Input accepted: " << result << std::endl;
	} else {
		std::cout << "Input rejected." << std::endl;
	}
	return 0;
}
