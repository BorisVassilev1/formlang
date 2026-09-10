#pragma once

#include <iostream>
#include <iterator>
#include <memory>
#include <ostream>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <ranges>
#include "concepts.hpp"

namespace fl {

template <symbol Symbol>
class WordSet {
   public:
	using WordID = unsigned int;
	struct WordData {
		size_t start;
		size_t length;
	};

   private:
	std::vector<Symbol>	  words;
	std::vector<WordData> wordsData;

	WordID nextWordID = 0;

   public:
	WordSet() { addWord(std::span<Symbol>{}); }

	WordSet(std::vector<Symbol> &&words_, std::vector<WordData> &&wordsData_)
		: words(std::move(words_)), wordsData(std::move(wordsData_)), nextWordID(wordsData.size()) {}
	WordSet(const std::vector<Symbol> &words_, const std::vector<WordData> &wordsData_)
		: words(std::move(words_)), wordsData(std::move(wordsData_)), nextWordID(wordsData.size()) {}

	template <class Input>
	WordID addWord(Input &&word) {
		wordsData.emplace_back(words.size(), word.size());
		words.insert(words.end(), word.begin(), word.end());
		return nextWordID++;
	}

	WordID copyWord(WordID id) {
		if (id >= nextWordID) { throw std::out_of_range(std::format("Invalid WordID: {}, size: {}", id, nextWordID)); }
		wordsData.emplace_back(wordsData[id].start, wordsData[id].length);
		return nextWordID++;
	}

	auto getWord(WordID id) const {
		if (id >= nextWordID) { throw std::out_of_range(std::format("Invalid WordID: {}, size: {}", id, nextWordID)); }
		const auto &[start, length] = wordsData[id];
		return std::span{words.data() + start, length};
	}

	auto getWordData(WordID id) const {
		if (id >= nextWordID) { throw std::out_of_range(std::format("Invalid WordID: {}, size: {}", id, nextWordID)); }
		return wordsData[id];
	}

	auto operator[](WordID id) const { return getWord(id); }

	size_t totalLength() const { return words.size(); }
	size_t size() const { return nextWordID; }

	void replaceWithSubstr(WordID id, size_t offset, size_t length) {
		if (id >= nextWordID) { throw std::out_of_range("Invalid WordID"); }
		if (offset + length > wordsData[id].length) { throw std::out_of_range("Substring exceeds word length"); }
		auto &[start, wordLength] = wordsData[id];
		start += offset;
		wordLength = length;
	}

	void clear() {
		words.clear();
		wordsData.clear();
		nextWordID = 0;
	}

	class Iterator {
	   private:
		const WordSet &wordSet;
		WordID		   currentID;

	   public:
		Iterator(const WordSet &ws, WordID id) : wordSet(ws), currentID(id) {}

		bool operator!=(const Iterator &other) const { return currentID != other.currentID; }

		auto operator*() const { return wordSet.getWord(currentID); }

		Iterator &operator++() {
			if (currentID < wordSet.nextWordID) { ++currentID; }
			return *this;
		}
	};

	auto begin() const { return Iterator(*this, 0); }
	auto end() const { return Iterator(*this, nextWordID); }
};

template <symbol Symbol>
class UniqueWordSet {
   public:
	using WordID = unsigned int;

   private:
	using WordData = WordSet<Symbol>::WordData;

	struct Storage {
		std::vector<Symbol>	  words;
		std::vector<WordData> wordsData;

		std::span<const Symbol> get(WordID id) const noexcept {
			const auto &[start, length] = wordsData[id];
			return {words.data() + start, length};
		}
		auto insert(std::span<const Symbol> word) {
			wordsData.emplace_back(words.size(), word.size());
			words.insert(words.end(), word.begin(), word.end());
		}
	};

	struct myHash {
		using is_transparent = void;	 // Allows this hash to be used in unordered_map with std::span<Symbol>
		const Storage *owner;
		constexpr myHash(const Storage *owner) : owner(owner) {}

		constexpr size_t operator()(WordID id) const { return (*this)(owner->get(id)); }
		constexpr size_t operator()(const std::span<Symbol> &span) const {
			return std::hash<std::string_view>()(
				std::string_view(reinterpret_cast<const char *>(span.data()), span.size() * sizeof(Symbol)));
		}
		constexpr size_t operator()(const std::span<const Symbol> &span) const {
			return std::hash<std::string_view>()(
				std::string_view(reinterpret_cast<const char *>(span.data()), span.size() * sizeof(Symbol)));
		}
	};

	struct myEqual {
		using is_transparent = void;	 // Allows this equal to be used in unordered_map with std::span<Symbol>
		const Storage *owner;
		constexpr myEqual(const Storage *owner) : owner(owner) {}

		bool operator()(WordID a, WordID b) const {
			auto &&A = owner->get(a);
			auto &&B = owner->get(b);
			return std::equal(A.begin(), A.end(), B.begin(), B.end());
		}
		template <class V>
		bool operator()(WordID id, const V &b) const {
			auto &&A = owner->get(id);
			return std::equal(A.begin(), A.end(), b.begin(), b.end());
		}
		template <class U>
		bool operator()(const U &a, WordID id) const {
			auto &&B = owner->get(id);
			return std::equal(a.begin(), a.end(), B.begin(), B.end());
		}
		template <class U, class V>
		bool operator()(const U &a, const V &b) const {
			return std::distance(a.begin(), a.end()) == std::distance(b.begin(), b.end()) &&
				   std::equal(a.begin(), a.end(), b.begin(), b.end());
		}
	};

	std::unique_ptr<Storage>							storage = std::make_unique<Storage>();
	std::unordered_map<WordID, WordID, myHash, myEqual> wordMap;

	WordID nextWordID = 0;

   public:
	UniqueWordSet() : storage(std::make_unique<Storage>()), wordMap(0, myHash(storage.get()), myEqual(storage.get())) {
		addWord(std::span<Symbol>{});
	}

	UniqueWordSet(const UniqueWordSet &) = delete;
	UniqueWordSet(UniqueWordSet &&other) noexcept
		: wordMap(0, myHash(other.storage.get()), myEqual(other.storage.get())) {
		storage	   = std::move(other.storage);
		wordMap	   = std::move(other.wordMap);
		nextWordID = std::exchange(other.nextWordID, 0);
	}
	UniqueWordSet &operator=(const UniqueWordSet &) = delete;
	UniqueWordSet &operator=(UniqueWordSet &&other) noexcept {
		storage	   = std::move(other.storage);
		wordMap	   = std::move(other.wordMap);
		nextWordID = std::exchange(other.nextWordID, 0);
		return *this;
	}

	template <class Input>
	WordID addWord(Input &&word) {
		auto it = wordMap.find(std::span{word.begin(), word.end()});
		if (it != wordMap.end()) {
			return it->second;	   // Word already exists, return its ID
		}
		storage->insert(std::span{word.begin(), word.end()});
		WordID id = nextWordID++;	  // increment before inserting because hash and equal do bounds checks
		wordMap.emplace(id, id);
		return id;
	}

	template <class Input>
	bool contains(Input &&word) const {
		return wordMap.find(std::span{word.begin(), word.end()}) != wordMap.end();
	}

	WordID addWord(Symbol *word, size_t length) {
		if (length == 0) return addWord(std::span<Symbol>{});
		auto it = wordMap.find(std::span{word, length});
		if (it != wordMap.end()) {
			return it->second;	   // Word already exists, return its ID
		}
		storage->insert(std::span{word, length});
		WordID id = nextWordID++;	  // increment before inserting because hash and equal do bounds checks
		wordMap.emplace(id, id);
		return id;
	}

	WordID addSubWord(WordID id, size_t offset, size_t length) {
		if (id >= nextWordID) { throw std::out_of_range(std::format("Invalid WordID: {}, size: {}", id, nextWordID)); }
		const auto &[start, wordLength] = storage->wordsData[id];
		if (offset + length > wordLength) { throw std::out_of_range("Substring exceeds word length"); }
		std::span<const Symbol> subWord{storage->words.data() + start + offset, length};
		auto					it = wordMap.find(subWord);
		if (it != wordMap.end()) {
			return it->second;	   // Subword already exists, return its ID
		}
		storage->wordsData.emplace_back(start + offset, length);
		WordID newID = nextWordID++;
		wordMap.emplace(newID, newID);
		return newID;
	}

	auto getWord(WordID id) const {
		if (id >= nextWordID) { throw std::out_of_range(std::format("Invalid WordID: {}, size: {}", id, nextWordID)); }
		return storage->get(id);
	}

	auto operator[](WordID id) const { return getWord(id); }

	size_t totalLength() const { return storage->words.size(); }
	size_t size() const { return nextWordID; }

	void clear() {
		storage->words.clear();
		storage->wordsData.clear();
		wordMap.clear();
		nextWordID = 0;
	}

	auto toWordSet() const {
		WordSet<Symbol> ws{storage->words, storage->wordsData};
		return ws;
	}

	class iterator {
	   private:
		const UniqueWordSet &wordSet;
		WordID				 currentID;

	   public:
		iterator(const UniqueWordSet &ws, WordID id) : wordSet(ws), currentID(id) {}

		bool operator!=(const iterator &other) const { return currentID != other.currentID; }

		auto operator*() const { return wordSet.getWord(currentID); }

		iterator &operator++() {
			if (currentID < wordSet.nextWordID) { ++currentID; }
			return *this;
		}
	};

	auto begin() const { return iterator(*this, 0); }
	auto end() const { return iterator(*this, nextWordID); }

	const UniqueWordSet &serialize(std::ostream &out) const {
		out.write(reinterpret_cast<const char *>(&nextWordID), sizeof(nextWordID));
		out.write(reinterpret_cast<const char *>(storage->wordsData.data()),
				  storage->wordsData.size() * sizeof(WordData));
		out.write(reinterpret_cast<const char *>(storage->words.data()), storage->words.size() * sizeof(Symbol));
		return *this;
	}

	UniqueWordSet(std::istream &in) : UniqueWordSet() {
		in.read(reinterpret_cast<char *>(&nextWordID), sizeof(nextWordID));
		storage->wordsData.resize(nextWordID);
		in.read(reinterpret_cast<char *>(storage->wordsData.data()), storage->wordsData.size() * sizeof(WordData));
		size_t totalLength = 0;
		for (const auto &[start, length] : storage->wordsData) {
			totalLength = std::max(totalLength, start + length);
		}
		storage->words.resize(totalLength);
		in.read(reinterpret_cast<char *>(storage->words.data()), totalLength * sizeof(Symbol));
		wordMap.reserve(nextWordID);
		for (WordID id = 0; id < nextWordID; ++id) {
			wordMap.insert({id, id});
		}
	}
};

template <class Symbol>
class ExtendableWordSet {
	std::vector<std::vector<Symbol>> data;
	struct WordData {
		unsigned int base;
		unsigned int start;
		unsigned int length : 31;
		bool		 canExtend : 1;
	};
	std::vector<WordData> wordsData;

   public:
	using WordID = unsigned int;

	ExtendableWordSet() {
		data.emplace_back();
		wordsData.push_back({0, 0, 0, false});
	}

	auto operator[](size_t id) const {
		if (id >= wordsData.size()) { throw std::out_of_range("Invalid WordID"); }
		const auto &[base, start, length, canExtend] = wordsData[id];
		return std::span{data[base].data() + start, length};
	}

	auto getWord(WordID id) const {
		if (id >= wordsData.size()) { throw std::out_of_range("Invalid WordID"); }
		const auto &[base, start, length, canExtend] = wordsData[id];
		return std::span{data[base].data() + start, length};
	}

	template <class Input>
	WordID addWord(Input &&word) {
		if (word.empty()) return 0;
		data.push_back(std::vector<Symbol>(word.begin(), word.end()));
		wordsData.push_back(
			{static_cast<unsigned int>(data.size() - 1), 0, static_cast<unsigned int>(word.size()), true});
		return wordsData.size() - 1;
	}

	template <class Input>
	WordID extendWord(WordID id, Input &&extension) {
		if (id >= wordsData.size()) { throw std::out_of_range("Invalid WordID"); }
		if (extension.empty()) return id;
		auto &[base, start, length, canExtend] = wordsData[id];
		if (canExtend) {
			data[base].insert(data[base].begin(), extension.begin(), extension.end());
			canExtend = false;
			wordsData.push_back(
				{base, start, static_cast<unsigned int>(data[base].size() - start), true});		// Update length
			return wordsData.size() - 1;
		} else {
			if (length + extension.size() == 0) return 0;
			data.push_back(std::vector<Symbol>(data[base].begin() + start, data[base].begin() + start + length));
			data.back().insert(data.back().end(), extension.begin(), extension.end());
			wordsData.push_back(
				{static_cast<unsigned int>(data.size() - 1), 0, static_cast<unsigned int>(data.back().size()), true});
			return wordsData.size() - 1;
		}
	}

	WordID replaceWithSuffix(WordID id, size_t offset) {
		auto &[base, start, length, canExtend] = wordsData[id];
		if (offset > length) [[unlikely]]
			std::cout << "Warning: shortening word with offset " << offset << " from length " << length << std::endl;
		assert(offset <= length && "cannot shorten word beyond its length");
		if (offset == length) return 0;
		start += offset;
		length -= offset;
		return id;
	}
};
}	  // namespace fl
