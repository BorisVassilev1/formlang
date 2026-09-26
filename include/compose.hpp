#pragma once

#include <queue>
#include <cassert>
#include <iomanip>

#include "TotalSSFT.hpp"
#include "concepts.hpp"
#include "datastructures.hpp"
#include "utils.h"

namespace fl {

// template <fl::symbol Symbol>
// class ComposeTotalSSFT : public TotalSSFT<Symbol> {
//    public:
//	ComposeTotalSSFT(const TotalSSFT<Symbol> &first, const TotalSSFT<Symbol> &second) {
//		using State = typename TotalSSFT<Symbol>::State;
//
//		using BigState = std::tuple<State, State>;
//
//		fl::unordered_map<BigState, State> stateRemap;
//		std::vector<bool>				   visited;
//		auto							   stateID = [&](const BigState &state) -> State {
//			auto it = stateRemap.find(state);
//			if (it != stateRemap.end()) return it->second;
//			State newID		  = this->N++;
//			stateRemap[state] = newID;
//			this->transitions.emplace_back();
//			visited.push_back(false);
//			this->output.push_back(0);
//			assert(this->transitions.size() == this->N);
//			assert(visited.size() == this->N);
//			assert(this->output.size() == this->N);
//			return newID;
//		};
//
//		std::vector<Symbol> scratchOutput;
//		auto				createTransition = [&](const BigState &state, State state_id,
//												   Symbol letter) -> std::pair<BigState, State> {
//			const auto &[s1, s2] = state;
//			State next_s1 = s1, next_s2 = s2;
//			auto [o1, succ1] = first.step(next_s1, letter);
//			assert(succ1);
//			scratchOutput.clear();
//			bool succ2 = second.steps(next_s2, o1, scratchOutput);
//			assert(succ2);
//
//			BigState next_state{next_s1, next_s2};
//			State	 next_id							= stateID(next_state);
//			this->transitions[state_id][(size_t)letter] = {this->words.addWord(scratchOutput), next_id};
//			return {next_state, next_id};
//		};
//
//		using namespace std::chrono_literals;
//		fl::SlowDown3 sd{100ms};
//
//		// bfs generation
//		std::queue<BigState> q;
//		{
//			State s2 = second.initial();
//			scratchOutput.clear();
//			auto succ2 = second.steps(s2, first.initialOutput(), scratchOutput);
//			assert(succ2);
//			this->initialOut = this->words.addWord(scratchOutput);
//			q.push({first.initial(), s2});
//		}
//		while (!q.empty()) {
//			BigState current = q.front();
//			q.pop();
//			State currentID = stateID(current);
//			if (visited[currentID]) continue;
//			visited[currentID] = true;
//
//			sd.do_thing([&] {
//				std::cerr << "\rGenerating state " << std::setw(10) << currentID << " / " << std::setw(10) << this->N
//						  << " qsize = " << q.size() << "               " << std::flush;
//			});
//
//			// calculate the psi-function for the current state
//			auto [s1, s2] = current;
//			auto o1		  = first.psi(s1);
//			scratchOutput.clear();
//			bool succ2 = second.steps(s2, o1, scratchOutput);
//			assert(succ2);
//			auto o2 = second.psi(s2);
//			scratchOutput.insert(scratchOutput.end(), o2.begin(), o2.end());
//			this->output[currentID] = this->words.addWord(scratchOutput);
//
//			for (Symbol letter = 0; letter < Symbol::size; ++letter) {
//				auto [next, nextID] = createTransition(current, currentID, letter);
//				if (!visited[nextID]) { q.push(next); }
//			}
//		}
//		std::cerr << std::endl;
//	}
// };

/// Composes two subsequential transducers into one that reads T1's input
/// alphabet and produces T2's output monoid: every output symbol T1 emits is
/// fed straight into T2 as it's produced. "Different domains" means T1 and
/// T2 need not agree on a Symbol type or share an OutputMonoid with each
/// other -- the only requirement (the standard composability condition) is
/// that T1's output tape and T2's input tape are literally the same monoid,
/// so T1's output values can be decoded directly as T2 input symbols.
template <SSFST T1, SSFST T2>
	requires(free_monoid<typename T1::OutputMonoid> &&
			 std::same_as<typename T1::OutputMonoid::Symbol, typename T2::InputMonoid::Symbol>)
class ComposeSSFST : public TotalSSFST<typename T1::Letter_t, T1::alphabet_size, get_output_t<T2>> {
	using Symbol	   = typename T1::Letter_t;
	using Base		   = TotalSSFST<Symbol, T1::alphabet_size, get_output_t<T2>>;
	using OutputMonoid = typename Base::OutputMonoid;
	using OutValue	   = typename Base::OutValue;

   public:
	using State = typename Base::State;

	ComposeSSFST(const T1 &first, const T2 &second) {
		using State1   = typename T1::State;
		using State2   = typename T2::State;
		using BigState = std::tuple<State1, State2>;

		auto &outMonoid = get<1>(this->GetMonoid());

		fl::unordered_map<BigState, State> stateRemap;
		std::vector<bool>				   visited;

		auto stateID = [&](const BigState &state) -> State {
			auto it = stateRemap.find(state);
			if (it != stateRemap.end()) return it->second;
			State newID		  = this->N++;
			stateRemap[state] = newID;
			this->transitions.emplace_back();
			visited.push_back(false);
			this->output.push_back(OutputMonoid::identity);
			return newID;
		};

		// Returns the accumulated OutValue and the T2 state reached.
		auto driveSecond = [&](State2								   s2,
							   const typename T1::OutputMonoid::Value &midWord) -> std::pair<OutValue, State2> {
			auto word = get<1>(first.GetMonoid()).gen(midWord);
			if (word.empty()) return {OutputMonoid::identity, s2};

			auto symIt		   = word.begin();
			auto [val2, next2] = second.Transitions(s2)[*symIt];
			// this may not be T2::OutputMonoid::Value, but is convertible to it
			auto acc = outMonoid.own(get<1>(second.GetMonoid()), std::get<1>(val2));
			s2		 = next2;

			for (++symIt; symIt != word.end(); ++symIt) {
				auto [valN, nextN] = second.Transitions(s2)[*symIt];
				auto piece		   = outMonoid.own(get<1>(second.GetMonoid()), std::get<1>(valN));
				acc				   = outMonoid.mul(acc, piece);
				s2				   = nextN;
			}
			return {OutValue(acc), s2};
		};

		State1 s1init = *first.Initial().begin();
		State2 s2init = *second.Initial().begin();

		State2 s2afterInit = s2init;
		if constexpr (SSFSTI<T1>) {
			auto [initOut, s2New] = driveSecond(s2init, first.InitialOutput());
			this->initialOut	  = initOut;
			s2afterInit			  = s2New;
		} else {
			this->initialOut = OutputMonoid::identity;
		}

		BigState			 initialState{s1init, s2afterInit};
		std::queue<BigState> q;
		q.push(initialState);

		while (!q.empty()) {
			BigState current = q.front();
			q.pop();
			State currentID = stateID(current);
			if (visited[currentID]) continue;
			visited[currentID] = true;

			auto [s1, s2] = current;

			// Psi: T1's final output at s1, threaded through T2 from s2,
			// then second's own final output at the T2 state that lands on.
			auto [midOut, s2final]	= driveSecond(s2, first.Psi(s1));
			OutValue secondFinal	= outMonoid.own(get<1>(second.GetMonoid()), second.Psi(s2final));
			this->output[currentID] = outMonoid.mul(midOut, secondFinal);

			for (Symbol l = 0; l < T1::alphabet_size; ++l) {
				auto [val1, next1]	 = first.Transitions(s1)[l];
				const auto &midWord	 = std::get<1>(val1);
				auto [outVal, next2] = driveSecond(s2, midWord);

				BigState nextState						= {next1, next2};
				State	 nextID							= stateID(nextState);
				this->transitions[currentID][size_t(l)] = {outVal, nextID};
				if (!visited[nextID]) q.push(nextState);
			}
		}
	}
};

template <class T1, class T2>
ComposeSSFST(T1, T2) -> ComposeSSFST<std::remove_cvref_t<T1>, std::remove_cvref_t<T2>>;

};	   // namespace fl
