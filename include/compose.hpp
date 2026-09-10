#pragma once

#include <queue>
#include <cassert>
#include <iomanip>

#include "TotalSSFT.hpp"
#include "concepts.hpp"
#include "datastructures.hpp"
#include "utils.h"

namespace fl {

template <fl::symbol Symbol>
class ComposeTotalSSFT : public TotalSSFT<Symbol> {
   public:
	ComposeTotalSSFT(const TotalSSFT<Symbol> &first, const TotalSSFT<Symbol> &second) {
		using State = typename TotalSSFT<Symbol>::State;

		using BigState = std::tuple<State, State>;

		fl::unordered_map<BigState, State> stateRemap;
		std::vector<bool>				   visited;
		auto							   stateID = [&](const BigState &state) -> State {
			auto it = stateRemap.find(state);
			if (it != stateRemap.end()) return it->second;
			State newID		  = this->N++;
			stateRemap[state] = newID;
			this->transitions.emplace_back();
			visited.push_back(false);
			this->output.push_back(0);
			assert(this->transitions.size() == this->N);
			assert(visited.size() == this->N);
			assert(this->output.size() == this->N);
			return newID;
		};

		std::vector<Symbol> scratchOutput;
		auto				createTransition = [&](const BigState &state, State state_id,
												   Symbol letter) -> std::pair<BigState, State> {
			const auto &[s1, s2] = state;
			State next_s1 = s1, next_s2 = s2;
			auto [o1, succ1] = first.step(next_s1, letter);
			assert(succ1);
			scratchOutput.clear();
			bool succ2 = second.steps(next_s2, o1, scratchOutput);
			assert(succ2);

			BigState next_state{next_s1, next_s2};
			State	 next_id							= stateID(next_state);
			this->transitions[state_id][(size_t)letter] = {this->words.addWord(scratchOutput), next_id};
			return {next_state, next_id};
		};

		using namespace std::chrono_literals;
		fl::SlowDown3 sd{100ms};

		// bfs generation
		std::queue<BigState> q;
		{
			State s2	 = second.initial();
			scratchOutput.clear();
			auto succ2 = second.steps(s2, first.initialOutput(), scratchOutput);
			assert(succ2);
			this->initialOut = this->words.addWord(scratchOutput);
			q.push({first.initial(), s2});
		}
		while (!q.empty()) {
			BigState current = q.front();
			q.pop();
			State currentID = stateID(current);
			if (visited[currentID]) continue;
			visited[currentID] = true;

			sd.do_thing([&] {
				std::cerr << "\rGenerating state " << std::setw(10) << currentID << " / " << std::setw(10) << this->N
						  << " qsize = " << q.size() << "               " << std::flush;
			});

			// calculate the psi-function for the current state
			auto [s1, s2]		   = current;
			auto				o1 = first.psi(s1);
			scratchOutput.clear();
			bool				succ2 = second.steps(s2, o1, scratchOutput);
			assert(succ2);
			auto o2 = second.psi(s2);
			scratchOutput.insert(scratchOutput.end(), o2.begin(), o2.end());
			this->output[currentID] = this->words.addWord(scratchOutput);

			for (Symbol letter = 0; letter < Symbol::size; ++letter) {
				auto [next, nextID] = createTransition(current, currentID, letter);
				if (!visited[nextID]) { q.push(next); }
			}
		}
		std::cerr << std::endl;
	}
};

};	   // namespace fl
