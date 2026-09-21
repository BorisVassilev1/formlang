#pragma once

#include <cassert>
#include <concepts.hpp>
#include <iostream>
#include <memory>
#include <unordered_set>
#include <cstring>

namespace fl {

/// S^* where the type S is a fl::symbol type.
///
/// Essentially a string pool
template <class S>
class KleeneMonoid {
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

		friend class KleeneMonoid<S>;
	};

   private:
	struct WordData {
		uint32_t start;
		uint32_t length;
	};

	/// is in the temporaries vector
	struct TemporaryId {
		const KleeneMonoid<S> *owner;
		uint32_t			   start;
		uint32_t			   length;
		TemporaryId(const KleeneMonoid<S> *owner, uint32_t start, uint32_t length);
		TemporaryId(const TemporaryId &other) noexcept;
		TemporaryId(TemporaryId &&other) noexcept;
		TemporaryId &operator=(const TemporaryId &other) noexcept;
		TemporaryId &operator=(TemporaryId &&other) noexcept;

		~TemporaryId();

		operator WordId() const;
	};

	/// is an infix of a word in the pool itself
	struct InfixId {
		const KleeneMonoid<S> *owner;
		uint32_t			   start;
		uint32_t			   length;

		operator WordId() const;
	};

	struct Storage {
		std::vector<S>		  words;
		std::vector<WordData> wordsData;

		std::vector<S> temporaries;
		uint32_t	   temporaryCount = 0;

		std::span<const S> get(WordId id) const noexcept;
		std::span<const S> get(const TemporaryId &id) const noexcept;
		std::span<const S> get(const InfixId &id) const noexcept;
		auto			   insert(std::span<const S> word);

		void createTemporary();
		void deleteTemporary();
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
	std::span<const S> get(const TemporaryId &id) const { return storage->get(id); }
	std::span<const S> get(const InfixId &id) const { return storage->get(id); }

	/// A reference to an operand of mul(), by index rather than by pointer: an
	/// operand that lives in storage->temporaries can't be a stable
	/// std::span, since that vector may still grow. An index into it, on the
	/// other hand, stays meaningful across reallocation, so we resolve the
	/// actual pointer only once no more growth can happen (see mul() below).
	struct Operand {
		bool	 fromTemporaries;
		uint32_t start;
		uint32_t length;
	};

	Operand operand(WordId a) const {
		auto &[s, l] = storage->wordsData[a.id];
		return {false, s, l};
	}
	Operand operand(const TemporaryId &a) const { return {true, a.start, a.length}; }
	Operand operand(const InfixId &a) const { return {false, a.start, a.length}; }

	/// A.B
	TemporaryId mul(Operand A, Operand B) const {
		uint32_t start = storage->temporaries.size();
		storage->temporaries.reserve(start + A.length + B.length);
		const S *srcA = (A.fromTemporaries ? storage->temporaries.data() : storage->words.data()) + A.start;
		const S *srcB = (B.fromTemporaries ? storage->temporaries.data() : storage->words.data()) + B.start;
		storage->temporaries.insert(storage->temporaries.end(), srcA, srcA + A.length);
		storage->temporaries.insert(storage->temporaries.end(), srcB, srcB + B.length);
		return {this, start, A.length + B.length};
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
		assert(isPrefix);
	}

   public:
	using Value	 = WordId;
	using Symbol = S;

	KleeneMonoid() : storage(std::make_unique<Storage>()), wordMap(0, myHash(storage.get()), myEqual(storage.get())) {
		addUnique(std::span<Symbol>{});
	}

	static constexpr const Value identity = 0;

	bool equal(Value a, Value b) const { return a.id == b.id; }
	bool equal(Value a, TemporaryId b) const { return myEqual{storage.get()}(get(a), get(b)); }
	bool equal(TemporaryId a, Value b) const { return myEqual{storage.get()}(get(a), get(b)); }
	bool equal(TemporaryId a, TemporaryId b) const { return myEqual{storage.get()}(get(a), get(b)); }
	bool equal(Value a, InfixId b) const { return myEqual{storage.get()}(get(a), get(b)); }
	bool equal(InfixId a, Value b) const { return myEqual{storage.get()}(get(a), get(b)); }
	bool equal(TemporaryId a, InfixId b) const { return myEqual{storage.get()}(get(a), get(b)); }
	bool equal(InfixId a, TemporaryId b) const { return myEqual{storage.get()}(get(a), get(b)); }
	bool equal(InfixId a, InfixId b) const { return myEqual{storage.get()}(get(a), get(b)); }

	std::size_t hash(Value a) const { return std::hash<decltype(a.id)>{}(a.id); }

	TemporaryId mul(Value a, Value b) const { return mul(operand(a), operand(b)); }
	TemporaryId mul(TemporaryId a, Value b) const { return mul(operand(a), operand(b)); }
	TemporaryId mul(Value a, TemporaryId b) const { return mul(operand(a), operand(b)); }
	TemporaryId mul(TemporaryId a, TemporaryId b) const { return mul(operand(a), operand(b)); }
	TemporaryId mul(InfixId a, Value b) const { return mul(operand(a), operand(b)); }
	TemporaryId mul(Value a, InfixId b) const { return mul(operand(a), operand(b)); }
	TemporaryId mul(InfixId a, TemporaryId b) const { return mul(operand(a), operand(b)); }
	TemporaryId mul(TemporaryId a, InfixId b) const { return mul(operand(a), operand(b)); }
	TemporaryId mul(InfixId a, InfixId b) const { return mul(operand(a), operand(b)); }

	InfixId invMul(Value a, Value b) const {
		checkPrefix(get(a), get(b));
		auto &[startA, lengthA] = storage->wordsData[a.id];
		auto &[startB, lengthB] = storage->wordsData[b.id];
		return {this, startB + lengthA, lengthB - lengthA};
	}
	InfixId invMul(TemporaryId a, Value b) const {
		checkPrefix(get(a), get(b));
		auto &[startB, lengthB] = storage->wordsData[b.id];
		return {this, startB + a.length, lengthB - a.length};
	}
	TemporaryId invMul(Value a, TemporaryId b) const {
		checkPrefix(get(a), get(b));
		auto &[startA, lengthA] = storage->wordsData[a.id];
		return {this, b.start + lengthA, b.length - lengthA};
	}
	TemporaryId invMul(TemporaryId a, TemporaryId b) const {
		checkPrefix(get(a), get(b));
		return {this, b.start + a.length, b.length - a.length};
	}
	InfixId invMul(InfixId a, Value b) const {
		checkPrefix(get(a), get(b));
		auto &[startB, lengthB] = storage->wordsData[b.id];
		return {this, startB + a.length, lengthB - a.length};
	}
	InfixId invMul(Value a, InfixId b) const {
		checkPrefix(get(a), get(b));
		auto &[startA, lengthA] = storage->wordsData[a.id];
		return {this, b.start + lengthA, b.length - lengthA};
	}
	TemporaryId invMul(InfixId a, TemporaryId b) const {
		checkPrefix(get(a), get(b));
		return {this, b.start + a.length, b.length - a.length};
	}
	InfixId invMul(TemporaryId a, InfixId b) const {
		checkPrefix(get(a), get(b));
		return {this, b.start + a.length, b.length - a.length};
	}
	InfixId invMul(InfixId a, InfixId b) const {
		checkPrefix(get(a), get(b));
		return {this, b.start + a.length, b.length - a.length};
	}

	std::span<const S> gen(Value a) const { return get(a); }
	std::span<const S> gen(const TemporaryId &a) const { return get(a); }
	std::span<const S> gen(const InfixId &a) const { return get(a); }

	Value own(const KleeneMonoid &m, Value a) const { return addUnique(m.get(a));}


	// ---------------- methods down from here are not required by the concepts 

	template <std::ranges::viewable_range V>
	Value create(V &&v) const {
		return addUnique(v);
	}

	Value create(const char *v) const
		requires std::same_as<char, S>
	{
		return addUnique(std::span<const char>(v, std::strlen(v)));
	}

	uint32_t temporaryCount() const { return storage->temporaryCount; }
	uint32_t totalWordCount() const { return nextWordId; }
};

template <class S>
KleeneMonoid<S>::TemporaryId::TemporaryId(const KleeneMonoid<S> *owner, uint32_t start, uint32_t length)
	: owner(owner), start(start), length(length) {
	owner->storage->createTemporary();
}
template <class S>
KleeneMonoid<S>::TemporaryId::TemporaryId(const TemporaryId &other) noexcept
	: owner(other.owner), start(other.start), length(other.length) {
	owner->storage->createTemporary();
}
template <class S>
KleeneMonoid<S>::TemporaryId::TemporaryId(TemporaryId &&other) noexcept
	: owner(other.owner), start(other.start), length(other.length) {
	owner->storage->createTemporary();
}

template <class S>
KleeneMonoid<S>::TemporaryId &KleeneMonoid<S>::TemporaryId::operator=(const TemporaryId &other) noexcept {
	assert(owner == other.owner);
	owner  = other.owner;
	start  = other.start;
	length = other.length;
	return *this;
}

template <class S>
KleeneMonoid<S>::TemporaryId &KleeneMonoid<S>::TemporaryId::operator=(TemporaryId &&other) noexcept {
	assert(owner == other.owner);
	owner  = other.owner;
	start  = other.start;
	length = other.length;
	return *this;
}

template <class S>
KleeneMonoid<S>::TemporaryId::~TemporaryId() {
	owner->storage->deleteTemporary();
}

template <class S>
KleeneMonoid<S>::TemporaryId::operator WordId() const {
	return owner->addUnique(std::span<const S>(owner->storage->temporaries.data() + start, length));
}

template <class S>
KleeneMonoid<S>::InfixId::operator WordId() const {
	return owner->addUniqueInfix(start, length);
}

template <class S>
std::span<const S> KleeneMonoid<S>::Storage::get(WordId id) const noexcept {
	const auto &[start, length] = wordsData[id.id];
	return {words.data() + start, length};
}

template <class S>
std::span<const S> KleeneMonoid<S>::Storage::get(const TemporaryId &id) const noexcept {
	return {temporaries.data() + id.start, id.length};
}

template <class S>
std::span<const S> KleeneMonoid<S>::Storage::get(const InfixId &id) const noexcept {
	return {words.data() + id.start, id.length};
}

template <class S>
auto KleeneMonoid<S>::Storage::insert(std::span<const S> word) {
	wordsData.emplace_back(words.size(), word.size());
	words.insert(words.end(), word.begin(), word.end());
}
template <class S>
void KleeneMonoid<S>::Storage::createTemporary() {
	++temporaryCount;
}
template <class S>
void KleeneMonoid<S>::Storage::deleteTemporary() {
	assert(temporaryCount > 0);
	--temporaryCount;
	if (temporaryCount == 0) { temporaries.clear(); }
}

template <class S>
constexpr size_t KleeneMonoid<S>::myHash::operator()(WordId id) const {
	return (*this)(owner->get(id));
}
template <class S>
constexpr size_t KleeneMonoid<S>::myHash::operator()(const std::span<S> &span) const {
	return std::hash<std::string_view>()(
		std::string_view(reinterpret_cast<const char *>(span.data()), span.size() * sizeof(S)));
}
template <class S>
constexpr size_t KleeneMonoid<S>::myHash::operator()(const std::span<const S> &span) const {
	return std::hash<std::string_view>()(
		std::string_view(reinterpret_cast<const char *>(span.data()), span.size() * sizeof(S)));
}

template <class S>
bool KleeneMonoid<S>::myEqual::operator()(WordId a, WordId b) const {
	auto &&A = owner->get(a);
	auto &&B = owner->get(b);
	return std::equal(A.begin(), A.end(), B.begin(), B.end());
}
template <class S>
template <class V>
bool KleeneMonoid<S>::myEqual::operator()(WordId id, const V &b) const {
	auto &&A = owner->get(id);
	return std::equal(A.begin(), A.end(), b.begin(), b.end());
}
template <class S>
template <class U>
bool KleeneMonoid<S>::myEqual::operator()(const U &a, WordId id) const {
	auto &&B = owner->get(id);
	return std::equal(a.begin(), a.end(), B.begin(), B.end());
}

template <class S>
template <class U, class V>
bool KleeneMonoid<S>::myEqual::operator()(const U &a, const V &b) const {
	return std::distance(a.begin(), a.end()) == std::distance(b.begin(), b.end()) &&
		   std::equal(a.begin(), a.end(), b.begin(), b.end());
}
};	   // namespace fl

static_assert(fl::monoid<fl::KleeneMonoid<char>>);
static_assert(fl::free_monoid<fl::KleeneMonoid<char>>);
