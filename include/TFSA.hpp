#pragma once

#include <ctime>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cassert>

#include "FST.hpp"
#include "utils.h"
#include "wordset.hpp"
#include "debug.hpp"

namespace fl {

// Classical Two-Tape Finite State Automaton (TFSA)
template <class Letter>
class TFSA {
   public:
	using State	   = unsigned int;
	using StringID = typename WordSet<Letter>::WordID;

	using Map = unordered_multimap<State, std::tuple<Letter, StringID, State>>;

	unsigned int		 N = 0;
	unordered_set<State> qFirsts;
	unordered_set<State> qFinals;
	Map					 transitions;
	WordSet<Letter>		 words;

	unordered_set<StringID> f_eps{};

	void addTransition(State from, Letter w1, StringID w2, State to) { transitions.insert({from, {w1, w2, to}}); }
	void addTransition(State from, Letter w1, const std::vector<Letter> w2, State to) {
		if (w2.empty()) addTransition(from, w1, 0, to);
		else {
			StringID id2 = words.addWord(std::span{w2.data(), w2.size()});
			addTransition(from, w1, id2, to);
		}
	}

	void print(std::ostream &out) const {
		// print in DOT
		out << "digraph TFSA {\n";
		out << "  rankdir=LR;\n";
		out << "  node [shape=circle];\n";
		out << "  init [label=\"N=" << this->N << "\", shape=square];\n";
		for (const int i : qFinals) {
			out << "  " << i << " [shape=doublecircle];\n";		// final States
		}
		for (const int i : qFirsts) {
			out << "  init -> " << i << " [style=dotted];\n";	  // initial states
		}
		for (const auto &[from, value] : transitions) {
			auto &[w1, id2, to] = value;
			auto w2				= words.getWord(id2);
			out << "  " << from << " -> " << to << " [label=\"<";
			if (w1 == Letter('\"')) out << "\\";
			out << w1 << ",";
			for (const auto &letter : w2) {
				// if ((uint8_t(letter) < 128 && uint8_t(letter) >= 32) || size_t(letter) > 256)
				out << letter;	   // TODO: This will break for some Letter types
								   // else out << (int)letter;
			}
			out << ">\"];\n";
		}
		out << "}\n";
	}

	State newState() { return N++; }

	/// this will work only for the type BPEToken because this outputs the outpt wordID-s and not the actual letters
	/// this was done because that is the case for byte-pair encodings
	auto f(const std::vector<Letter> &input) const {
		using DelayID  = UniqueWordSet<Letter>::WordID;
		using BigState = std::vector<std::tuple<State, DelayID>>;	  // (state, position in input)
		static BigState			state0;
		static BigState			state1;
		static WordSet<DelayID> stateDelays0;
		static WordSet<DelayID> stateDelays1;

		auto *currentState	  = &state0;
		auto *nextState		  = &state1;
		auto *stateDelays	  = &stateDelays0;
		auto *nextStateDelays = &stateDelays1;

		currentState->clear();
		nextState->clear();
		stateDelays->clear();
		stateDelays->addWord(std::span<DelayID>{});
		nextStateDelays->clear();
		nextStateDelays->addWord(std::span<DelayID>{});

		for (const auto &q : qFirsts) {
			currentState->push_back({q, 0});		// all have epsilon delay
		}
		std::sort(currentState->begin(), currentState->end());

		std::vector<Letter> output;
		for (size_t pos = 0; pos < input.size(); ++pos) {
			// std::cout << "TFSA::f: processing position " << pos << " / " << input.size()
			//		  << ", current states: " << currentState.size();
			//  for (const auto &[state, delayIndex] : currentState) {
			//	auto oldDelay = stateDelays[delayIndex];
			//	std::cout << " (state " << state << " with delay [";
			//	for (const auto &l : oldDelay) std::cout << l;
			//	std::cout << "])";
			//  }
			// std::cout << std::endl;

			DelayID delayID = -1;
			for (const auto &[state, delayIndex] : *currentState) {
				auto it		  = transitions.equal_range(state);
				auto oldDelay = stateDelays->getWord(delayIndex);
				for (const auto &[_, value] : RangeFromPair(it)) {
					const auto &[w1, id2, to] = value;
					if (w1 != input[pos]) continue;
					auto w2 = std::span{&id2, &id2 + 1};

					// std::cout << " TFSA::f: from state " << state << " with delay [";
					// for (const auto &l : oldDelay)
					//	std::cout << l << ' ';
					// std::cout << "], reading '" << w1 << "', output [";
					// for (const auto &l : w2)
					//	std::cout << l << ' ';
					// std::cout << "], to state " << to << std::endl;

					auto wordToDelay = std::views::concat(oldDelay, w2);
					auto temp_id	 = nextStateDelays->addWord(wordToDelay);
					if (delayID == (DelayID)-1) {
						delayID = nextStateDelays->addWord(wordToDelay);
					} else {
						auto i = fl::commonPrefixLen(wordToDelay, nextStateDelays->getWord(delayID));
						nextStateDelays->replaceWithSubstr(delayID, 0, i);
					}
					nextState->push_back({to, temp_id});
				}
			}
			if (delayID == (DelayID)-1) {
				dbLog(dbg::LOG_ERROR, "TFSA::f: no transitions found at position ", pos, ", symbol: '", input[pos],
					  "'\n");
				break;
			}

			auto eaten = nextStateDelays->getWord(delayID);

			// std::cout << " TFSA::f: eaten output [";
			// for (const auto &l : eaten)
			//	std::cout << l;
			// std::cout << "] delay id: " << delayID << std::endl;
			if (!eaten.empty()) {
				// std::cout << " TFSA::f: outputting eaten [";
				// for (const auto &l : eaten)
				//	std::cout << l << ' ';
				// std::cout << "]" << std::endl;

				for (const auto &l : eaten)
					if (l != 0) output.push_back(Letter(l));
			}

			for (auto &[q, delay_id] : *nextState) {
				auto wrongDelay = nextStateDelays->getWord(delay_id);
				if (wrongDelay.size() < eaten.size()) continue;
				auto correctDelay =
					std::span{(DelayID *)wrongDelay.data() + eaten.size(), wrongDelay.size() - eaten.size()};
				auto new_id = nextStateDelays->addWord(correctDelay);
				delay_id	= new_id;
			}

			std::sort(nextState->begin(), nextState->end());
			nextState->erase(std::unique(nextState->begin(), nextState->end()), nextState->end());

			std::swap(currentState, nextState);
			std::swap(stateDelays, nextStateDelays);
			nextState->clear();
			nextStateDelays->clear();
		}
		return output;
	}

	void writeBinary(std::ostream &out) const {
		out.write(reinterpret_cast<const char *>(&N), sizeof(N));
		size_t transitions_size = transitions.size();
		out.write(reinterpret_cast<const char *>(&transitions_size), sizeof(transitions_size));
		for (const auto &trans_map : transitions) {
			size_t map_size = 0;
			for (const auto &[_, _] : transitions)
				++map_size;
			out.write(reinterpret_cast<const char *>(&map_size), sizeof(map_size));
			for (const auto &[letter, value] : transitions) {
				const auto &[w1, id2, to] = value;
				out.write(reinterpret_cast<const char *>(&letter), sizeof(letter));
				out.write(reinterpret_cast<const char *>(&id2), sizeof(id2));
				out.write(reinterpret_cast<const char *>(&to), sizeof(to));
			}
		}
		size_t finals_size = qFinals.size();
		out.write(reinterpret_cast<const char *>(&finals_size), sizeof(finals_size));
		for (const auto &final_state : qFinals) {
			out.write(reinterpret_cast<const char *>(&final_state), sizeof(final_state));
		}
	}

	void readBinary(std::istream &in) {
		in.read(reinterpret_cast<char *>(&N), sizeof(N));
		size_t transitions_size;
		in.read(reinterpret_cast<char *>(&transitions_size), sizeof(transitions_size));
		for (size_t t = 0; t < transitions_size; ++t) {
			size_t map_size;
			in.read(reinterpret_cast<char *>(&map_size), sizeof(map_size));
			for (size_t i = 0; i < map_size; ++i) {
				Letter	 letter;
				StringID id2;
				State	 to;
				in.read(reinterpret_cast<char *>(&letter), sizeof(letter));
				in.read(reinterpret_cast<char *>(&id2), sizeof(id2));
				in.read(reinterpret_cast<char *>(&to), sizeof(to));
				transitions.insert({t, {letter, id2, to}});
			}
		}
		size_t finals_size;
		in.read(reinterpret_cast<char *>(&finals_size), sizeof(finals_size));
		for (size_t i = 0; i < finals_size; ++i) {
			State final_state;
			in.read(reinterpret_cast<char *>(&final_state), sizeof(final_state));
			qFinals.insert(final_state);
		}
	}
};

template <class Letter>
auto expandFST(FST<Letter> &&fst) {
	TFSA<Letter> expanded;
	using State		 = TFSA<Letter>::State;
	using StringID	 = TFSA<Letter>::StringID;
	expanded.N		 = fst.N;
	expanded.qFirsts = std::move(fst.qFirsts);
	expanded.qFinals = std::move(fst.qFinals);

	for (const auto &[from, value] : fst.transitions) {
		auto [id1, id2, to] = value;
		auto &w2			= fst.words[id2];
		if (id1 == 0) {
			auto new_id = expanded.words.addWord(w2);
			expanded.addTransition(from, Letter::eps, new_id, to);
			continue;
		}
		auto &w1   = fst.words[id1];
		State prev = from;

		if (w1.size() < w2.size()) {	 // |w1| < |w2|
			assert(w1.size() > 0);
			for (unsigned int i = 0; i < w1.size() - 1; ++i) {
				const auto &a	 = w1[i];
				const auto &b	 = w2[i];
				State		next = expanded.newState();
				StringID	w2id = expanded.words.addWord(std::span{&b, &b + 1});
				expanded.addTransition(prev, a, w2id, next);
				prev = next;
			}
			StringID w2id = expanded.words.addWord(std::span{w2.data() + w1.size() - 1, w2.size() - w1.size() + 1});
			expanded.addTransition(prev, w1.back(), w2id, to);
		} else {
			for (unsigned int i = 0; i < w2.size(); ++i) {
				const auto &a	 = w1[i];
				const auto &b	 = w2[i];
				State		next = (i == w1.size() - 1) ? to : expanded.newState();
				StringID	w2id = expanded.words.addWord(std::span{&b, &b + 1});
				expanded.addTransition(prev, a, w2id, next);
				prev = next;
			}
			for (unsigned int i = w2.size(); i < w1.size(); ++i) {
				const auto &a	 = w1[i];
				State		next = (i == w1.size() - 1) ? to : expanded.newState();
				expanded.addTransition(prev, a, 0, next);
				prev = next;
			}
		}
	}

	return expanded;
}

template <class Letter>
void drawFSA(const TFSA<Letter> &fsa) {
	ShellProcess p("dot -Tsvg > a.svg && feh ./a.svg");
	fsa.print(p.in());
	p.in() << std::endl;
	p.in().close();
	p.wait();
	auto out = getString(p.out()), err = getString(p.err());
	if (!out.empty()) std::cout << out << std::endl;
	if (!err.empty()) std::cout << err << std::endl;
}

// https://lml.bas.bg/~stoyan/finite-state-techniques.pdf#theorem.4.4.8
template <class Letter>
auto removeUpperEpsilonFST(TFSA<Letter> &&fsa) {
	using State	   = TFSA<Letter>::State;
	using StringID = TFSA<Letter>::StringID;

	std::stack<int>													 stack;
	std::vector<bool>												 visited(fsa.N, false);
	std::vector<std::vector<std::tuple<State, std::vector<Letter>>>> closure(fsa.N);

	for (State i = 0; i < fsa.N; ++i) {
		stack.push(0);
		visited[i] = true;
		closure[i].push_back({i, {}});	   // add the state itself with an empty word
		while (!stack.empty()) {
			auto p					= stack.top();
			const auto [current, u] = closure[i][p];
			stack.pop();

			auto [i1, i2] = fsa.transitions.equal_range(current);
			for (const auto &[_, value] : std::ranges::subrange(i1, i2)) {
				const auto &[w1, id2, to] = value;
				if (w1 == Letter::eps && !visited[to]) {	 // epsilon transition
					visited[to]	  = true;
					auto new_word = u;
					new_word.insert(new_word.end(), fsa.words.getWord(id2).begin(), fsa.words.getWord(id2).end());
					closure[i].push_back({to, std::move(new_word)});
					stack.push(closure[i].size() - 1);
				}
			}
		}

		visited.assign(fsa.N, false);
	}

	for (auto &i : fsa.qFirsts) {
		for (const auto &[c, w] : closure[i]) {
			if (fsa.qFinals.contains(c)) {
				fsa.qFinals.insert(i);
				fsa.f_eps.insert(fsa.words.addWord(w));		// f(eps)
			}
		}
	}

	std::erase_if(fsa.transitions, [](const auto &pair) {
		const auto &[from, value] = pair;
		const auto &[w1, id2, to] = value;
		return w1 == Letter::eps;	  // remove epsilon transitions
	});

	typename TFSA<Letter>::Map new_transitions;
	for (State q1 = 0; q1 < fsa.N; ++q1) {
		for (const auto &[q_, u] : closure[q1]) {
			auto [i1, i2] = fsa.transitions.equal_range(q_);
			for (const auto &[_, value] : std::ranges::subrange(i1, i2)) {
				const auto &[sigma, id2, q__] = value;
				const auto &v				  = fsa.words.getWord(id2);
				for (const auto &[q2, w] : closure[q__]) {
					auto new_word = u;
					new_word.insert(new_word.end(), v.begin(), v.end());
					new_word.insert(new_word.end(), w.begin(), w.end());
					StringID new_id = fsa.words.addWord(new_word);
					new_transitions.insert({q1, {sigma, new_id, q2}});
				}
			}
		}
	}
	fsa.transitions = std::move(new_transitions);

	return std::move(fsa);
}

template <class Letter>
auto trimFSA(TFSA<Letter> &&fsa) {
	if (fsa.qFinals.empty()) {
		fsa.N		= 0;
		fsa.qFirsts = {0};
		fsa.words.clear();
		fsa.words.addWord(std::span<Letter>{});		// add empty word
		fsa.transitions.clear();
		return std::move(fsa);
	}
	using State	   = FST<Letter>::State;
	using StringID = FST<Letter>::StringID;
	std::vector<bool> visited_back(fsa.N, false);
	std::vector<bool> visited_forw(fsa.N, false);

	{
		auto						   &forwardTransitions = fsa.transitions;
		std::vector<std::vector<State>> backwardTransitions;
		backwardTransitions.resize(fsa.N);
		for (const auto &[from, value] : forwardTransitions) {
			const auto &[_, _, to] = value;
			backwardTransitions[to].push_back(from);
		}

		std::vector<State> stack;
		if (fsa.qFinals.size() != fsa.N) {
			for (const auto &final : fsa.qFinals) {
				visited_back[final] = true;
				stack.push_back(final);
			}
			while (!stack.empty()) {
				State current = stack.back();
				stack.pop_back();
				for (const auto &next : backwardTransitions[current]) {
					if (!visited_back[next]) {
						visited_back[next] = true;
						stack.push_back(next);
					}
				}
			}
		} else {
			for (unsigned int i = 0; i < fsa.N; ++i) {
				visited_back[i] = true;
			}
		}
		dbLog(dbg::LOG_DEBUG, "Finished backward reachability")

			for (const auto &first : fsa.qFirsts) {
			visited_forw[first] = true;		// mark initial states as visited
			stack.push_back(first);
		}
		while (!stack.empty()) {
			State current = stack.back();
			stack.pop_back();
			auto [i1, i2] = forwardTransitions.equal_range(current);
			for (const auto &[_, value] : std::ranges::subrange(i1, i2)) {
				const auto &[id1, id2, to] = value;
				if (!visited_forw[to]) {
					visited_forw[to] = true;
					stack.push_back(to);
				}
			}
		}
		dbLog(dbg::LOG_DEBUG, "Finished forward reachability")
	}
	size_t			   cnt = 0;
	std::vector<State> new_map(fsa.N, -1);
	for (unsigned int i = 0; i < fsa.N; ++i) {
		if (visited_back[i] && visited_forw[i]) { new_map[i] = cnt++; }
	}
	dbLog(dbg::LOG_DEBUG, std::format("Trimmed FSA: {} / {} states are reachable from both sides.\n",
									  std::count(new_map.begin(), new_map.end(), -1u) ^ fsa.N, fsa.N));

	if (cnt == fsa.N) {
		dbLog(dbg::LOG_DEBUG, "No states were removed during trimming.");
		return std::move(fsa);
	}

	TFSA<Letter> new_fsa;
	new_fsa.N = cnt;
	new_fsa.qFirsts.reserve(fsa.qFirsts.size());
	for (const auto &q : fsa.qFirsts) {
		if (new_map[q] != -1u) { new_fsa.qFirsts.insert(new_map[q]); }
	}

	new_fsa.qFinals.reserve(fsa.qFinals.size());
	for (const auto &q : fsa.qFinals) {
		if (new_map[q] != -1u) { new_fsa.qFinals.insert(new_map[q]); }
	}

	std::vector<bool> words_used(fsa.words.size(), false);
	words_used[0] = true;
	for (const auto &q : fsa.f_eps) {
		words_used[q] = true;
	}
	for (const auto &[from, value] : fsa.transitions) {
		const auto &[_, id, to] = value;
		if (new_map[from] != -1u && new_map[to] != -1u) { words_used[id] = true; }
	}

	std::vector<StringID> words_index_map(fsa.words.size(), -1);
	for (size_t i = 0; i < fsa.words.size(); ++i) {
		if (words_used[i]) {
			auto id			   = new_fsa.words.addWord(std::move(fsa.words.getWord(i)));
			words_index_map[i] = id;
		}
	}
	unordered_set<StringID> new_f_eps;
	for (const auto &q : fsa.f_eps) {
		if (words_used[q]) { new_f_eps.insert(words_index_map[q]); }
	}
	new_fsa.f_eps = std::move(new_f_eps);

	for (const auto &[from, value] : fsa.transitions) {
		const auto &[sigma, id, to] = value;
		if (new_map[from] != -1u && new_map[to] != -1u) {
			State	 new_from = new_map[from];
			State	 new_to	  = new_map[to];
			StringID new_id	  = words_index_map[id];
			new_fsa.transitions.insert({new_from, {sigma, new_id, new_to}});
		}
	}

	return std::move(new_fsa);
}

template <class Letter>
auto realtimeFST(FST<Letter> &&fst) {
	return trimFSA(removeUpperEpsilonFST(expandFST(removeEpsilonFST(trimFSA(std::move(fst))))));
}

template <class Letter>
auto pseudoDeterminizeFST(TFSA<Letter> &&fst) {
	using State = TFSA<Letter>::State;

	using BigState	= std::vector<State>;
	using BigLetter = std::tuple<Letter, typename UniqueWordSet<Letter>::WordID>;

	struct MyHash {
		using is_transparent = void;
		constexpr size_t operator()(const BigState &x) const {
			return std::hash<std::string_view>()(std::string_view(reinterpret_cast<const char *>(x.data()), x.size()));
		}
	};

	TFSA<Letter>										dfa;
	std::vector<std::reference_wrapper<const BigState>> states;
	unordered_map<BigState, State, MyHash>				state_map;
	std::queue<State>									queue;
	UniqueWordSet<Letter>								secondTapeWords;

	auto getStateID = [&](BigState &&bs) -> std::pair<State, bool> {
		std::ranges::sort(bs);
		bs.erase(std::unique(bs.begin(), bs.end()), bs.end());

		auto it = state_map.find(bs);
		if (it == state_map.end()) {
			State new_id = dfa.newState();
			for (const auto &s : bs) {
				if (fst.qFinals.contains(s)) {
					dfa.qFinals.insert(new_id);
					break;
				}
			}
			auto [it, _] = state_map.emplace(std::move(bs), new_id);
			states.emplace_back(it->first);
			// check if any of the states in bs is final
			return {new_id, true};
		} else {
			return {it->second, false};
		}
	};

	auto [initial_state, _] = getStateID(BigState{std::from_range, fst.qFirsts});
	queue.push(initial_state);
	dfa.qFirsts.insert(initial_state);

	while (!queue.empty()) {
		State current = queue.front();
		queue.pop();
		const BigState &current_bs = states[current];

		unordered_map<BigLetter, BigState> current_transitions;

		for (const auto &q : current_bs) {
			auto [i1, i2] = fst.transitions.equal_range(q);
			for (const auto &[_, value] : std::ranges::subrange(i1, i2)) {
				const auto &[u, v, to] = value;
				BigLetter sigma		   = {u, secondTapeWords.addWord(fst.words.getWord(v))};
				current_transitions[sigma].push_back(to);
			}
		}

		for (const auto &[sigma, next_bs] : current_transitions) {
			auto [next_state, is_new]		  = getStateID(BigState(next_bs));
			auto [sigma_letter, sigma_wordid] = sigma;
			dfa.addTransition(current, sigma_letter, sigma_wordid, next_state);
			if (is_new) { queue.push(next_state); }
		}
	}

	// dbLog(dbg::LOG_DEBUG, "TFSA Pseudo-determinized FST has ", dfa.N, " states.");
	dfa.words = std::move(secondTapeWords.toWordSet());
	// drawFSA(dfa);

	return std::move(dfa);
}
}	  // namespace fl
