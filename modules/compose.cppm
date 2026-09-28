module;

#include <cassert>
#include <queue>

export module formlang:compose;

import :total_ssft;
import :transducer_concepts;
import :datastructures;

export namespace fl {

/// Composes two subsequential transducers
template <FST T1, FST T2>
	requires(FST_with_arcs<T1> && FST_traversable<T2> &&
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
		auto driveSecond =
			[&](State2									s2,
				const typename T1::OutputMonoid::Value &midWord) -> std::optional<std::pair<OutValue, State2>> {
			if (midWord == get<1>(first.GetMonoid()).identity) {
				if constexpr (SSFSTI<T2>)
					return std::pair{outMonoid.own(get<1>(second.GetMonoid()), second.InitialOutput()), s2};
				else return std::pair{OutputMonoid::identity, s2};
			}

			auto word  = get<1>(first.GetMonoid()).gen(midWord);
			auto symIt = word.begin();

			auto t = second.Transition(s2, *symIt);
			if (!t) return std::nullopt;
			auto [val2, next2] = *t;

			// this may not be T2::OutputMonoid::Value, but is convertible to it
			auto acc = outMonoid.own(get<1>(second.GetMonoid()), val2);
			if constexpr (SSFSTI<T2>) {
				auto t2InitOut	  = second.InitialOutput();
				auto t2InitOutVal = outMonoid.own(get<1>(second.GetMonoid()), t2InitOut);
				acc				  = outMonoid.mul(t2InitOutVal, acc);
			}

			s2 = next2;

			for (++symIt; symIt != word.end(); ++symIt) {
				auto tN = second.Transition(s2, *symIt);
				if (!tN) return std::nullopt;
				auto [valN, nextN] = *tN;

				auto piece = outMonoid.own(get<1>(second.GetMonoid()), valN);
				acc		   = outMonoid.mul(acc, piece);
				s2		   = nextN;
			}
			return std::pair{OutValue(acc), s2};
		};

		State1 s1init = *first.Initial().begin();
		State2 s2init = *second.Initial().begin();

		State2 s2afterInit = s2init;
		if constexpr (SSFSTI<T1>) {
			auto res = driveSecond(s2init, first.InitialOutput());
			if (!res) {
				// create initial state and exit;
				stateID({s1init, s2init});
				return;
			}
			auto [initOut, s2New] = *res;
			this->initialOut	  = initOut;
			s2afterInit			  = s2New;
		} else {
			if constexpr (SSFSTI<T2>)
				this->initialOut = outMonoid.own(get<1>(second.GetMonoid()), second.InitialOutput());
			else this->initialOut = OutputMonoid::identity;
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
			auto res = driveSecond(s2, first.Psi(s1));
			if (res) {
				auto [midOut, s2final]	= *res;
				OutValue secondFinal	= outMonoid.own(get<1>(second.GetMonoid()), second.Psi(s2final));
				this->output[currentID] = outMonoid.mul(midOut, secondFinal);
			}

			for (const auto &[val, next1] : first.Transitions(s1)) {
				const auto &[l, midWord] = val;
				auto res2 = driveSecond(s2, midWord);
				if (!res2) continue;	 // no transition in T2 for this midWord
				auto [outVal, next2] = *res2;

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
