#include <iostream>

#include <FST.hpp>
#include <ExpandedFST.hpp>
#include <ambiguity.hpp>
#include <functionality.hpp>
#include <letter.hpp>
#include <regexParser.hpp>
#include <utils.h>
#include <SSFT.hpp>
#include <concepts.hpp>

using namespace fl;

template <class Letter>
using SSFST_t = SparseSSFST<Letter>;

auto toSymbol(const char *s) { return fl::toSymbol<Letter>(s); }

void test_determinization() {
	ExpandedFST<Letter> fsa;
	fsa.N		= 8;
	fsa.qFirsts = {0, 1};
	fsa.qFinals = {3, 6, 7};
	fsa.addTransition(0, 'a', toSymbol("c"), 2);
	fsa.addTransition(0, 'a', toSymbol("cc"), 3);
	fsa.addTransition(1, 'a', toSymbol("cc"), 3);
	fsa.addTransition(1, 'a', toSymbol("ccc"), 4);
	fsa.addTransition(2, 'b', toSymbol("ccd"), 5);
	fsa.addTransition(3, 'b', toSymbol("cd"), 5);
	fsa.addTransition(4, 'b', toSymbol("dd"), 6);
	fsa.addTransition(5, 'a', toSymbol("d"), 7);
	fsa.addTransition(6, 'a', toSymbol(""), 7);

	drawFSA(fsa);

	bool functional = isFunctional(fsa);
	std::cout << "functional: " << functional << std::endl;
	if (!functional) {
		std::cout << "FSA is not functional." << std::endl;
		return;
	} else {
		std::cout << "FSA is functional." << std::endl;
	}

	SSFST_t<Letter> ssft(std::move(fsa));
	statFSA(ssft);

	std::cout << "draw SSFT" << std::endl;
	drawFSA(ssft);
}

void test_bounded_variation() {
	ExpandedFST<Letter> efst;
	efst.N		 = 4;
	efst.qFirsts = {0};
	efst.qFinals = {0, 1, 3};
	efst.addTransition(0, 'a', toSymbol("a"), 1);
	efst.addTransition(1, 'a', toSymbol("a"), 1);
	efst.addTransition(0, 'a', toSymbol(""), 2);
	efst.addTransition(2, 'a', toSymbol(""), 2);
	efst.addTransition(2, 'b', toSymbol("b"), 3);

	drawFSA(efst);

	bool functional = isFunctional(efst);
	if (!functional) {
		std::cout << "FSA is not functional." << std::endl;
		return;
	} else {
		std::cout << "FSA is functional." << std::endl;
	}

	try {
		SSFST_t<Letter> ssft(std::move(efst));
		statFSA(ssft);

		std::cout << "draw SSFT" << std::endl;
		drawFSA(ssft);
	} catch (const std::exception &e) {
		std::cout << "Error: " << e.what() << std::endl;
		return;
	}
}

void test_replace() {
	auto r = rgx::optionalReplace("<':)','😄'>+<'=D', '🍄'>", "abcd");
	auto t = rgx::parseRegex(r);

	std::cout << "Optional replace: " << t << std::endl;

	BENCH(makeFSA_BerriSethi<Letter>(*t), 100, "BENCH makeFSA Berry-Sethi: ");
	FST<Letter> fst = makeFSA_BerriSethi<Letter>(*t);
	// fst.print(std::cout);

	BENCH(makeFSA_Thompson<Letter>(*t), 100, "BENCH makeFSA Thompson: ");
	// auto fst = makeFSA_Thompson<Letter>(*t);
	// fsa.print(std::cout);
	std::cout << "FSA has " << fst.N << " states and " << fst.transitions.size() << " transitions and "
			  << fst.words.size() << " words." << std::endl;
	// fsa.print(std::cout);
	// drawFSA(fst);

	fst = removeEpsilonFST<Letter>(std::move(fst));
	fst = trimFSA<Letter>(std::move(fst));

	if (!testInfiniteAmbiguity(fst)) {
		std::cout << "FSA is not infinitely ambiguous." << std::endl;
	} else {
		std::cout << "FSA is infinitely ambiguous." << std::endl;
		return;
	}

	auto fsa = expandFST(std::move(fst));
	// drawFSA(fsa);
	fsa = removeUpperEpsilonFST(std::move(fsa));
	// drawFSA(fsa);
	fsa = trimFSA(std::move(fsa));
	std::cout << "FSA has " << fsa.N << " states and " << fsa.transitions.size() << " transitions and "
			  << fsa.words.size() << " words as REALTIME." << std::endl;

	bool functional = isFunctional(fsa);
	std::cout << "functional: " << functional << std::endl;
	if (!functional) {
		std::cout << "FSA is not functional." << std::endl;
		return;
	} else {
		std::cout << "FSA is functional." << std::endl;
	}

	SSFST_t<Letter> ssft(std::move(fsa));
	statFSA(ssft);

	std::cout << "draw SSFT" << std::endl;
	// drawFSA(ssft);

	auto input		 = toSymbol("abbababb");
	auto [output, b] = ssft.f(input);
	std::cout << "Input: " << input << std::endl;
	std::cout << "Output: " << output << std::endl;

	input				= toSymbol("ab:)ab:)aaa:):)a=D=Dbab");
	std::tie(output, b) = ssft.f(input);
	std::cout << "Input: " << input << std::endl;
	std::cout << "Output: " << output << std::endl;
}

int main() {
	// test_determinization();
	// test_bounded_variation();
	test_replace();

	return 0;
}
