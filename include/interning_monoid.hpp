#pragma once

#include <algorithm>
#include <cassert>
#include <compare>
#include <iostream>
#include <memory>
#include <unordered_set>
#include <cstring>
#include <span>
#include <vector>
#include <string_view>
#include <ranges>
#include <iterator>
#include <functional>
#include <cstdint>
#include <format>

#include "dbg.hpp"
#include "concepts.hpp"
#include "formatting.hpp"

namespace fl {

template <class S>
class InterningMonoidElementPrinter;

/// S^* where the type S is a fl::symbol type.
///
/// Essentially a string pool
template <class S>
class InterningMonoid {
   public:
	class WordId {
		uint32_t id;

		constexpr WordId(uint32_t id) : id(id) {}

	   public:
		constexpr WordId() : id(0) {}
		constexpr WordId(const WordId &)			= default;
		constexpr WordId(WordId &&)					= default;
		constexpr WordId &operator=(const WordId &) = default;
		constexpr WordId &operator=(WordId &&)		= default;

		// comparison is stateless
		constexpr bool operator==(const WordId &other) const { return id == other.id; }
		constexpr bool operator!=(const WordId &other) const { return id != other.id; }
		constexpr auto operator<=>(const WordId &other) const { return id <=> other.id; }

		friend class InterningMonoid<S>;
	};

   private:
	struct WordData {
		uint32_t start;
		uint32_t length;
	};

	class Operand;

	struct Storage {
		std::vector<S>		  words;
		std::vector<WordData> wordsData;

		std::vector<S> temporaries;
		uint32_t	   temporaryCount = 0;

		std::span<const S> get(WordId id) const noexcept;
		std::span<const S> get(const Operand &op) const noexcept;
		auto			   insert(std::span<const S> word);

		void createTemporary();
		void deleteTemporary();
	};

	class Operand {
		const InterningMonoid *owner;
		bool				   fromTemporaries;
		friend class InterningMonoid<S>;

	   public:
		uint32_t start;
		uint32_t length;

		Operand() = delete;
		operator WordId() const;
		Operand(const InterningMonoid *owner, uint32_t start, uint32_t length, bool fromTemporaries = false);
		// no move constructor/assignment: Operand is a lightweight index into the shared
		// `temporaries` pool, not an owner of unique storage, so "moving" one is just
		// another live reference to the same slot -- std::move(x) falls back to the copy
		// ctor/assignment below, which increments temporaryCount like any other copy.
		Operand(const Operand &) noexcept;
		~Operand();

		Operand &operator=(const Operand &) noexcept;
		Operand &operator=(WordId id) noexcept;
	};

	struct myHash {
		using is_transparent = void;	 // Allows this hash to be used in unordered_map with std::span<Symbol>
		const Storage *owner;
		constexpr myHash(const Storage *owner) : owner(owner) {}

		constexpr size_t operator()(WordId id) const;
		constexpr size_t operator()(const std::span<S> &span) const;
		constexpr size_t operator()(const std::span<const S> &span) const;
	};

	struct myEqual {
		using is_transparent = void;	 // Allows this equal to be used in unordered_map with std::span<Symbol>
		const Storage *owner;
		constexpr myEqual(const Storage *owner) : owner(owner) {}

		bool operator()(WordId a, WordId b) const;
		template <class V>
		bool operator()(WordId id, const V &b) const;
		template <class U>
		bool operator()(const U &a, WordId id) const;
		template <class U, class V>
		bool operator()(const U &a, const V &b) const;
	};

	mutable std::unique_ptr<Storage>					storage = std::make_unique<Storage>();
	mutable std::unordered_set<WordId, myHash, myEqual> wordMap;
	mutable uint32_t									nextWordId = 0;

	template <std::ranges::viewable_range V>
	WordId addUnique(V &&word) const {
		auto it = wordMap.find(word);
		if (it != wordMap.end()) {
			return *it;		// Word already exists, return its ID
		}
		storage->insert(word);
		WordId id = nextWordId++;	  // increment before inserting because hash and equal do bounds checks
		wordMap.emplace(id);
		return id;
	}

	WordId addUniqueInfix(uint32_t start, uint32_t length) const {
		auto it = wordMap.find(std::span<const S>(storage->words.data() + start, length));
		if (it != wordMap.end()) {
			return *it;		// Infix already exists, return its ID
		}
		storage->wordsData.emplace_back(start, length);
		WordId id = nextWordId++;	  // increment before inserting because hash and equal do bounds checks
		wordMap.emplace(id);
		return id;
	}

	std::span<const S> get(WordId id) const { return storage->get(id); }
	std::span<const S> get(const Operand &op) const { return storage->get(op); }

	/// A.B
	Operand mulImpl(Operand A, Operand B) const {
		uint32_t start = storage->temporaries.size();
		storage->temporaries.reserve(start + A.length + B.length);
		const S *srcA = (A.fromTemporaries ? storage->temporaries.data() : storage->words.data()) + A.start;
		const S *srcB = (B.fromTemporaries ? storage->temporaries.data() : storage->words.data()) + B.start;
		storage->temporaries.insert(storage->temporaries.end(), srcA, srcA + A.length);
		storage->temporaries.insert(storage->temporaries.end(), srcB, srcB + B.length);
		return Operand{this, start, A.length + B.length, true};
	}

	/// A^-1.B
	void checkPrefix(std::span<const S> A, std::span<const S> B) const {
		assert(A.size() <= B.size());
		[[maybe_unused]] bool isPrefix = true;
		for (std::size_t i = 0; i < A.size(); ++i)
			if (A[i] != B[i]) {
				isPrefix = false;
				break;
			}
		if constexpr (dbg::enabled) {
			if (!isPrefix) {
				std::cerr << "A is not a prefix of B";
				if constexpr (OStreamable<S>) {
					std::cerr << ": A = ";
					for (const auto &c : A)
						std::cerr << c;
					std::cerr << ", B = ";
					for (const auto &c : B)
						std::cerr << c;
					std::cerr << " A.size() = " << A.size() << ", B.size() = " << B.size() << std::endl;
				} else {
					std::cerr << " (non-printable symbol type)" << std::endl;
				}
			}
		}
		assert(isPrefix);
	}

	size_t gcp_len(std::span<const S> A, std::span<const S> B) const {
		size_t len = 0;
		while (len < A.size() && len < B.size() && A[len] == B[len]) {
			++len;
		}
		return len;
	}

   public:
	using Value	 = WordId;
	using Symbol = S;

	InterningMonoid()
		: storage(std::make_unique<Storage>()), wordMap(0, myHash(storage.get()), myEqual(storage.get())) {
		addUnique(std::span<Symbol>{});
	}

	static constexpr const Value identity = 0;

	bool equal(Value a, Value b) const { return a.id == b.id; }
	bool equal(Value a, Operand b) const { return myEqual{storage.get()}(get(a), get(b)); }
	bool equal(Operand a, Value b) const { return myEqual{storage.get()}(get(a), get(b)); }
	bool equal(Operand a, Operand b) const { return myEqual{storage.get()}(get(a), get(b)); }

	std::size_t hash(Value a) const { return std::hash<decltype(a.id)>{}(a.id); }

	Operand widen(WordId a) const {
		auto &[s, l] = storage->wordsData[a.id];
		return {this, s, l, false};
	}

	Operand mul(Value a, Value b) const { return mulImpl(widen(a), widen(b)); }
	Operand mul(Operand a, Value b) const { return mulImpl(a, widen(b)); }
	Operand mul(Value a, Operand b) const { return mulImpl(widen(a), b); }
	Operand mul(Operand a, Operand b) const { return mulImpl(a, b); }

	Operand invMul(Value a, Value b) const {
		checkPrefix(get(a), get(b));
		auto &[startA, lengthA] = storage->wordsData[a.id];
		auto &[startB, lengthB] = storage->wordsData[b.id];
		return {this, startB + lengthA, lengthB - lengthA, false};
	}
	Operand invMul(Operand a, Value b) const {
		checkPrefix(get(a), get(b));
		auto &[startB, lengthB] = storage->wordsData[b.id];
		return {this, startB + a.length, lengthB - a.length, false};
	}
	Operand invMul(Value a, Operand b) const {
		checkPrefix(get(a), get(b));
		auto &[startA, lengthA] = storage->wordsData[a.id];
		return {this, b.start + lengthA, b.length - lengthA, b.fromTemporaries};
	}
	Operand invMul(Operand a, Operand b) const {
		checkPrefix(get(a), get(b));
		return {this, b.start + a.length, b.length - a.length, b.fromTemporaries};
	}

	std::span<const S> gen(Value a) const { return get(a); }
	std::span<const S> gen(const Operand &a) const { return get(a); }

	std::size_t size(Value a) const { return storage->wordsData[a.id].length; }
	std::size_t size(const Operand &a) const { return a.length; }

	Operand own(const InterningMonoid &m, Value a) const {
		const auto &data = m.get(a);
		storage->temporaries.insert(storage->temporaries.end(), data.begin(), data.end());
		return {this, static_cast<uint32_t>(storage->temporaries.size() - data.size()),
				static_cast<uint32_t>(data.size()), true};
	}
	Operand own(const InterningMonoid &m, Operand a) const {
		const auto &data = m.get(a);
		storage->temporaries.insert(storage->temporaries.end(), data.begin(), data.end());
		return {this, static_cast<uint32_t>(storage->temporaries.size() - data.size()),
				static_cast<uint32_t>(data.size()), true};
	}

	template <std::ranges::viewable_range V>
	Value from(V &&v) const {
		return addUnique(v);
	}

	Operand sub(Value a, std::size_t start, std::size_t length) const {
		auto &[startA, lengthA] = storage->wordsData[a.id];
		assert(start + length <= lengthA);
		return {this, startA + static_cast<uint32_t>(start), static_cast<uint32_t>(length), false};
	}
	Operand sub(Operand a, std::size_t start, std::size_t length) const {
		assert(start + length <= a.length);
		return {this, a.start + static_cast<uint32_t>(start), static_cast<uint32_t>(length), a.fromTemporaries};
	}

	auto gcp(auto a, auto b) const {
		const auto &A	= get(a);
		const auto &B	= get(b);
		size_t		len = gcp_len(A, B);
		return sub(a, 0, len);
	}

	std::size_t C() const {
		std::size_t maxLength = 0;
		for (const auto &[start, length] : storage->wordsData)
			maxLength = std::max(maxLength, static_cast<std::size_t>(length));
		return maxLength;
	}

	InterningMonoidElementPrinter<S> p(WordId id) const { return {this, get(id)}; }
	InterningMonoidElementPrinter<S> p(const Operand &id) const { return {this, get(id)}; }

	// ---------------- methods down from here are not required by the concepts

	Value from(const char *v) const
		requires std::same_as<char, S>
	{
		return addUnique(std::span<const char>(v, std::strlen(v)));
	}

	uint32_t	temporaryCount() const { return storage->temporaryCount; }
	uint32_t	totalWordCount() const { return wordMap.size(); }
	std::size_t poolByteCount() const { return storage->words.size() * sizeof(S); }

	/// Reclaims the storage of elements that are not in the given ranges, and renumbers
	/// every surviving WordId into a dense id space (no dead entries left in wordsData).
	/// Since ids move, the new id is written back into every element of every range given
	/// -- each range must therefore be writable, not just readable.
	template <std::ranges::forward_range... Ranges>
		requires((std::same_as<std::ranges::range_value_t<Ranges>, Value> &&
				  std::indirectly_writable<std::ranges::iterator_t<Ranges>, Value>) &&
				 ...)
	void compact(Ranges &&...liveRanges) const {
		std::vector<bool> live(nextWordId, false);
		live[identity.id] = true;	  // the empty word is always kept
		auto markLive	  = [&](const auto &range) {
			for (const Value &v : range)
				live[v.id] = true;
		};
		(markLive(liveRanges), ...);

		// Assign dense new ids in ascending original-id order: id 0 (the empty word,
		// always live) is always encountered first, so it stays id 0.
		std::vector<uint32_t> oldToNew(nextWordId, ~0u);
		std::vector<uint32_t> liveIds;
		liveIds.reserve(nextWordId);
		for (uint32_t id = 0; id < nextWordId; ++id) {
			if (live[id]) {
				oldToNew[id] = static_cast<uint32_t>(liveIds.size());
				liveIds.push_back(id);
			}
		}
		uint32_t liveCount = static_cast<uint32_t>(liveIds.size());

		// Sort (a copy of) the live ids by their ORIGINAL offset so that words whose byte
		// ranges overlap or nest -- e.g. an InfixId created by invMul, which reuses a
		// suffix of another word's bytes rather than owning independent storage -- become
		// adjacent and can be coalesced into a single physical copy, instead of each being
		// duplicated independently (which would silently undo that sharing).
		std::vector<uint32_t> byStart = liveIds;
		std::ranges::sort(byStart, {}, [&](uint32_t id) { return storage->wordsData[id].start; });

		std::vector<S> newWords;
		newWords.reserve(storage->words.size());
		uint32_t runOldStart = 0, runOldEnd = 0, runNewStart = 0;
		bool	 haveRun = false;
		for (uint32_t id : byStart) {
			auto &[start, length] = storage->wordsData[id];
			uint32_t end		  = start + length;
			if (!haveRun || start > runOldEnd) {
				// starts a fresh, disjoint run
				runOldStart = start;
				runNewStart = newWords.size();
				newWords.insert(newWords.end(), storage->words.begin() + start, storage->words.begin() + end);
				runOldEnd = end;
				haveRun	  = true;
			} else if (end > runOldEnd) {
				// overlaps the current run but extends past it -- copy only the uncovered tail
				newWords.insert(newWords.end(), storage->words.begin() + runOldEnd, storage->words.begin() + end);
				runOldEnd = end;
			}
			// else: fully contained in the current run already -- nothing to copy
			start = runNewStart + (start - runOldStart);
		}
		storage->words = std::move(newWords);

		// Re-key wordsData by the new dense id, dropping every dead entry.
		std::vector<WordData> newWordsData(liveCount);
		for (uint32_t id : liveIds)
			newWordsData[oldToNew[id]] = storage->wordsData[id];
		storage->wordsData = std::move(newWordsData);
		nextWordId		   = liveCount;

		// content -> id lookup must be rebuilt from scratch: every id it held is stale now.
		wordMap.clear();
		for (uint32_t id = 0; id < nextWordId; ++id)
			wordMap.emplace(WordId(id));

		// write the new ids back so callers' handles stay valid.
		auto remap = [&](auto &range) {
			for (Value &v : range) {
				assert(oldToNew[v.id] != ~0u && "a value outside the given live ranges was live-marked");
				v = WordId(oldToNew[v.id]);
			}
		};
		(remap(liveRanges), ...);
	}

	const InterningMonoid &serialize(std::ostream &out) const {
		assert(storage->temporaryCount == 0 &&
			   "cannot serialize an InterningMonoid while a mul()/invMul() result (TemporaryId/InfixId) is still "
			   "outstanding -- convert it to a Value first");
		out.write(reinterpret_cast<const char *>(&nextWordId), sizeof(nextWordId));
		out.write(reinterpret_cast<const char *>(storage->wordsData.data()),
				  storage->wordsData.size() * sizeof(WordData));
		out.write(reinterpret_cast<const char *>(storage->words.data()), storage->words.size() * sizeof(S));
		return *this;
	}

	explicit InterningMonoid(std::istream &in)
		: storage(std::make_unique<Storage>()), wordMap(0, myHash(storage.get()), myEqual(storage.get())) {
		in.read(reinterpret_cast<char *>(&nextWordId), sizeof(nextWordId));
		storage->wordsData.resize(nextWordId);
		in.read(reinterpret_cast<char *>(storage->wordsData.data()), storage->wordsData.size() * sizeof(WordData));

		uint32_t totalLength = 0;
		for (const auto &[start, length] : storage->wordsData)
			totalLength = std::max(totalLength, start + length);
		storage->words.resize(totalLength);
		in.read(reinterpret_cast<char *>(storage->words.data()), totalLength * sizeof(S));

		// rebuild the content -> id lookup table. Any dead id left pointing
		// at the empty word by a prior compact() collapses harmlessly onto
		// the real identity entry via content-based dedup (whichever id
		// resolves to "" is inserted first -- id 0 always does, since the
		// loop runs in ascending order), so it's safe to just walk every id.
		wordMap.reserve(nextWordId);
		for (uint32_t id = 0; id < nextWordId; ++id)
			wordMap.emplace(WordId(id));
	}
};

template <class S>
InterningMonoid<S>::Operand::Operand(const InterningMonoid *owner, uint32_t start, uint32_t length,
									 bool fromTemporaries)
	: owner(owner), fromTemporaries(fromTemporaries), start(start), length(length) {
	if (fromTemporaries) { owner->storage->createTemporary(); }
}

template <class S>
InterningMonoid<S>::Operand::Operand(const Operand &other) noexcept
	: owner(other.owner), fromTemporaries(other.fromTemporaries), start(other.start), length(other.length) {
	if (fromTemporaries) { owner->storage->createTemporary(); }
}

template <class S>
InterningMonoid<S>::Operand::~Operand() {
	if (fromTemporaries) { owner->storage->deleteTemporary(); }
}

template <class S>
InterningMonoid<S>::Operand::operator WordId() const {
	if (fromTemporaries) {
		return owner->addUnique(std::span<const S>(owner->storage->temporaries.data() + start, length));
	} else {
		return owner->addUniqueInfix(start, length);
	}
}

template <class S>
InterningMonoid<S>::Operand &InterningMonoid<S>::Operand::operator=(const Operand &rhs) noexcept {
	if (this == &rhs) return *this;
	assert(owner == rhs.owner && "cannot assign Operands from different InterningMonoids");
	if (fromTemporaries) { owner->storage->deleteTemporary(); }
	start			= rhs.start;
	length			= rhs.length;
	fromTemporaries = rhs.fromTemporaries;
	if (fromTemporaries) { owner->storage->createTemporary(); }
	return *this;
}
template <class S>
InterningMonoid<S>::Operand &InterningMonoid<S>::Operand::operator=(WordId id) noexcept {
	if (fromTemporaries) { owner->storage->deleteTemporary(); }
	auto &[start, length] = owner->storage->wordsData[id.id];
	this->start			  = start;
	this->length		  = length;
	fromTemporaries		  = false;
	return *this;
}

template <class S>
std::span<const S> InterningMonoid<S>::Storage::get(WordId id) const noexcept {
	const auto &[start, length] = wordsData[id.id];
	return {words.data() + start, length};
}

template <class S>
std::span<const S> InterningMonoid<S>::Storage::get(const Operand &op) const noexcept {
	if (op.fromTemporaries) {
		return {temporaries.data() + op.start, op.length};
	} else {
		return {words.data() + op.start, op.length};
	}
}

template <class S>
auto InterningMonoid<S>::Storage::insert(std::span<const S> word) {
	wordsData.emplace_back(words.size(), word.size());
	words.insert(words.end(), word.begin(), word.end());
}
template <class S>
void InterningMonoid<S>::Storage::createTemporary() {
	++temporaryCount;
}
template <class S>
void InterningMonoid<S>::Storage::deleteTemporary() {
	assert(temporaryCount > 0);
	--temporaryCount;
	if (temporaryCount == 0) { temporaries.clear(); }
}

template <class S>
constexpr size_t InterningMonoid<S>::myHash::operator()(WordId id) const {
	return (*this)(owner->get(id));
}
template <class S>
constexpr size_t InterningMonoid<S>::myHash::operator()(const std::span<S> &span) const {
	return std::hash<std::string_view>()(
		std::string_view(reinterpret_cast<const char *>(span.data()), span.size() * sizeof(S)));
}
template <class S>
constexpr size_t InterningMonoid<S>::myHash::operator()(const std::span<const S> &span) const {
	return std::hash<std::string_view>()(
		std::string_view(reinterpret_cast<const char *>(span.data()), span.size() * sizeof(S)));
}

template <class S>
bool InterningMonoid<S>::myEqual::operator()(WordId a, WordId b) const {
	auto &&A = owner->get(a);
	auto &&B = owner->get(b);
	return std::equal(A.begin(), A.end(), B.begin(), B.end());
}
template <class S>
template <class V>
bool InterningMonoid<S>::myEqual::operator()(WordId id, const V &b) const {
	auto &&A = owner->get(id);
	return std::equal(A.begin(), A.end(), b.begin(), b.end());
}
template <class S>
template <class U>
bool InterningMonoid<S>::myEqual::operator()(const U &a, WordId id) const {
	auto &&B = owner->get(id);
	return std::equal(a.begin(), a.end(), B.begin(), B.end());
}

template <class S>
template <class U, class V>
bool InterningMonoid<S>::myEqual::operator()(const U &a, const V &b) const {
	return std::distance(a.begin(), a.end()) == std::distance(b.begin(), b.end()) &&
		   std::equal(a.begin(), a.end(), b.begin(), b.end());
}

template <class S>
class InterningMonoidElementPrinter {
	const InterningMonoid<S> *monoid;
	const std::span<const S>  word;

   public:
	InterningMonoidElementPrinter(const InterningMonoid<S> *monoid, const std::span<const S> &word)
		: monoid(monoid), word(word) {}
	friend std::ostream &operator<<(std::ostream &os, const InterningMonoidElementPrinter &p) {
		if constexpr (debug_printable_symbol<S>) {
			for (const auto &c : p.word)
				c.debug_print(os);
		} else if constexpr (printable_symbol<S>) {
			for (const auto &c : p.word)
				os << c;
		} else {
			os << "\"unprintable\"";
		}
		return os;
	}
};

};	   // namespace fl

template <class S>
struct std::formatter<fl::InterningMonoidElementPrinter<S>> : fl::ostream_formatter {};
