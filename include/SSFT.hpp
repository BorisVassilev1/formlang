#pragma once

#include <ranges>
#include <map>

#include "ExpandedFST.hpp"
#include "InterningMonoid.hpp"
#include "concepts.hpp"
#include "transducer_concepts.hpp"
#include "wordset.hpp"
#include "debug.hpp"
#include "datastructures.hpp"

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

	// We use vector because noone cares about individual states and delays
	using BigState			 = std::vector<std::tuple<State, typename M::Value>>;
	using IntermediateStates = std::vector<std::reference_wrapper<const BigState>>;

	void printIntermediate(const IntermediateStates &states, std::ostream &out) const {
		out << "digraph SparseSSFST {\n";
		out << "  rankdir=LR;\n";
		out << "  node [shape=circle];\n";
		out << "  init [label=\"N=" << states.size() << "\", shape=square];\n";
		out << "  init -> 0;\n";	 // initial state
		for (const auto [i, state] : std::ranges::views::enumerate(states)) {
			out << "  " << i << " [label=\"";
			for (const auto &[q, id] : state.get()) {
				out << "(" << q << ", ";
				out << print_if_can(get<1>(monoid), id);
				out << ")\n ";
			}
			out << "\"";
			if (qFinals.contains(i)) out << ", shape=doublecircle";		// final states
			out << "];\n";
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

	void drawIntermediate(const IntermediateStates &states) const {
		ShellProcess p("dot -Tsvg > a.svg && feh ./a.svg");
		printIntermediate(states, p.in());
		p.in() << std::endl;
		p.in().close();
		p.wait();
		std::cout << getString(p.out()) << std::endl;
		std::cout << getString(p.err()) << std::endl;
	}

   public:
	SparseSSFST() : transitions(0) {}

	// accepts a trimmed ExpandedFST and builds a subsequential finite-state transducer
	// tests for bounded variation
	SparseSSFST(ExpandedFST<Symbol, M> &&fsa, bool resolveNonFunctionality = false) : transitions(0) {
		unsigned int C		   = get<1>(fsa.GetMonoid()).C();
		auto		 MAX_DELAY = C * fsa.N * fsa.N;		// C * |Q|^2
		auto		 curr_max  = 0u;

		const auto &fsa_output = get<1>(fsa.monoid);
		const auto &output	   = get<1>(monoid);

		std::vector<std::reference_wrapper<const BigState>> states;		// states of the SSFST
		std::map<BigState, State> stateMap;								// maps sets of states to index in states vector

		State			  nextState = 0;
		const auto		  newState	= [&nextState]() -> State { return nextState++; };
		std::stack<State> queue;

		if (!fsa.f_eps.empty()) {
			this->output[0] = output.own(fsa_output, *fsa.f_eps.begin());	  // output for the initial state
		}

		BigState initial;
		for (const auto &q : fsa.qFirsts) {
			initial.push_back({q, output.identity});
			if (fsa.qFinals.contains(q)) { qFinals.insert(0); }
		}
		std::sort(initial.begin(), initial.end());
		auto [it, _] = stateMap.insert({std::move(initial), 0});
		states.emplace_back(it->first);		// add the initial state
		queue.push(newState());

		std::cout << std::endl;

		using namespace std::chrono_literals;
		SlowDown sd(100ms);
		uint64_t processedStates = 0;

		// InterningMonoid<Symbol> temporaryWords;		// used for the new state delays
		std::vector<BigState> nextStates;
		std::vector<std::reference_wrapper<typename Map::value_type>>
			currentTransitions;		// transitions from the current state
		while (!queue.empty()) {
			State current = queue.top();
			queue.pop();
			const BigState &currentState = states[current];
			processedStates += currentState.size();

			State nextState		= 0;
			auto  localNewState = [&nextState, &nextStates]() {
				auto &ref = nextStates.emplace_back();
				return std::tuple(std::reference_wrapper{ref}, nextState++);
			};

			// for each (q,w) in the current state
			for (const auto &[q, delay] : currentState) {
				const auto [it1, it2] = fsa.transitions.equal_range(q);
				for (const auto &[_, right] : std::ranges::subrange(it1, it2)) {
					const auto &[val, next] = right;
					const auto &[s, w]		= val;

					auto currentOutput = output.own(fsa_output, w);			   // the output for this transition
					auto wordToDelay   = output.mul(delay, currentOutput);	   // concat

					auto it = transitions.find({current, s});	  // for each transition from 'current' with letter 's'
					if (it == transitions.end()) {
						// create a new transition
						auto [to, to_ind] = localNewState();
						auto [t_it, b]	  = transitions.insert({{current, s}, {wordToDelay, to_ind}});
						currentTransitions.emplace_back(std::ref(*t_it));
						to.get().emplace_back(next, wordToDelay);

					} else {
						auto &[_, rhs]		 = *it;
						auto &[bigOutput, n] = rhs;

						// update the existing transition
						auto  gcp	  = output.gcp(wordToDelay, bigOutput);
						auto &nextBig = nextStates[n];
						bigOutput	  = gcp;
						nextBig.emplace_back(next, wordToDelay);
					}
				}
			}

			for (const auto &ref : currentTransitions) {
				auto &[_, rhs]	   = ref.get();
				auto &[output, to] = rhs;
				auto &nextBig	   = nextStates[to];

				for (auto &[q, delay] : nextBig) {
					delay = get<1>(monoid).invMul(output, delay);
					if (get<1>(monoid).size(delay) > curr_max) { curr_max = get<1>(monoid).size(delay); }
					if (get<1>(monoid).size(delay) > MAX_DELAY) {
						throw std::runtime_error("Delay too long, bounded variation not satisfied");
					}
				}
			}

			std::vector<int> stateRemap(nextStates.size(), -1);
			for (const auto &[i, nextState] : std::views::enumerate(nextStates)) {
				// sort and remove duplicates for uniqueness
				std::sort(nextState.begin(), nextState.end());
				nextState.erase(std::unique(nextState.begin(), nextState.end()), nextState.end());

				// check if the next state is already in the states vector
				auto it = stateMap.find(nextState);
				if (it != stateMap.end()) {
					stateRemap[i] = it->second;
					continue;
				}

				// if not, add it to the states vector and map
				State newIndex = newState();
				stateRemap[i]  = newIndex;

				State bestOutToKeep = -1;
				for (const auto &[q, delay] : nextState) {
					if (fsa.qFinals.contains(q)) {
						qFinals.insert(newIndex);
						auto output = delay;	 // output for this final state is the delay

						// if there is already an output for this state and it is different
						if (this->output.contains(newIndex) && !get<1>(monoid).equal(delay, this->output[newIndex])) {
							if (!resolveNonFunctionality)
								throw std::runtime_error(
									std::format("Non-functionality detected at state {} between outputs {} and {}",
												newIndex, print_if_can(get<1>(monoid), delay),
												print_if_can(get<1>(monoid), this->output[newIndex])));
							else {
								// try to resolve by choosing the output that ends in this state
								assert(bestOutToKeep != -1ull);
								auto [b1, e1] = fsa.transitions.equal_range(bestOutToKeep);
								auto [b2, e2] = fsa.transitions.equal_range(q);

								bool bestHasFuture = b1 != e1;
								bool currHasFuture = b2 != e2;
								std::cout << "conflict at state " << newIndex << " between outputs "
										  << print_if_can(get<1>(monoid), this->output[newIndex]) << " and "
										  << print_if_can(get<1>(monoid), output) << std::endl;

								if (bestHasFuture && currHasFuture) {
									throw std::runtime_error(
										"Failed to resolve non-functionality, both outputs have perspective");
								} else if (currHasFuture) continue;		// do not write
							}
						}
						this->output[newIndex] = output;
						bestOutToKeep		   = q;
					}
				}

				auto [inserted_it, isInserted] = stateMap.insert({std::move(nextState), newIndex});
				assert(isInserted);
				states.emplace_back(inserted_it->first);
				queue.push(newIndex);
			}

			for (const auto &ref : currentTransitions) {
				auto &[_, rhs]		 = ref.get();
				auto &[outputID, to] = rhs;
				if (stateRemap[to] == -1) {
					std::cerr << "Error: state remap failed for state " << to << std::endl;
					continue;
				}
				to = stateRemap[to];
			}

			sd.do_thing([&]() {
				std::cout << "\rCurrent max delay: " << curr_max;
				std::cout << " Current states count: " << states.size() << " Upper bound: " << MAX_DELAY;
				std::cout << " transitions: " << transitions.size() << std::flush;
				std::cout << " Mean states in SparseSSFST state: " << (double)processedStates / (double)states.size()
						  << std::flush;
			});

			// if (curr_max >= 1) {
			// drawIntermediate(states);
			//}

			// clear temporary data to conserve memory allocation
			nextStates.clear();
			currentTransitions.clear();
		}
		this->N = states.size();

		// print in dot format
		if (N < 100) { drawIntermediate(states); }
		std::cout << std::endl;
	}

	const auto &GetMonoid() const { return monoid; }

	[[clang::always_inline]] inline std::size_t			 Size() const { return N; }
	[[clang::always_inline]] inline std::array<State, 1> Initial() const { return {0}; }
	[[clang::always_inline]] inline bool				 IsInitial(State s) const { return s == 0; }
	[[clang::always_inline]] inline const auto			&Final() const { return qFinals; }
	[[clang::always_inline]] inline bool				 IsFinal(State s) const { return qFinals.contains(s); }

	[[clang::always_inline]] inline OutputMonoid::Value Psi(State s) const {
		if (!qFinals.contains(s)) return get<1>(monoid).identity;
		return output.at(s);
	}

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
			out << "#define SparseSSFST_INCLUDED\n";
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

template <class Symbol>
void statFSA(const SparseSSFST<Symbol> &fsa) {
	fsa.printInfo(std::cout);
}

}	  // namespace fl

#include "letter.hpp"
namespace {
using SSFSTType = fl::SparseSSFST<fl::Letter, fl::InterningMonoid<fl::Letter>>;

static_assert(fl::SSFST<SSFSTType>, "SparseSSFST does not satisfy the subsequential transducer concept");
static_assert(fl::SSFST_traversable<SSFSTType>,
			  "SparseSSFST does not satisfy the subsequential transducer traversable concept");
}	  // namespace
