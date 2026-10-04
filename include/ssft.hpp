#pragma once

#include <ranges>
#include <map>
#include <chrono>
#include <format>
#include <iostream>
#include <stack>
#include <array>
#include <optional>
#include <cassert>
#include <cstdint>
#include <algorithm>

#include "debug_macros.hpp"


#include "expanded_fst.hpp"
#include "interning_monoid.hpp"
#include "concepts.hpp"
#include "transducer_concepts.hpp"
#include "dbg.hpp"
#include "datastructures.hpp"
#include "cartesian_monoid.hpp"
#include "symbol_monoid.hpp"
#include "utils.hpp"
#include "pipes.hpp"

namespace fl {
template <symbol Symbol>
class OutputFSA;

// Subsequential Finite-State Transducer (SSFST)
template <symbol Symbol, free_monoid M = InterningMonoid<Symbol>>
class SparseSSFST {
   public:
	using State = unsigned int;

	using Monoid = CartesianMonoid<SymbolMonoid<Symbol>, M>;

	constexpr static bool deterministic = true;
	constexpr static bool sorted_arcs	= true;

   private:
	using OutputMonoid = M;
	using OutValue	   = OutputMonoid::Value;
	using OutSymbol	   = OutputMonoid::Symbol;
	using Map =
		unordered_map<std::tuple<State, Symbol>, std::pair<OutValue, State>, cartesian_hash<hash<State>, hash<Symbol>>>;

	Monoid						   monoid;
	Map							   transitions;
	unordered_set<State>		   qFinals;
	unsigned int				   N = 0;
	unordered_map<State, OutValue> output;

   public:
	SparseSSFST() : transitions(0) {}

	//////////////// builder interface //////////////////////

	State NewState() { return N++; }

	void AddTransition(State from, Monoid::Value label, State to) {
		const auto &[letter, outputID] = label;
		transitions[{from, letter}]	   = {outputID, to};
	}

	void AddInitial(State state) { assert(state == 0); }	/// only state 0 is initial
	void AddFinal(State state) { qFinals.insert(state); }
	void SetPsi(State state, OutputMonoid::Value v) { output[state] = std::move(v); }

	const auto &GetMonoid() const { return monoid; }

	[[clang::always_inline]] inline std::size_t			 Size() const { return N; }
	[[clang::always_inline]] inline std::array<State, 1> Initial() const { return {0}; }
	[[clang::always_inline]] inline bool				 IsInitial(State s) const { return s == 0; }
	[[clang::always_inline]] inline const auto			&Final() const { return qFinals; }
	[[clang::always_inline]] inline bool				 IsFinal(State s) const { return qFinals.contains(s); }

	[[clang::always_inline]] inline OutputMonoid::Value Psi(State s) const { return output.at(s); }

	[[clang::always_inline]] inline std::optional<std::tuple<OutValue, State>> Transition(
		State s, typename get_input_t<Monoid>::Symbol l) const {
		auto it = transitions.find({s, l});
		if (it == transitions.end()) return std::nullopt;
		return it->second;
	}

	[[clang::always_inline]] inline auto Transitions() const {
		return std::views::transform(transitions, [](const auto &p) {
			const auto &[from, value]  = p;
			const auto &[s, l]		   = from;
			const auto &[outputID, to] = value;
			return std::tuple(s, typename Monoid::Value{l, outputID}, to);
		});
	}

	auto f(const std::vector<OutSymbol> &input) const {
		std::vector<OutSymbol> output;
		State				   current = 0;		// initial state
		for (const auto &letter : input) {
			auto it = transitions.find({current, letter});
			if (it == transitions.end()) return std::pair{output, false};
			const auto &[outputID, next] = it->second;
			const auto &out				 = get<1>(monoid).gen(outputID);
			output.insert(output.end(), out.begin(), out.end());
			current = next;
		}
		if (qFinals.contains(current)) {
			auto word = get<1>(monoid).gen(this->output.at(current));
			output.insert(output.end(), word.begin(), word.end());
		} else return std::pair{output, false};		// not in final state
		return std::pair{output, true};				// in final state
	}

	void print(std::ostream &out) const {
		out << "digraph SparseSSFST {\n";
		out << "  rankdir=LR;\n";
		out << "  node [shape=circle];\n";
		out << "  init [label=\"N=" << N << "\", shape=square];\n";
		out << "  init -> 0;\n";	 // initial state
		for (const auto &q : qFinals) {
			if (qFinals.contains(q)) {
				out << "  " << q << " [shape=doublecircle, label=\"";
				out << print_if_can(get<1>(monoid), output.at(q));
				out << "\"];\n";									   // final States with output
			} else out << "  " << q << " [shape=doublecircle];\n";	   // final States
		}
		for (const auto &[from, value] : transitions) {
			const auto &[s, l]		   = from;
			const auto &[outputID, to] = value;
			out << "  " << s << " -> " << to << " [label=\"<" << l << ", ";
			out << print_if_can(get<1>(monoid), outputID);
			out << ">\"];\n";
		}
		out << "}\n";
	}

	template <std::size_t N, std::size_t Transitions>
	struct PackedSparseSSFSTInputOnly {
		std::array<std::tuple<uint8_t, uint16_t, uint8_t>, Transitions> transitions_list;
		std::array<Symbol, N>											output;
		State															first;

		constexpr PackedSparseSSFSTInputOnly() : transitions_list{}, output{'\0'}, first(0) {}

		void write(std::ostream &out) const {
			out << "#include <cstdint>\n";
			out << "constexpr uint64_t packed_ssft[] = {\n";
			const uint64_t *data = reinterpret_cast<const uint64_t *>(this);
			for (std::size_t i = 0; i < sizeof(PackedSparseSSFSTInputOnly) / sizeof(uint64_t); ++i) {
				out << "0x" << std::hex << data[i] << std::dec << "ULL,";
			}
			out << "};\n";
		}
	};

	template <std::size_t N, std::size_t T>
	constexpr PackedSparseSSFSTInputOnly<N, T> packInputOnly() const {
		if (N != this->N) { dbLog(dbg::LOG_ERROR, "SparseSSFST::packInputOnly: N mismatch: ", N, " != ", this->N); }
		if (T != this->transitions.size()) {
			dbLog(dbg::LOG_ERROR, "SparseSSFST::packInputOnly: Transitions mismatch: ", T,
				  " != ", this->transitions.size());
		}
		PackedSparseSSFSTInputOnly<N, T> packed;
		packed.first	  = 0;
		std::size_t index = 0;
		for (const auto &[lhs, rhs] : transitions) {
			const auto &[from, letter]		 = lhs;
			const auto &[out, to]			 = rhs;
			packed.transitions_list[index++] = {from, uint16_t(letter), to};
		}
		for (std::size_t s = 0; s < N; ++s) {
			if (qFinals.contains(State(s))) {
				packed.output[s] = *get<1>(monoid).gen(output.at(State(s))).begin();
			} else {
				packed.output[s] = Symbol::eof;
			}
		}
		return packed;
	}

	template <std::size_t N, std::size_t T>
	static auto loadPackedInputOnly(const PackedSparseSSFSTInputOnly<N, T> &packed) {
		SparseSSFST<Symbol> ssft;
		ssft.N = 0;
		for (const auto &trans : packed.transitions_list) {
			const auto &[from, letter, to]			 = trans;
			ssft.transitions[{from, Symbol(letter)}] = {0, to};
			if (from >= ssft.N) ssft.N = from + 1;
			if (to >= ssft.N) ssft.N = to + 1;
		}
		for (std::size_t s = 0; s < N; ++s) {
			if (packed.output[s] != Symbol::eof) {
				ssft.qFinals.insert(State(s));
				ssft.output[State(s)] = ssft.words.addWord(std::array<Symbol, 1>{packed.output[s]});
			}
		}
		return ssft;
	}

	void printInfo(std::ostream &out) const {
		out << std::format("SparseSSFST has {} states and {} transitions.\n", N, transitions.size());
		out << std::format("Average transitions per state: {:.2f}\n", static_cast<double>(transitions.size()) / N);
		out << std::format("Number of final states: {}\n", qFinals.size());
		if constexpr (requires { get<1>(monoid).totalWordCount(); })
			out << std::format("Words stored: {}\n", get<1>(monoid).totalWordCount());
		if constexpr (requires { get<1>(monoid).poolByteCount(); })
			out << std::format("Words cache size: {}\n", get<1>(monoid).poolByteCount());
	}

	friend class OutputFSA<Symbol>;
};

/// Subsequentialization of a real-time FST by subset construction: each state of the result is a set of
/// (input state, pending output delay) pairs. Throws if the output delays are unbounded.
template <SSFST_builder TOut, FST_with_arcs T>
	requires(free_monoid<get_input_t<T>> && std::same_as<typename get_input_t<T>::Value, typename get_input_t<T>::Symbol>)
TOut subsequentializeFST(const T &fst, bool resolveNonFunctionality = false) {
	using InState	   = typename T::State;
	using Symbol	   = typename get_input_t<T>::Symbol;
	using OutputMonoid = get_output_t<TOut>;
	using OutValue	   = typename OutputMonoid::Value;
	using State		   = typename TOut::State;
	using Value		   = typename TOut::Monoid::Value;
	using BigState	   = std::vector<std::tuple<InState, OutValue>>;

	static_assert(ordered_monoid<OutputMonoid>, "Output monoid must be comparable for sorting");

	TOut ssft;
	const auto &outMonoid = get<1>(ssft.GetMonoid());
	const auto &inMonoid  = get<1>(fst.GetMonoid());

	std::size_t C		 = inMonoid.C();
	auto		MAX_DELAY = C * fst.Size() * fst.Size();	 // C * |Q|^2
	std::size_t curr_max  = 0;

	std::vector<std::reference_wrapper<const BigState>> states;
	std::map<BigState, State>		stateMap;
	std::stack<State>									queue;

	const State init = ssft.NewState();
	if constexpr (requires { fst.f_eps; }) {
		if (!fst.f_eps.empty()) ssft.SetPsi(init, outMonoid.own(inMonoid, *fst.f_eps.begin()));
	}

	BigState initial;
	for (const auto &q : fst.Initial()) {
		initial.push_back({q, OutputMonoid::identity});
		if (fst.IsFinal(q)) ssft.AddFinal(init);
	}
	std::sort(initial.begin(), initial.end());
	auto [it, _] = stateMap.insert({std::move(initial), init});
	states.emplace_back(it->first);
	queue.push(init);

	using namespace std::chrono_literals;
	SlowDown sd(100ms);
	uint64_t processedStates = 0;

	struct Pending {
		Symbol		symbol;
		OutValue	output;
		std::size_t next;	 // index into nextStates
	};
	std::vector<BigState>						nextStates;
	std::vector<Pending>						pending;
	fl::unordered_map<Symbol, std::size_t>		pendingIndex;

	std::cout << std::endl;
	while (!queue.empty()) {
		State current = queue.top();
		queue.pop();
		const BigState &currentState = states[current];
		processedStates += currentState.size();

		for (const auto &[q, delay] : currentState) {
			for (const auto &[val, next] : fst.Transitions(q)) {
				const auto &[s, w]	  = val;
				auto		currentOutput = outMonoid.own(inMonoid, w);
				auto		wordToDelay	  = outMonoid.mul(delay, currentOutput);

				auto it = pendingIndex.find(s);
				if (it == pendingIndex.end()) {
					pendingIndex.emplace(s, pending.size());
					pending.push_back({s, wordToDelay, nextStates.size()});
					nextStates.emplace_back().emplace_back(next, wordToDelay);
				} else {
					Pending &p = pending[it->second];
					p.output   = outMonoid.gcp(wordToDelay, p.output);
					nextStates[p.next].emplace_back(next, wordToDelay);
				}
			}
		}

		for (const auto &p : pending) {
			for (auto &[_, delay] : nextStates[p.next]) {
				delay	 = outMonoid.invMul(p.output, delay);
				curr_max = std::max(curr_max, outMonoid.size(delay));
				if (outMonoid.size(delay) > MAX_DELAY)
					throw std::runtime_error("Delay too long, bounded variation not satisfied");
			}
		}

		std::vector<State> stateRemap(nextStates.size());
		for (std::size_t i = 0; i < nextStates.size(); ++i) {
			BigState &nextState = nextStates[i];
			std::sort(nextState.begin(), nextState.end());
			nextState.erase(std::unique(nextState.begin(), nextState.end()),
							nextState.end());

			auto found = stateMap.find(nextState);
			if (found != stateMap.end()) {
				stateRemap[i] = found->second;
				continue;
			}

			State newIndex = ssft.NewState();
			stateRemap[i]  = newIndex;

			std::optional<OutValue> finalOut;
			std::optional<InState>	bestOutToKeep;
			for (const auto &[q, delay] : nextState) {
				if (!fst.IsFinal(q)) continue;

				if (finalOut && !outMonoid.equal(delay, *finalOut)) {
					if (!resolveNonFunctionality)
						throw std::runtime_error(
							std::format("Non-functionality detected at state {} between outputs {} and {}", newIndex,
										print_if_can(outMonoid, delay), print_if_can(outMonoid, *finalOut)));

					assert(bestOutToKeep);
					bool bestHasFuture = !std::ranges::empty(fst.Transitions(*bestOutToKeep));
					bool currHasFuture = !std::ranges::empty(fst.Transitions(q));
					std::cout << "conflict at state " << newIndex << " between outputs "
							  << print_if_can(outMonoid, *finalOut) << " and " << print_if_can(outMonoid, delay)
							  << std::endl;

					if (bestHasFuture && currHasFuture)
						throw std::runtime_error("Failed to resolve non-functionality, both outputs have perspective");
					else if (currHasFuture) continue;	 // do not write
				}
				finalOut	  = delay;
				bestOutToKeep = q;
			}
			if (finalOut) {
				ssft.AddFinal(newIndex);
				ssft.SetPsi(newIndex, *finalOut);
			}

			auto [inserted_it, isInserted] = stateMap.insert({std::move(nextState), newIndex});
			assert(isInserted);
			states.emplace_back(inserted_it->first);
			queue.push(newIndex);
		}

		for (const auto &p : pending) ssft.AddTransition(current, Value{p.symbol, p.output}, stateRemap[p.next]);

		sd.do_thing([&]() {
			std::cout << "\rCurrent max delay: " << curr_max << " Current states count: " << states.size()
					  << " Upper bound: " << MAX_DELAY
					  << " Mean states in SparseSSFST state: " << (double)processedStates / (double)states.size()
					  << std::flush;
		});

		nextStates.clear();
		pending.clear();
		pendingIndex.clear();
	}
	std::cout << std::endl;
	return ssft;
}

template <class Symbol>
void statFSA(const SparseSSFST<Symbol> &fsa) {
	fsa.printInfo(std::cout);
}

}	  // namespace fl
