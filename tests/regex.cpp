#include <cstring>
#include <fstream>
#include <iostream>
#include <regexParser.hpp>
#include <FST.hpp>
#include <ExpandedFST.hpp>
#include <letter.hpp>
#include "SSFT.hpp"
#include "ambiguity.hpp"
#include "functionality.hpp"

using namespace fl;

void generate1M() {
	srand(std::chrono::system_clock::now().time_since_epoch().count());
	std::ofstream out("regex_1M.txt");
	for (int i = 0; i < 1000; ++i) {
		out << rgx::generateRegexString(1000) << std::endl;
		if (i != 999) {
			out << " + ";
		} else {
			out << std::endl;
		}
	}
}

int main(int argc, char **argv) {
	rgx::RegexParser parser;

	if (argc == 2 && std::string(argv[1]) == "--generate") {
		generate1M();
		std::cout << "Generated regex_1M.txt" << std::endl;
		return 0;
	}

	std::stringstream buffer;
	std::string		  fileName = "test_file_regex.txt";
	if (argc == 2) fileName = argv[1];

	std::ifstream file(fileName);
	if (!file) {
		std::cout << "error: " << strerror(errno) << std::endl;
		return 1;
	}
	std::cout << "parsing file: " << fileName << std::endl;
	buffer << file.rdbuf();

	std::string text = buffer.str();

	BENCH(rgx::tokenize(text), 100, "BENCH tokenize : ");
	auto tokens = rgx::tokenize(text);
	if (tokens.size() < 1000) { std::cout << "tokens: " << tokens << std::endl; }

	BENCH({ rgx::parseRegex(text); }, 10, "BENCH parsing regex: ");
	auto reg = rgx::parseRegex(text);
	std::cout << reg->size() << " tokens in regex." << std::endl;
	if (tokens.size() < 1000) {
		std::cout << "regex: " << std::endl;
		reg->print(std::cout);
		std::cout << std::endl;
	}
	// generate1M();
	//

	FST<Letter> fst;
	// BENCH(fst = makeFSA_BerriSethi<Letter>(*reg), 1, "BENCH makeFSA: ");
	BENCH(fst = makeFSA_BerriSethi<Letter>(*reg), 1, "BENCH makeFSA Thompson: ");
	std::cout << "FSA has " << fst.N << " states and " << fst.transitions.size() << " transitions and "
			  << fst.words.size() << " words." << std::endl;

	if (tokens.size() < 1000) { drawFSA(fst); }

	bool infAmbiguity;
	BENCH(infAmbiguity = testInfiniteAmbiguity(fst), 100, "BENCH testInfiniteAmbiguity: ");
	std::cout << "Testing infinite ambiguity: " << testInfiniteAmbiguity(fst) << std::endl;

	BENCH(fst = trimFSA<Letter>(std::move(fst));, 1, "BENCH trimFSA: ");
	BENCH(fst = removeEpsilonFST<Letter>(std::move(fst));, 1, "BENCH removeEpsilonFST: ");
	if (tokens.size() < 1000) drawFSA(fst);
	std::cout << "FSA has " << fst.N << " states and " << fst.transitions.size() << " transitions and "
			  << fst.words.size() << " words after removing epsilons." << std::endl;

	BENCH(fst = trimFSA<Letter>(std::move(fst));, 1, "BENCH trimFSA again: ");
	if (tokens.size() < 1000) drawFSA(fst);
	std::cout << "FSA has " << fst.N << " states and " << fst.transitions.size() << " transitions and "
			  << fst.words.size() << " words after trimming." << std::endl;

	ExpandedFST<Letter> efst;
	BENCH(efst = expandFST<Letter>(std::move(fst));, 1, "BENCH expandFST: ");
	if (tokens.size() < 1000) drawFSA(efst);
	std::cout << "Expanded FSA has " << efst.N << " states and " << efst.transitions.size() << " transitions and "
			  << efst.words.size() << " words." << std::endl;

	if (!infAmbiguity) {
		BENCH(efst = removeUpperEpsilonFST<Letter>(std::move(efst));, 1, "BENCH realtimeFST: ");
		if (tokens.size() < 1000) drawFSA(efst);
		std::cout << "Real-time FSA has " << efst.N << " states and " << efst.transitions.size() << " transitions and "
				  << efst.words.size() << " words." << std::endl;

		BENCH(efst = trimFSA<Letter>(std::move(efst));, 1, "BENCH trimFSA again: ");
		if (tokens.size() < 1000) drawFSA(efst);
		std::cout << "Trimmed FSA has " << efst.N << " states and " << efst.transitions.size() << " transitions and "
				  << efst.words.size() << " words." << std::endl;
		bool isFunc = isFunctional(efst);
		std::cout << "isFunctional: " << isFunc << std::endl;

		if (!isFunc) {
			std::cout << "The FSA is not functional!" << std::endl;
			return 1;
		}
		try {
			std::cout << "converting to SSFT..." << std::endl;
			auto ssfst = SparseSSFST<Letter>(std::move(efst));
			drawFSA(ssfst);

			statFSA(ssfst);

			std::string input;
			std::cin >> input;
			auto [result, b] = ssfst.f(toSymbol<Letter>(input));
			if (b) {
				std::cout << "Input accepted: " << result << std::endl;
			} else {
				std::cout << "Input rejected." << std::endl;
			}
		} catch (const std::exception &e) { std::cerr << "Error: " << e.what() << std::endl; }
	}
}
