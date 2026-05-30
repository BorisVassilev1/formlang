#pragma once

#include <queue>
#include <ranges>
#include <map>

#include "TFSA.hpp"
#include "functionality.hpp"
#include "wordset.hpp"
#include "debug.hpp"
#include "datastructures.hpp"

namespace fl {
// Subsequential Finite-State Transducer (SSFST)
template <class Letter>
class SSFT {
   public:
	using State	   = unsigned int;
	using StringID = WordSet<Letter>::WordID;
	using Map	   = fl::unordered_map<std::tuple<State, Letter>, std::pair<StringID, State>>;

	WordSet<Letter>					   words;
	Map								   transitions;
	fl::unordered_set<State>		   qFinals;
	unsigned int					   N = 0;
	fl::unordered_map<State, StringID> output;

	SSFT() = default;

	// accepts a trimmed TFSA and builds a subsequential finite-state transducer
	// tests for bounded variation
	SSFT(TFSA<Letter> &&fsa, bool resolveNonFunctionality = false) {
		unsigned int C = 0;
		for (auto w : fsa.words) {
			if (w.size() > C) C = w.size();
		}
		auto MAX_DELAY = C * fsa.N * fsa.N;		// C * |Q|^2
		auto curr_max  = 0u;

		UniqueWordSet<Letter> stateDelays;
		using DelayID = UniqueWordSet<Letter>::WordID;

		// We use vector because noone cares about individual states and delays
		using BigState = std::vector<std::tuple<State, DelayID>>;

		std::vector<std::reference_wrapper<const BigState>> states;		// states of the SSFST
		std::map<BigState, State> stateMap;								// maps sets of states to index in states vector

		State			  nextState = 0;
		const auto		  newState	= [&nextState]() -> State { return nextState++; };
		std::stack<State> queue;

		if (!fsa.f_eps.empty()) {
			auto wordID		= *fsa.f_eps.begin();
			this->output[0] = words.addWord(fsa.words[wordID]);		// output for the initial state
		}

		BigState initial;
		for (const auto &q : fsa.qFirsts) {
			initial.push_back({q, 0});
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

		while (!queue.empty()) {
			State current = queue.top();
			queue.pop();
			const BigState &currentState = states[current];
			processedStates += currentState.size();

			static WordSet<Letter>		 temporaryWords;	 // used for the new state delays
			static std::vector<BigState> nextStates;
			static std::vector<std::reference_wrapper<typename Map::value_type>>
				  currentTransitions;	  // transitions for the current state
			State nextState		= 0;
			auto  localNewState = [&nextState]() {
				 auto &ref = nextStates.emplace_back();
				 return std::tuple(std::reference_wrapper{ref}, nextState++);
			};

			// for each (q,w) in the current state
			for (const auto &[q, delay_id] : currentState) {
				auto oldDelay		  = stateDelays[delay_id];
				const auto [it1, it2] = fsa.transitions.equal_range(q);
				for (const auto &[_, right] : std::ranges::subrange(it1, it2)) {
					const auto &[s, id, next] = right;

					auto it = transitions.find({current, s});	  // for each transition from 'current' with letter 's'
					if (it == transitions.end()) {
						// create a new transition
						auto wordToDelay  = std::views::concat(oldDelay, fsa.words[id]);
						auto new_id		  = words.addWord(wordToDelay);
						auto temp_id	  = temporaryWords.addWord(wordToDelay);
						auto [to, to_ind] = localNewState();
						auto [t_it, b]	  = transitions.insert({{current, s}, {new_id, to_ind}});
						currentTransitions.emplace_back(std::ref(*t_it));
						to.get().emplace_back(next, temp_id);
					} else {
						auto &[_, rhs]		= *it;
						auto &[outputID, n] = rhs;
						// update the existing transition
						auto  wordToDelay = std::views::concat(oldDelay, fsa.words[id]);
						auto  temp_id	  = temporaryWords.addWord(wordToDelay);
						auto  outputWord  = words[outputID];
						auto  i			  = commonPrefixLen(wordToDelay, outputWord);
						auto &nextBig	  = nextStates[n];
						words.replaceWithSubstr(outputID, 0, i);
						nextBig.emplace_back(next, temp_id);
					}
				}
			}

			for (const auto &ref : currentTransitions) {
				auto &[_, rhs]		 = ref.get();
				auto &[outputID, to] = rhs;
				auto &nextBig		 = nextStates[to];

				for (auto &[q, delay_id] : nextBig) {
					auto wrongDelay = temporaryWords[delay_id];
					auto eaten		= words[outputID].size();

					if (wrongDelay.size() > curr_max) { curr_max = wrongDelay.size(); }
					if (wrongDelay.size() - eaten > MAX_DELAY) {
						throw std::runtime_error("Delay too long, bounded variation not satisfied");
					}

					auto new_id =
						stateDelays.addWord((Letter *)&*wrongDelay.begin() + eaten, wrongDelay.size() - eaten);
					delay_id = new_id;
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
				for (const auto &[q, delayID] : nextState) {
					if (fsa.qFinals.contains(q)) {
						qFinals.insert(newIndex);
						auto output = words.addWord(stateDelays[delayID]);
						// if there is already an output for this state and it is different
						if (this->output.contains(newIndex) &&
							!std::ranges::equal(stateDelays[delayID], words[this->output[newIndex]])) {
							if (!resolveNonFunctionality)
								throw std::runtime_error(
									std::format("Non-functionality detected at state {} between outputs {} and {}",
												newIndex, stateDelays[delayID], words[this->output[newIndex]]));
							else {
								// try to resolve by choosing the output that ends in this state
								assert(bestOutToKeep != -1ull);
								auto [b1, e1] = fsa.transitions.equal_range(bestOutToKeep);
								auto [b2, e2] = fsa.transitions.equal_range(q);

								bool bestHasFuture = b1 != e1;
								bool currHasFuture = b2 != e2;
								std::cout << "conflict at state " << newIndex << " between outputs "
										  << words[this->output[newIndex]][0] << " and " << words[output][0]
										  << std::endl;

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
				std::cout << " Mean states in SSFT state: " << (double)processedStates / (double)states.size()
						  << std::flush;
			});

			// if (curr_max >= 1) {
			//	ShellProcess p("dot -Tsvg > a.svg && feh ./a.svg");
			//	auto		&in = p.in();
			//	in << "digraph SSFT {\n";
			//	in << "  rankdir=LR;\n";
			//	in << "  node [shape=circle];\n";
			//	in << "  init [label=\"N=" << states.size() << "\", shape=square];\n";
			//	in << "  init -> 0;\n";		// initial state
			//	for (const auto [i, state] : std::ranges::views::enumerate(states)) {
			//		in << "  " << i << " [label=\"";
			//		for (const auto &[q, id] : state.get()) {
			//			in << "(" << q << ", ";
			//			for (const auto &letter : stateDelays[id]) {
			//				if (uint8_t(letter) < 128 && uint8_t(letter) >= 32) in << letter;
			//				else in << (int)letter;
			//			}
			//			in << ")\n ";
			//		}
			//		in << "\"";
			//		if (qFinals.contains(i)) in << ", shape=doublecircle";
			//		in << "];\n";	  // final States
			//	}

			//	for (const auto &[from, value] : transitions) {
			//		const auto &[s, l]		   = from;
			//		const auto &[outputID, to] = value;
			//		in << "  " << s << " -> " << to << " [label=\"<" << l << ", ";
			//		for (const auto &letter : words[outputID]) {
			//			if (uint8_t(letter) < 128 && uint8_t(letter) >= 32) in << letter;
			//			else in << (int)letter;
			//		}
			//		in << ">\"];\n";
			//	}
			//	in << "}\n";

			//	p.in() << std::endl;
			//	p.in().close();
			//	p.wait();
			//	std::cout << getString(p.out()) << std::endl;
			//	std::cout << getString(p.err()) << std::endl;
			//}

			// clear temporary data to conserve memory allocation
			nextStates.clear();
			currentTransitions.clear();
			temporaryWords.clear();
		}
		this->N = states.size();

		// print in dot format
		if (N < 100) {
			ShellProcess p("dot -Tsvg > a.svg && feh ./a.svg");
			auto		&in = p.in();
			in << "digraph SSFT {\n";
			in << "  rankdir=LR;\n";
			in << "  node [shape=circle];\n";
			in << "  init [label=\"N=" << states.size() << "\", shape=square];\n";
			in << "  init -> 0;\n";		// initial state
			for (const auto [i, state] : std::ranges::views::enumerate(states)) {
				in << "  " << i << " [label=\"";
				for (const auto &[q, id] : state.get()) {
					in << "(" << q << ", ";
					for (const auto &letter : stateDelays[id]) {
						if ((uint8_t(letter) < 128 && uint8_t(letter) >= 32) || size_t(letter) > 256) in << letter;
						else in << (int)letter;
					}
					in << ")\n ";
				}
				in << "\"";
				if (qFinals.contains(i)) in << ", shape=doublecircle";
				in << "];\n";	  // final States
			}

			for (const auto &[from, value] : transitions) {
				const auto &[s, l]		   = from;
				const auto &[outputID, to] = value;
				in << "  " << s << " -> " << to << " [label=\"<" << l << ", ";
				for (const auto &letter : words[outputID]) {
					if ((uint8_t(letter) < 128 && uint8_t(letter) >= 32) || size_t(letter) > 256) in << letter;
					else in << (int)letter;
				}
				in << ">\"];\n";
			}
			in << "}\n";

			p.in() << std::endl;
			p.in().close();
			p.wait();
			std::cout << getString(p.out()) << std::endl;
			std::cout << getString(p.err()) << std::endl;
		}
		std::cout << std::endl;
	}

	auto f(const std::vector<Letter> &input) const {
		std::vector<Letter> output;
		State				current = 0;	 // initial state
		for (const auto &letter : input) {
			auto it = transitions.find({current, letter});
			if (it == transitions.end()) return std::pair{output, false};
			const auto &[outputID, next] = it->second;
			output.insert(output.end(), words[outputID].begin(), words[outputID].end());
			current = next;
		}
		if (qFinals.contains(current)) {
			auto word = words[this->output.at(current)];
			output.insert(output.end(), word.begin(), word.end());
		} else return std::pair{output, false};		// not in final state
		return std::pair{output, true};				// in final state
	}

	void print(std::ostream &out) const {
		out << "digraph SSFT {\n";
		out << "  rankdir=LR;\n";
		out << "  node [shape=circle];\n";
		out << "  init [label=\"N=" << N << "\", shape=square];\n";
		out << "  init -> 0;\n";	 // initial state
		for (const auto &q : qFinals) {
			if (qFinals.contains(q)) {
				out << "  " << q << " [shape=doublecircle, label=\"";
				for (const auto &letter : words[output.at(q)]) {
					if ((uint8_t(letter) < 128 && uint8_t(letter) >= 32) || size_t(letter) > 256) out << letter;
					else out << (int)letter;
				}
				out << "\"];\n";									   // final States with output
			} else out << "  " << q << " [shape=doublecircle];\n";	   // final States
		}
		for (const auto &[from, value] : transitions) {
			const auto &[s, l]		   = from;
			const auto &[outputID, to] = value;
			out << "  " << s << " -> " << to << " [label=\"<" << l << ", ";
			for (const auto &letter : words[outputID]) {
				if ((uint8_t(letter) < 128 && uint8_t(letter) >= 32) || size_t(letter) > 256) out << letter;
				else out << (int)letter;
			}
			out << ">\"];\n";
		}
		out << "}\n";
	}

	template <std::size_t N, std::size_t Transitions>
	struct PackedSSFTInputOnly {
		std::array<std::tuple<uint8_t, uint16_t, uint8_t>, Transitions> transitions_list;
		std::array<Letter, N>											output;
		State															first;

		constexpr PackedSSFTInputOnly() : transitions_list{}, output{'\0'}, first(0) {}

		void write(std::ostream &out) const {
			out << "#include <cstdint>\n";
			out << "#define SSFT_INCLUDED\n";
			out << "constexpr uint64_t packed_ssft[] = {\n";
			const uint64_t *data = reinterpret_cast<const uint64_t *>(this);
			for (std::size_t i = 0; i < sizeof(PackedSSFTInputOnly) / sizeof(uint64_t); ++i) {
				out << "0x" << std::hex << data[i] << std::dec << "ULL,";
			}
			out << "};\n";
		}
	};

	template <std::size_t N, std::size_t T>
	constexpr PackedSSFTInputOnly<N, T> packInputOnly() const {
		if (N != this->N) { dbLog(dbg::LOG_ERROR, "SSFT::packInputOnly: N mismatch: ", N, " != ", this->N); }
		if (T != this->transitions.size()) {
			dbLog(dbg::LOG_ERROR, "SSFT::packInputOnly: Transitions mismatch: ", T, " != ", this->transitions.size());
		}
		PackedSSFTInputOnly<N, T> packed;
		packed.first	  = 0;
		std::size_t index = 0;
		for (const auto &[lhs, rhs] : transitions) {
			const auto &[from, letter]		 = lhs;
			const auto &[out, to]			 = rhs;
			packed.transitions_list[index++] = {from, uint16_t(letter), to};
		}
		for (std::size_t s = 0; s < N; ++s) {
			if (qFinals.contains(State(s))) {
				packed.output[s] = words[output.at(State(s))][0];
			} else {
				packed.output[s] = Letter::eof;
			}
		}
		return packed;
	}

	template <std::size_t N, std::size_t T>
	static auto loadPackedInputOnly(const PackedSSFTInputOnly<N, T> &packed) {
		SSFT<Letter> ssft;
		ssft.N = 0;
		for (const auto &trans : packed.transitions_list) {
			const auto &[from, letter, to]			 = trans;
			ssft.transitions[{from, Letter(letter)}] = {0, to};
			if (from >= ssft.N) ssft.N = from + 1;
			if (to >= ssft.N) ssft.N = to + 1;
		}
		for (std::size_t s = 0; s < N; ++s) {
			if (packed.output[s] != Letter::eof) {
				ssft.qFinals.insert(State(s));
				ssft.output[State(s)] = ssft.words.addWord(std::array<Letter, 1>{packed.output[s]});
			}
		}
		return ssft;
	}

	void printInfo(std::ostream &out) const {
		out << std::format("SSFT has {} states and {} transitions.\n", N, transitions.size());
		out << std::format("Average transitions per state: {:.2f}\n", static_cast<double>(transitions.size()) / N);
		std::map<State, int> transitionCount;
		for (const auto &[_, value] : transitions) {
			const auto &[_, to] = value;
			transitionCount[to]++;
		}
		out << std::format("Number of final states: {}\n", qFinals.size());
	}
};

template <class Letter>
void drawFSA(const SSFT<Letter> &fsa) {
	ShellProcess p("dot -Tsvg > a.svg && feh ./a.svg");
	fsa.print(p.in());
	p.in() << std::endl;
	p.in().close();
	p.wait();
	auto out = getString(p.out()), err = getString(p.err());
	if (!out.empty()) std::cout << out << std::endl;
	if (!err.empty()) std::cout << err << std::endl;
}

template <class Letter>
class SSFTTraverser {
	const SSFT<Letter> &ssft;
	using State = typename SSFT<Letter>::State;

   public:
	State current;

	SSFTTraverser(const SSFT<Letter> &ssft) : ssft(ssft), current(0) {}

	auto step(const Letter &letter) {
		auto it = ssft.transitions.find({current, letter});
		if (it == ssft.transitions.end()) return std::make_pair(std::span<const Letter>{}, false);
		const auto &[outputID, next] = it->second;
		current						 = next;
		return std::make_pair(ssft.words[outputID], true);
	}

	template <std::ranges::input_range R>
	auto traverseOutputOnlyUntilCan(R &&input) {
		current = 0;
		for (const auto &letter : input) {
			auto [out_span, ok] = step(letter);
			if (!ok) return out_span;
		}
		if (ssft.qFinals.contains(current)) {
			return ssft.words[ssft.output.at(current)];
		} else return std::span<const Letter>{};
	}

	template <class Iterator, class Sentinel>
	std::pair<std::optional<Letter>, int> traverseOutputOnlyUntilCan(Iterator &begin, Sentinel &&end) {
		current = 0;
		int len = 0;
		for (; begin != end; ++begin) {
			auto [out_span, ok] = step(*begin);
			if (!ok) break;
			++len;
		}
		if (len == 0) {
			++begin;
			++len;
		}
		if (ssft.qFinals.contains(current)) {
			const auto &out = ssft.words[ssft.output.at(current)];
			assert(out.size() == 1);
			return std::make_pair(out[0], len);
		} else return std::make_pair(std::nullopt, len);
	}
};
}	  // namespace fl
