#pragma once

#include "concepts.hpp"
#include "wordset.hpp"

template <fl::SSFST Transducer>
auto mso(const Transducer &ssft) {
	using Letter = typename Transducer::Letter_t;
	using State	 = typename Transducer::State;

	fl::WordSet<Letter> words;
	using WordID = typename fl::WordSet<Letter>::WordID;
	std::vector<WordID> mso(ssft.N, -1);

	std::unordered_map<std::tuple<State, Letter>, std::tuple<State, WordID>> reverseDelta;

	/// TODO

}
