#pragma once

#include "concepts.hpp"
#include "pipes.hpp"
#include "utils.h"
#include "wordset.hpp"

namespace fl {

template <symbol Symbol, std::size_t alphabetSize = Symbol::size>
class TotalSSFT {
   public:
	using State	 = unsigned int;
	using WordID = UniqueWordSet<Symbol>::WordID;
	struct Transition {
		WordID outputID;
		State  next;
		bool   operator==(const Transition &other) const  = default;
		bool   operator!=(const Transition &other) const  = default;
		bool   operator<=>(const Transition &other) const = default;
	};
	using Map	   = std::vector<std::array<Transition, alphabetSize>>;
	using Letter_t = Symbol;

   protected:
	unsigned int		  N = 0;
	UniqueWordSet<Symbol> words;

	/// transitions[from][letter] = (outputID, to)
	Map transitions;
	/// the Ψ-function, output[state] = outputID
	std::vector<WordID> output;
	WordID				initialOut = 0;

   public:
	TotalSSFT() = default;

	TotalSSFT(const TotalSSFT &) = default;
	TotalSSFT(TotalSSFT &&)		 = default;

	TotalSSFT &operator=(const TotalSSFT &) = default;
	TotalSSFT &operator=(TotalSSFT &&)		= default;

	[[clang::always_inline]] inline constexpr std::size_t size() const { return N; }
	[[clang::always_inline]] inline constexpr std::size_t cacheCount() const { return words.size(); }
	[[clang::always_inline]] inline constexpr std::size_t cacheSize() const { return words.totalLength(); }

	[[clang::always_inline]] inline std::pair<std::span<const Symbol>, bool> step(State &state, Symbol letter) const {
		auto &[outputID, next] = transitions[state][size_t(letter)];
		if (next == -1u) return std::make_pair(std::span<const Symbol>{}, false);
		state = next;
		return std::make_pair(words[outputID], true);
	}

	[[clang::always_inline]] inline bool steps(State &state, std::span<const Symbol> input,
											   std::vector<Symbol> &output) const {
		for (Symbol letter : input) {
			auto [out, ok] = step(state, letter);
			if (!ok) return false;
			output.insert(output.end(), out.begin(), out.end());
		}
		return true;
	}

	[[clang::always_inline]] inline std::span<const Symbol> psi(State state) const {
		return std::span<const Symbol>(words[output[state]]);
	}
	[[clang::always_inline]] inline State initial() const { return 0; }

	[[clang::always_inline]] inline std::span<const Symbol> initialOutput() const { return words[initialOut]; }
	[[clang::always_inline]] inline bool					isFinal(State) const { return true; }

	void f(std::span<const Symbol> input, std::vector<Symbol> &outputWord) const {
		State state = 0;
		outputWord.clear();
		outputWord.insert(outputWord.end(), words[initialOut].begin(), words[initialOut].end());
		for (const auto &letter : input) {
			const auto &[outputID, next] = transitions[state][size_t(letter)];
			state						 = next;
			for (const auto &outLetter : words[outputID])
				outputWord.push_back(outLetter);
		}
		for (const auto &outLetter : words[output[state]])
			outputWord.push_back(outLetter);
	}
	std::vector<Symbol> f(std::span<const Symbol> input) const {
		std::vector<Symbol> outputWord;
		f(input, outputWord);
		return outputWord;
	}

	const TotalSSFT &serialize(std::ostream &out) const {
		out.write(reinterpret_cast<const char *>(&N), sizeof(N));
		words.serialize(out);
		out.write(reinterpret_cast<const char *>(transitions.data()), transitions.size() * sizeof(transitions[0]));
		out.write(reinterpret_cast<const char *>(output.data()), output.size() * sizeof(output[0]));
		return *this;
	}

	TotalSSFT(std::istream &in) {
		in.read(reinterpret_cast<char *>(&N), sizeof(N));
		words = UniqueWordSet<Symbol>(in);
		transitions.resize(N);
		output.resize(N);
		in.read(reinterpret_cast<char *>(transitions.data()), transitions.size() * sizeof(transitions[0]));
		in.read(reinterpret_cast<char *>(output.data()), output.size() * sizeof(output[0]));
	}

	const TotalSSFT &print(std::ostream &out) const {
		out << "digraph ReplaceWithMarkerSSFT {\n";
		out << "  rankdir=LR;\n";
		out << "  node [shape=circle];\n";
		out << "  init [label=\"N=" << N << "\", shape=square];\n";
		out << "  init -> 0;\n";	 // initial state
		for (State s = 0; s < N; ++s) {
			if (output[s] != 0) {
				out << "  " << s << " [shape=doublecircle, label=\"";
				for (const auto &letter : words[output[s]]) {
					out << letter;
				}
				out << "\"];\n";								 // final States with output
			} else out << "  " << s << " [shape=circle];\n";	 // final States

			for (Symbol l = 0; l < Symbol::size; ++l) {
				const auto &[outputID, next] = transitions[s][size_t(l)];
				if (next != -1u) {
					out << "  " << s << " -> " << next << " [label=\"<" << l << ", ";
					for (const auto &letter : words[outputID]) {
						out << letter;
					}
					out << ">\"];\n";
				}
			}
		}
		out << "}\n";
		return *this;
	}
};

template <class Symbol, size_t alphabetSize>
void drawFSA(const TotalSSFT<Symbol, alphabetSize> &fsa) {
	ShellProcess p("dot -Tsvg > a.svg && feh ./a.svg");
	fsa.print(p.in());
	p.in() << std::endl;
	p.in().close();
	p.wait();
	auto out = getString(p.out()), err = getString(p.err());
	if (!out.empty()) std::cout << out << std::endl;
	if (!err.empty()) std::cout << err << std::endl;
}

template <class Symbol, size_t alphabetSize>
void statFSA(const TotalSSFT<Symbol, alphabetSize> &fsa) {
	std::cout << "Subsequential Transtuder : |Q| = " << fsa.size() << ", |Σ| = " << alphabetSize
			  << ", |Δ| = " << fsa.size() * alphabetSize << ", |strings| = " << fsa.cacheCount()
			  << ", total = " << fsa.cacheSize() << std::endl;
}

/// tests if the subsequential transducer is canonical
template <SSFST Transducer>
bool isCanonical(const Transducer &t) {
	using State = typename Transducer::State;
	using Symbol = typename Transducer::Letter_t;

	for (State s = 0; s < t.size(); ++s) {
		std::span<const Symbol> gcp;
		bool					haveCandidate = t.isFinal(s);
		if (haveCandidate) gcp = t.psi(s);

		for (Symbol l = 0; l < Symbol::size; ++l) {
			if (haveCandidate && gcp.empty()) break;
			State next			= s;
			auto [out, ok] = t.step(next, l);
			if (!ok) continue;
			if (!haveCandidate) {
				gcp			  = out;
				haveCandidate = true;
				continue;
			}
			size_t k = 0;
			while (k < gcp.size() && k < out.size() && size_t(gcp[k]) == size_t(out[k])) ++k;
			gcp = gcp.subspan(0, k);
		}

		if (haveCandidate && !gcp.empty()) return false;
	}
	return true;
}

}	  // namespace fl

#include "letter.hpp"
static_assert(fl::SSFSTI<fl::TotalSSFT<fl::Letter>>,
			  "TotalSSFT does not satisfy the subsequential transducer concept");
