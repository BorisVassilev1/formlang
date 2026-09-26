#pragma once

#include "CartesianMonoid.hpp"
#include "SymbolMonoid.hpp"
#include "pipes.hpp"
#include "transducer_concepts.hpp"
#include "utils.h"

namespace fl {

template <symbol Symbol, std::size_t alphabetSize = Symbol::size, monoid M = InterningMonoid<Symbol>>
class TotalSSFST {
   public:
	constexpr static bool		 deterministic = true;
	constexpr static bool		 sorted_arcs   = true;
	constexpr static std::size_t alphabet_size = alphabetSize;

	using State		   = unsigned int;
	using InputMonoid  = SymbolMonoid<Symbol>;
	using OutputMonoid = M;
	using Monoid	   = CartesianMonoid<SymbolMonoid<Symbol>, M>;

	using OutValue = typename OutputMonoid::Value;

	struct Trans {
		OutValue outputID;
		State	 next;
		bool	 operator==(const Trans &other) const  = default;
		bool	 operator!=(const Trans &other) const  = default;
		bool	 operator<=>(const Trans &other) const = default;
	};
	using Map	   = std::vector<std::array<Trans, alphabetSize>>;
	using Letter_t = Symbol;

   protected:
	unsigned int N = 0;

	Monoid monoid;
	/// transitions[from][letter] = (outputID, to)
	Map transitions;
	/// the Ψ-function, output[state] = outputID
	std::vector<OutValue> output;
	OutValue			  initialOut = OutputMonoid::identity;

   public:
	TotalSSFST() = default;

	TotalSSFST(const TotalSSFST &) = default;
	TotalSSFST(TotalSSFST &&)	   = default;

	TotalSSFST &operator=(const TotalSSFST &) = default;
	TotalSSFST &operator=(TotalSSFST &&)	  = default;

	[[clang::always_inline]] inline constexpr std::size_t size() const { return N; }
	[[clang::always_inline]] inline constexpr std::size_t cacheCount() const {
		return get<1>(monoid).totalWordCount();		// this is not guaranteed to work
	}
	[[clang::always_inline]] inline constexpr std::size_t cacheSize() const {
		return get<1>(monoid).poolByteCount();	   // this is not guaranteed to work
	}

	//[[clang::always_inline]] inline std::pair<std::span<const Symbol>, bool> step(State &state, Symbol letter) const {
	//	auto &[outputID, next] = transitions[state][size_t(letter)];
	//	if (next == -1u) return std::make_pair(std::span<const Symbol>{}, false);
	//	state = next;
	//	return std::make_pair(words[outputID], true);
	//}

	//[[clang::always_inline]] inline bool steps(State &state, std::span<const Symbol> input,
	//										   std::vector<Symbol> &output) const {
	//	for (Symbol letter : input) {
	//		auto [out, ok] = step(state, letter);
	//		if (!ok) return false;
	//		output.insert(output.end(), out.begin(), out.end());
	//	}
	//	return true;
	//}

	[[clang::always_inline]] inline auto				 Psi(State state) const { return output[state]; }
	[[clang::always_inline]] inline std::array<State, 1> Initial() const { return {0}; }
	[[clang::always_inline]] inline bool				 IsInitial(State state) const { return state == 0; }

	[[clang::always_inline]] inline auto InitialOutput() const { return initialOut; }

	[[clang::always_inline]] inline bool		IsFinal(State) const { return true; }
	[[clang::always_inline]] inline auto		Final() const { return std::ranges::iota_view(0u, N); }
	[[clang::always_inline]] inline std::size_t Size() const { return N; }
	[[clang::always_inline]] inline auto		Transitions(State state) const {
		return std::views::enumerate(transitions[state]) | std::views::transform([](const auto &p) {
				   const auto &[letter, transition] = p;
				   return std::make_tuple(typename Monoid::Value(letter, transition.outputID), transition.next);
			   });
	}
	[[clang::always_inline]] inline auto Transition(State state, Symbol letter) const {
		return transitions[state][size_t(letter)];
	}

	[[clang::always_inline]] inline auto Transitions() const {
		return std::views::iota(0u, N * alphabetSize) | std::views::transform([this](std::size_t i) {
				   State  s						= i / alphabetSize;
				   Symbol l						= Symbol(i % alphabetSize);
				   const auto &[outputID, next] = transitions[s][size_t(l)];
				   return std::tuple(s, typename Monoid::Value{l, outputID}, next);
			   });
	}
	const Monoid &GetMonoid() const { return monoid; }

	/// this can return some kind of weird range, but let's be reasonable
	void f(range_of<Symbol> auto input, std::vector<OutValue> &outputWord) const {
		State state = 0;
		outputWord.clear();
		outputWord.push_back(initialOut);
		for (const auto &letter : input) {
			const auto &[outputID, next] = transitions[state][size_t(letter)];
			state						 = next;
			outputWord.push_back(outputID);
		}
		outputWord.push_back(output[state]);
	}

	/// The same as the other overload, but in some sense
	///	f(input) | get<1>(monoid).gen()
	void f(range_of<Symbol> auto input, std::vector<Symbol> &outputWord) const
		requires(!std::same_as<Symbol, OutValue>)	  // would clash
	{
		State state = 0;
		outputWord.clear();
		//
		const auto &initial_out = get<1>(monoid).gen(initialOut);
		outputWord.insert(outputWord.end(), initial_out.begin(), initial_out.end());

		for (const auto &letter : input) {
			const auto &[outputID, next] = transitions[state][size_t(letter)];
			state						 = next;
			const auto &out				 = get<1>(monoid).gen(outputID);
			outputWord.insert(outputWord.end(), out.begin(), out.end());
		}
		const auto &final_out = get<1>(monoid).gen(output[state]);
		outputWord.insert(outputWord.end(), final_out.begin(), final_out.end());
	}

	std::vector<Symbol> f(std::span<const Symbol> input) const {
		std::vector<Symbol> outputWord;
		f(input, outputWord);
		return outputWord;
	}

	const TotalSSFST &serialize(std::ostream &out) const {
		out.write(reinterpret_cast<const char *>(&N), sizeof(N));
		monoid.serialize(out);
		out.write(reinterpret_cast<const char *>(transitions.data()), transitions.size() * sizeof(transitions[0]));
		out.write(reinterpret_cast<const char *>(output.data()), output.size() * sizeof(output[0]));
		return *this;
	}

	TotalSSFST(std::istream &in) {
		in.read(reinterpret_cast<char *>(&N), sizeof(N));
		monoid = Monoid(in);
		transitions.resize(N);
		output.resize(N);
		in.read(reinterpret_cast<char *>(transitions.data()), transitions.size() * sizeof(transitions[0]));
		in.read(reinterpret_cast<char *>(output.data()), output.size() * sizeof(output[0]));
	}

	const TotalSSFST &print(std::ostream &out) const {
		out << "digraph ReplaceWithMarkerSSFT {\n";
		out << "  rankdir=LR;\n";
		out << "  node [shape=circle];\n";
		out << "  init [label=\"N=" << N << "\", shape=square];\n";
		out << "  init -> 0;\n";	 // initial state
		for (State s = 0; s < N; ++s) {
			if (!get<1>(monoid).equal(output[s], OutputMonoid::identity)) {
				out << "  " << s << " [shape=doublecircle, label=\"";
				out << print_if_can(get<1>(monoid), output[s]);
				out << "\"];\n";								 // final States with output
			} else out << "  " << s << " [shape=circle];\n";	 // final States

			for (Symbol l = 0; l < Symbol::size; ++l) {
				const auto &[outputID, next] = transitions[s][size_t(l)];
				if (next != -1u) {
					out << "  " << s << " -> " << next << " [label=\"<" << l << ", ";
					out << print_if_can(get<1>(monoid), outputID);
					out << ">\"];\n";
				}
			}
		}
		out << "}\n";
		return *this;
	}
};

template <class Symbol, size_t alphabetSize>
void statFSA(const TotalSSFST<Symbol, alphabetSize> &fsa) {
	std::cout << "Subsequential Transtuder : |Q| = " << fsa.size() << ", |Σ| = " << alphabetSize
			  << ", |Δ| = " << fsa.size() * alphabetSize << ", |strings| = " << fsa.cacheCount()
			  << ", total = " << fsa.cacheSize() << std::endl;
}

/// tests if the subsequential transducer is canonical
template <SSFST Transducer>
bool isCanonical(const Transducer &t) {
	using State	 = typename Transducer::State;
	using Symbol = typename Transducer::Letter_t;

	const auto &monoid = t.GetMonoid();

	std::vector<Symbol> gcp;
	for (State s = 0; s < t.size(); ++s) {
		gcp.clear();
		bool haveCandidate = t.IsFinal(s);
		if (haveCandidate) {
			const auto &out = get<1>(monoid).gen(t.Psi(s));
			gcp.insert(gcp.end(), out.begin(), out.end());
		}

		for (const auto &[letter, to] : t.Transitions(s)) {
			if (haveCandidate && gcp.empty()) break;
			const auto &[a, b] = letter;
			const auto &out	   = get<1>(monoid).gen(b);
			if (!haveCandidate) {
				gcp.insert(gcp.end(), out.begin(), out.end());
				haveCandidate = true;
				continue;
			}
			size_t k = 0;
			while (k < gcp.size() && k < out.size() && size_t(gcp[k]) == size_t(out[k]))
				++k;
			gcp.erase(gcp.begin(), gcp.begin() + k);
		}

		if (haveCandidate && !gcp.empty()) return false;
	}
	return true;
}

}	  // namespace fl

#include "letter.hpp"
static_assert(fl::SSFSTI<fl::TotalSSFST<fl::Letter>>,
			  "TotalSSFT does not satisfy the subsequential transducer concept");
