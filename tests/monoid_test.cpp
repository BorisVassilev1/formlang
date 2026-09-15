#include <cassert>
#include <iostream>
#include "KleeneMonoid.hpp"

// int main() {
//	fl::KleeneMonoid<char> sigma_star;
//	using Value = decltype(sigma_star)::Value;
//
//	Value v1 = sigma_star.create("asdf");
//
//	Value v2 = sigma_star.create("as");
//	Value v3 = sigma_star.create("df");
//	Value v4 = sigma_star.mul(v2, v3);
//
//	assert(sigma_star.equal(v1, v4));
//	assert(!sigma_star.equal(v1, v3));
//
//	assert(sigma_star.temporaryCount() == 0);
//
//	{
//		auto temp = sigma_star.mul(v2, v3);
//		assert(sigma_star.temporaryCount() == 1);
//	}
//
//	assert(sigma_star.temporaryCount() == 0);
//
//	std::cout << "test passed" << std::endl;
//
// }

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "../doctest.h"

#include "KleeneMonoid.hpp"

#include <array>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using M	 = fl::KleeneMonoid<char>;
using Id = M::Value;

/// Copy a word out of the pool. Never hold a span across a create/mul call:
/// the underlying vector can reallocate.
std::string word(const M &m, Id id) {
	auto s = m.gen(id);
	return std::string(s.begin(), s.end());
}

/// Same as word(), but through gen() directly, so it works for TemporaryId
/// and InfixId too, without interning them as a side effect.
template <class T>
std::string peek(const M &m, const T &id) {
	auto s = m.gen(id);
	return std::string(s.begin(), s.end());
}

std::span<const char> bytes(const std::string &s) { return std::span<const char>(s.data(), s.size()); }

}	 // namespace

// ---------------------------------------------------------------------------
// Construction and the identity element
// ---------------------------------------------------------------------------

TEST_SUITE("KleeneMonoid / identity") {

	TEST_CASE("a fresh monoid holds exactly the empty word") {
		M m;
		CHECK(m.totalWordCount() == 1);
		CHECK(m.temporaryCount() == 0);
		CHECK(m.gen(M::identity).empty());
	}

	TEST_CASE("the empty word interns to the identity") {
		M m;
		CHECK(m.equal(m.create(""), M::identity));
		CHECK(m.equal(m.create(std::span<const char>{}), M::identity));
		CHECK(m.totalWordCount() == 1);
	}

	TEST_CASE("identity is a two-sided unit") {
		M  m;
		Id a  = m.create("abc");
		Id la = m.mul(M::identity, a);
		Id ra = m.mul(a, M::identity);
		CHECK(m.equal(la, a));
		CHECK(m.equal(ra, a));
		CHECK(m.equal(Id(m.mul(M::identity, M::identity)), M::identity));
	}
}

// ---------------------------------------------------------------------------
// create / interning
// ---------------------------------------------------------------------------

TEST_SUITE("KleeneMonoid / create") {

	TEST_CASE("equal words share an id, distinct words do not") {
		M  m;
		Id a1 = m.create("abc");
		Id a2 = m.create("abc");
		Id b  = m.create("abd");

		CHECK(m.equal(a1, a2));
		CHECK_FALSE(m.equal(a1, b));
		CHECK(m.totalWordCount() == 3);	   // identity, abc, abd
	}

	TEST_CASE("gen round-trips the word") {
		M m;
		CHECK(word(m, m.create("abc")) == "abc");
		CHECK(word(m, m.create("x")) == "x");
		CHECK(word(m, m.create("")).empty());
	}

	TEST_CASE("prefixes and suffixes are separate words") {
		M  m;
		Id ab  = m.create("ab");
		Id abc = m.create("abc");
		Id bc  = m.create("bc");
		Id a   = m.create("a");

		CHECK_FALSE(m.equal(ab, abc));
		CHECK_FALSE(m.equal(bc, abc));
		CHECK_FALSE(m.equal(a, ab));
		CHECK(m.totalWordCount() == 5);
		CHECK(word(m, ab) == "ab");
		CHECK(word(m, bc) == "bc");
	}

	TEST_CASE("the const char* overload stops at the NUL terminator") {
		M m;
		CHECK(m.gen(m.create("abc")).size() == 3);
	}

	TEST_CASE("the range overload keeps embedded NULs") {
		M		   m;
		const char raw[] = {'a', '\0', 'b'};
		Id		   a	 = m.create(std::span<const char>(raw, 3));
		CHECK(m.gen(a).size() == 3);
		CHECK_FALSE(m.equal(a, m.create("a")));
	}

	TEST_CASE("accepts contiguous ranges other than span") {
		M				  m;
		const std::string s = "hello";
		std::vector<char> v = {'h', 'e', 'l', 'l', 'o'};

		Id from_string = m.create(bytes(s));
		Id from_vector = m.create(std::span<const char>(v.data(), v.size()));
		CHECK(m.equal(from_string, from_vector));
		CHECK(word(m, from_string) == "hello");
	}
}

// ---------------------------------------------------------------------------
// hash
// ---------------------------------------------------------------------------

TEST_SUITE("KleeneMonoid / hash") {

	TEST_CASE("hash is stable for equal ids") {
		M  m;
		Id a1 = m.create("abc");
		Id a2 = m.create("abc");
		Id b  = m.create("abd");
		CHECK(m.hash(a1) == m.hash(a2));
		CHECK(m.hash(a1) != m.hash(b));
	}

	TEST_CASE("hash of the identity is stable across instances") {
		M m1, m2;
		CHECK(m1.hash(M::identity) == m2.hash(M::identity));
	}
}

// ---------------------------------------------------------------------------
// mul
// ---------------------------------------------------------------------------

TEST_SUITE("KleeneMonoid / mul") {

	TEST_CASE("mul concatenates") {
		M  m;
		Id a  = m.create("abc");
		Id b  = m.create("def");
		Id ab = m.mul(a, b);
		CHECK(word(m, ab) == "abcdef");
		CHECK(word(m, Id(m.mul(b, a))) == "defabc");
	}

	// Reads a TemporaryId without going through WordId, so it is the narrowest
	// check on Storage::get(const TemporaryId&).
	TEST_CASE("equal reads a temporary's own contents") {
		M  m;
		Id a = m.create("ab");
		Id b = m.create("cd");

		CHECK(m.equal(m.mul(a, b), m.mul(a, b)));
		CHECK_FALSE(m.equal(m.mul(a, b), m.mul(b, a)));
		CHECK(m.equal(m.create("abcd"), m.mul(a, b)));
		CHECK_FALSE(m.equal(m.create("abce"), m.mul(a, b)));
	}

	TEST_CASE("mul is associative") {
		M  m;
		Id a = m.create("ab");
		Id b = m.create("cd");
		Id c = m.create("ef");

		Id left	 = m.mul(Id(m.mul(a, b)), c);
		Id right = m.mul(a, Id(m.mul(b, c)));
		CHECK(m.equal(left, right));
		CHECK(word(m, left) == "abcdef");
	}

	TEST_CASE("a product interns to the pre-existing word") {
		M  m;
		Id a	  = m.create("abc");
		Id b	  = m.create("def");
		Id abcdef = m.create("abcdef");

		uint32_t before = m.totalWordCount();
		Id		 prod	= m.mul(a, b);
		CHECK(m.equal(prod, abcdef));
		CHECK(m.totalWordCount() == before);	// no new entry
	}

	TEST_CASE("a new product adds exactly one word") {
		M		 m;
		Id		 a		= m.create("abc");
		Id		 b		= m.create("def");
		uint32_t before = m.totalWordCount();
		Id		 prod	= m.mul(a, b);
		CHECK(m.totalWordCount() == before + 1);
		CHECK(word(m, prod) == "abcdef");
	}

	TEST_CASE("mul accepts temporary operands") {
		M  m;
		Id a = m.create("ab");
		Id b = m.create("cd");
		Id c = m.create("ef");

		CHECK(word(m, Id(m.mul(m.mul(a, b), c))) == "abcdef");
		CHECK(word(m, Id(m.mul(a, m.mul(b, c)))) == "abcdef");
		CHECK(word(m, Id(m.mul(m.mul(a, b), m.mul(c, a)))) == "abcdefab");
	}

	// mul(A, B) appends A and B to Storage::temporaries; when the operands are
	// themselves temporaries that is a self-referencing range insert. This
	// forces the scratch buffer to grow mid-operation.
	TEST_CASE("mul of two temporaries survives scratch-buffer growth") {
		M				  m;
		const std::string big(4096, 'q');
		Id				  a = m.create(bytes(big));
		Id				  b = m.create("tail");

		for (int i = 0; i < 8; ++i) {
			CAPTURE(i);
			auto prod = m.mul(m.mul(a, b), m.mul(a, b));
			Id	 id	  = prod;
			CHECK(m.gen(id).size() == 2 * (big.size() + 4));
			CHECK(word(m, id) == big + "tail" + big + "tail");
		}
	}

	TEST_CASE("mul is idempotent on repeated products") {
		M  m;
		Id a  = m.create("xy");
		Id p1 = m.mul(a, a);
		Id p2 = m.mul(a, a);
		CHECK(m.equal(p1, p2));
		CHECK(word(m, p1) == "xyxy");
	}
}

// ---------------------------------------------------------------------------
// invMul
// ---------------------------------------------------------------------------

TEST_SUITE("KleeneMonoid / invMul") {

	TEST_CASE("invMul(Value, Value) strips the prefix") {
		M  m;
		Id a	  = m.create("abc");
		Id abcdef = m.create("abcdef");
		Id suffix = m.invMul(a, abcdef);
		CHECK(word(m, suffix) == "def");
		CHECK(m.equal(suffix, m.create("def")));
	}

	TEST_CASE("an infix that is already interned does not get a second id") {
		M		 m;
		Id		 a		= m.create("abc");
		Id		 abcdef = m.create("abcdef");
		Id		 def	= m.create("def");
		uint32_t before = m.totalWordCount();
		Id		 suffix = m.invMul(a, abcdef);
		CHECK(m.equal(suffix, def));
		CHECK(m.totalWordCount() == before);
	}

	TEST_CASE("invMul(TemporaryId, Value)") {
		M  m;
		Id a	  = m.create("abc");
		Id b	  = m.create("de");
		Id abcdef = m.create("abcdef");
		Id rest	  = m.invMul(m.mul(a, b), abcdef);
		CHECK(word(m, rest) == "f");
	}

	TEST_CASE("invMul(Value, TemporaryId) strips the prefix") {
		M  m;
		Id a = m.create("abc");
		Id b = m.create("def");

		Id suffix = m.invMul(a, m.mul(a, b));
		CHECK(word(m, suffix) == "def");
		CHECK(m.equal(suffix, b));
	}

	TEST_CASE("invMul(TemporaryId, TemporaryId) strips the prefix") {
		M  m;
		Id a = m.create("ab");
		Id b = m.create("cd");
		Id c = m.create("ef");

		// (ab.cd)^-1 . (ab.cd.ef) == ef
		Id rest = m.invMul(m.mul(a, b), m.mul(Id(m.mul(a, b)), c));
		CHECK(word(m, rest) == "ef");
	}

	TEST_CASE("invMul by the identity is a no-op") {
		M  m;
		Id a	 = m.create("abc");
		Id b	 = m.create("d");
		Id whole = m.invMul(M::identity, m.mul(a, b));
		CHECK(word(m, whole) == "abcd");
	}

	TEST_CASE("invMul by the whole word yields the identity") {
		M  m;
		Id a	 = m.create("abc");
		Id b	 = m.create("def");
		Id empty = m.invMul(Id(m.mul(a, b)), m.mul(a, b));
		CHECK(m.equal(empty, M::identity));
	}

	TEST_CASE("mul then invMul round-trips") {
		M  m;
		Id a = m.create("prefix");
		Id b = m.create("suffix");
		CHECK(m.equal(Id(m.invMul(a, m.mul(a, b))), b));
	}
}

// ---------------------------------------------------------------------------
// InfixId
//
// invMul returns an InfixId, not a TemporaryId, whenever the result still
// lives in the stable `words` storage - i.e. whenever the right-hand operand
// is a Value or another InfixId. Only a TemporaryId on the right needs a
// TemporaryId back, since that's the only case where the result depends on
// the volatile temporaries buffer.
// ---------------------------------------------------------------------------

TEST_SUITE("KleeneMonoid / InfixId") {

	TEST_CASE("gen(InfixId) reads a middle infix without interning it") {
		M		 m;
		Id		 whole	= m.create("abcdef");
		Id		 ab		= m.create("ab");
		uint32_t before = m.totalWordCount();

		auto cdef = m.invMul(ab, whole);	 // InfixId "cdef"
		CHECK(peek(m, cdef) == "cdef");
		CHECK(m.totalWordCount() == before);	 // gen() alone must not intern it
	}

	TEST_CASE("invMul(Value, InfixId) strips further off an existing infix") {
		M  m;
		Id whole = m.create("abcdef");
		Id ab	 = m.create("ab");
		Id cd	 = m.create("cd");

		auto cdef = m.invMul(ab, whole);	 // InfixId "cdef"
		auto ef	  = m.invMul(cd, cdef);		 // invMul(Value, InfixId) -> InfixId "ef"
		CHECK(peek(m, ef) == "ef");
		CHECK(m.equal(Id(ef), m.create("ef")));
	}

	TEST_CASE("invMul(InfixId, Value) strips a prefix that is itself an infix") {
		M  m;
		Id whole  = m.create("abcdef");
		Id a	  = m.create("a");
		Id whole2 = m.create("bcdefij");

		auto bcdef = m.invMul(a, whole);		 // InfixId "bcdef"
		auto ij	   = m.invMul(bcdef, whole2);	 // invMul(InfixId, Value) -> InfixId "ij"
		CHECK(peek(m, ij) == "ij");
	}

	TEST_CASE("invMul(InfixId, InfixId) strips a prefix infix from another infix") {
		M  m;
		Id abcd = m.create("abcd");
		Id ab	= m.create("ab");
		Id cdxy = m.create("cdxy");

		auto cd	  = m.invMul(ab, abcd);				 // InfixId "cd"
		auto full = m.invMul(M::identity, cdxy);		 // InfixId "cdxy" (the whole word, as an InfixId)
		auto xy	  = m.invMul(cd, full);					 // invMul(InfixId, InfixId) -> InfixId "xy"
		CHECK(peek(m, xy) == "xy");
	}

	TEST_CASE("invMul(TemporaryId, InfixId) strips a temporary prefix from an infix") {
		M  m;
		Id abcdef = m.create("abcdef");
		Id ab	  = m.create("ab");
		Id c	  = m.create("c");
		Id d	  = m.create("d");

		auto cdef = m.invMul(ab, abcdef);	  // InfixId "cdef"
		auto cd	  = m.mul(c, d);			  // TemporaryId "cd"
		auto ef	  = m.invMul(cd, cdef);		  // invMul(TemporaryId, InfixId) -> InfixId "ef"
		CHECK(peek(m, ef) == "ef");
	}

	TEST_CASE("invMul(InfixId, TemporaryId) yields a TemporaryId, not an InfixId") {
		M  m;
		Id abcdef = m.create("abcdef");
		Id ab	  = m.create("ab");
		Id e	  = m.create("e");
		Id f	  = m.create("f");

		auto cdef = m.invMul(ab, abcdef);	  // InfixId "cdef"
		auto ef	  = m.mul(e, f);			  // TemporaryId "ef" (1 live temporary)
		auto whole = m.mul(cdef, ef);		  // TemporaryId "cdefef" (2 live temporaries)
		REQUIRE(m.temporaryCount() == 2);

		auto rest = m.invMul(cdef, whole);	  // invMul(InfixId, TemporaryId) -> TemporaryId "ef"
		CHECK(m.temporaryCount() == 3);	  // rest is itself a live temporary, not a plain InfixId
		CHECK(peek(m, rest) == "ef");
	}

	TEST_CASE("mul accepts InfixId operands in every position") {
		M  m;
		Id abcd = m.create("abcd");
		Id ab	= m.create("ab");
		Id ef	= m.create("ef");

		auto cd = m.invMul(ab, abcd);	  // InfixId "cd"

		CHECK(peek(m, m.mul(cd, ef)) == "cdef");			   // InfixId, Value
		CHECK(peek(m, m.mul(ef, cd)) == "efcd");			   // Value, InfixId
		CHECK(peek(m, m.mul(cd, m.mul(ef, ef))) == "cdefef");	   // InfixId, TemporaryId
		CHECK(peek(m, m.mul(m.mul(ef, ef), cd)) == "efefcd");	   // TemporaryId, InfixId
		CHECK(peek(m, m.mul(cd, cd)) == "cdcd");			   // InfixId, InfixId
	}

	TEST_CASE("equal compares InfixId against Value, TemporaryId, and InfixId") {
		M  m;
		Id abcd = m.create("abcd");
		Id ab	= m.create("ab");
		Id cd	= m.create("cd");
		Id xy	= m.create("xy");

		auto cdInfix = m.invMul(ab, abcd);	   // InfixId "cd"
		auto cdTemp	 = m.mul(m.create("c"), m.create("d"));	// TemporaryId "cd"

		CHECK(m.equal(cd, cdInfix));
		CHECK(m.equal(cdInfix, cd));
		CHECK_FALSE(m.equal(xy, cdInfix));
		CHECK_FALSE(m.equal(cdInfix, xy));

		CHECK(m.equal(cdTemp, cdInfix));
		CHECK(m.equal(cdInfix, cdTemp));

		auto cdInfix2 = m.invMul(m.create("x"), m.create("xcd"));	  // another InfixId "cd"
		CHECK(m.equal(cdInfix, cdInfix2));
		CHECK_FALSE(m.equal(cdInfix, m.invMul(m.create("x"), m.create("xxy"))));
	}

	TEST_CASE("converting an InfixId interns it, sharing storage with an equal Value") {
		M		 m;
		Id		 abcd = m.create("abcd");
		Id		 ab	  = m.create("ab");
		Id		 cd	  = m.create("cd");
		uint32_t before = m.totalWordCount();

		auto cdInfix  = m.invMul(ab, abcd);	 // content "cd" already exists as `cd`
		Id	 interned = cdInfix;
		CHECK(m.equal(interned, cd));
		CHECK(m.totalWordCount() == before);	 // no new entry: "cd" was already interned

		Id q = m.create("q");
		Id qz = m.create("qz");
		before = m.totalWordCount();

		auto infixZ		 = m.invMul(q, qz);	   // InfixId "z", content never interned before
		Id	 firstIntern = infixZ;
		CHECK(m.totalWordCount() == before + 1);	 // interned exactly once
		Id secondIntern = infixZ;					 // converting again must not add a second entry
		CHECK(m.totalWordCount() == before + 1);
		CHECK(m.equal(firstIntern, secondIntern));
	}
}

// ---------------------------------------------------------------------------
// TemporaryId lifetime
//
// Each live TemporaryId holds one unit of temporaryCount: construction, copy
// and move all increment, every destructor decrements. Assignment leaves the
// count alone because neither side dies.
// ---------------------------------------------------------------------------

TEST_SUITE("KleeneMonoid / temporaries") {

	TEST_CASE("temporaryCount tracks live temporaries") {
		M  m;
		Id a = m.create("ab");
		Id b = m.create("cd");

		CHECK(m.temporaryCount() == 0);
		{
			auto t = m.mul(a, b);
			CHECK(m.temporaryCount() == 1);
			{
				auto u = m.mul(b, a);
				CHECK(m.temporaryCount() == 2);
			}
			CHECK(m.temporaryCount() == 1);
			Id id = t;	  // conversion must not consume the temporary
			CHECK(m.temporaryCount() == 1);
			CHECK(word(m, id) == "abcd");
		}
		CHECK(m.temporaryCount() == 0);
	}

	TEST_CASE("copying and moving a temporary each take a reference") {
		M  m;
		Id a = m.create("ab");
		Id b = m.create("cd");

		{
			auto t1 = m.mul(a, b);
			REQUIRE(m.temporaryCount() == 1);
			auto t2 = t1;
			CHECK(m.temporaryCount() == 2);
			auto t3 = std::move(t1);
			CHECK(m.temporaryCount() == 3);
			CHECK(word(m, Id(t2)) == "abcd");
			CHECK(word(m, Id(t3)) == "abcd");
		}
		CHECK(m.temporaryCount() == 0);
	}

	TEST_CASE("assignment does not change the count") {
		M  m;
		Id a = m.create("ab");
		Id b = m.create("cd");

		auto t1 = m.mul(a, b);
		auto t2 = m.mul(b, a);
		REQUIRE(m.temporaryCount() == 2);
		t1 = t2;
		CHECK(m.temporaryCount() == 2);
		CHECK(word(m, Id(t1)) == "cdab");
		t1 = m.mul(a, a);
		CHECK(m.temporaryCount() == 2);
		CHECK(word(m, Id(t1)) == "abab");
	}

	TEST_CASE("a temporary can be converted to a Value more than once") {
		M  m;
		Id a = m.create("ab");
		Id b = m.create("cd");

		auto t	 = m.mul(a, b);
		Id	 id1 = t;
		Id	 id2 = t;
		CHECK(m.equal(id1, id2));
	}

	TEST_CASE("converting a temporary interns it") {
		M		 m;
		Id		 a		= m.create("ab");
		Id		 b		= m.create("cd");
		uint32_t before = m.totalWordCount();
		{
			auto t = m.mul(a, b);
			CHECK(m.totalWordCount() == before);	// not interned yet
			Id id = t;
			(void)id;
			CHECK(m.totalWordCount() == before + 1);
		}
	}

	TEST_CASE("comparing against a temporary does not intern it") {
		M		 m;
		Id		 a		= m.create("ab");
		Id		 b		= m.create("cd");
		uint32_t before = m.totalWordCount();
		CHECK_FALSE(m.equal(a, m.mul(a, b)));
		CHECK(m.totalWordCount() == before);
	}
}

// ---------------------------------------------------------------------------
// Scale / reallocation
// ---------------------------------------------------------------------------

TEST_SUITE("KleeneMonoid / scale") {

	TEST_CASE("ids stay valid across storage reallocation") {
		M						 m;
		std::vector<std::string> expected;
		std::vector<Id>			 ids;

		for (int i = 0; i < 500; ++i) {
			expected.push_back("w" + std::to_string(i) + "|" + std::string(i % 17, 'x'));
			ids.push_back(m.create(bytes(expected.back())));
		}

		REQUIRE(m.totalWordCount() == 501);

		for (std::size_t i = 0; i < ids.size(); ++i) {
			CAPTURE(i);
			CHECK(word(m, ids[i]) == expected[i]);
			CHECK(m.equal(ids[i], m.create(bytes(expected[i]))));
		}
		CHECK(m.totalWordCount() == 501);	 // nothing was added on re-create
	}

	TEST_CASE("long products") {
		M			m;
		Id			acc = M::identity;
		std::string expected;
		for (int i = 0; i < 64; ++i) {
			Id piece = m.create("ab");
			acc		 = m.mul(acc, piece);
			expected += "ab";
		}
		CHECK(word(m, acc) == expected);
	}
}

// ---------------------------------------------------------------------------
// Other symbol types
// ---------------------------------------------------------------------------

TEST_SUITE("KleeneMonoid / symbol types") {

	TEST_CASE("works over char32_t") {
		using M32 = fl::KleeneMonoid<char32_t>;
		M32 m;

		std::array<char32_t, 3> abc{U'\u03b1', U'\u03b2', U'\u03b3'};
		std::array<char32_t, 3> abd{U'\u03b1', U'\u03b2', U'\u03b4'};

		auto x = m.create(std::span<const char32_t>(abc));
		auto y = m.create(std::span<const char32_t>(abc));
		auto z = m.create(std::span<const char32_t>(abd));

		CHECK(m.equal(x, y));
		CHECK_FALSE(m.equal(x, z));
		CHECK(m.totalWordCount() == 3);
		CHECK(m.gen(x).size() == 3);
		CHECK(m.gen(x)[2] == U'\u03b3');
		CHECK(m.gen(M32::identity).empty());
	}

	TEST_CASE("char32_t words are not confused with their byte encodings") {
		using M32 = fl::KleeneMonoid<char32_t>;
		M32 m;

		std::array<char32_t, 1> one{U'\u0041'};
		std::array<char32_t, 2> two{U'\u0041', U'\u0000'};
		auto					a = m.create(std::span<const char32_t>(one));
		auto					b = m.create(std::span<const char32_t>(two));
		CHECK_FALSE(m.equal(a, b));
		CHECK(m.totalWordCount() == 3);
	}

	TEST_CASE("mul over char32_t") {
		using M32 = fl::KleeneMonoid<char32_t>;
		M32 m;

		std::array<char32_t, 2> ab{U'\u03b1', U'\u03b2'};
		std::array<char32_t, 2> cd{U'\u03b3', U'\u03b4'};
		auto					x = m.create(std::span<const char32_t>(ab));
		auto					y = m.create(std::span<const char32_t>(cd));
		auto					p = M32::Value(m.mul(x, y));
		REQUIRE(m.gen(p).size() == 4);
		CHECK(m.gen(p)[0] == U'\u03b1');
		CHECK(m.gen(p)[3] == U'\u03b4');
	}
}

// ---------------------------------------------------------------------------
// Known issues - see KNOWN_ISSUES.md
// ---------------------------------------------------------------------------

TEST_SUITE("KleeneMonoid / known issues") {

	// #2 compile error: checkPrefix streams S to std::cerr, and
	// operator<<(ostream&, char32_t) is deleted in C++20. Same for char16_t,
	// char8_t, and any symbol type without an operator<<.
	TEST_CASE("invMul works over char32_t") {
		using M32 = fl::KleeneMonoid<char32_t>;
		M32 m;

		std::array<char32_t, 2> ab{U'\u03b1', U'\u03b2'};
		std::array<char32_t, 4> abcd{U'\u03b1', U'\u03b2', U'\u03b3', U'\u03b4'};
		auto					x = m.create(std::span<const char32_t>(ab));
		auto					y = m.create(std::span<const char32_t>(abcd));

		auto rest = M32::Value(m.invMul(x, y));
		REQUIRE(m.gen(rest).size() == 2);
		CHECK(m.gen(rest)[0] == U'\u03b3');
	}

	// #3 runtime: deleteTemporary() is never called, so the scratch buffer
	// grows for the lifetime of the monoid. There is no public accessor for its
	// size, so this checks the invariant that offsets restart once nothing is
	// live.
	TEST_CASE("the temporaries buffer is reclaimed when the count hits zero") {
		M  m;
		Id a = m.create("ab");
		Id b = m.create("cd");

		uint32_t first = 0, second = 0;
		{
			auto t = m.mul(a, b);
			first  = t.start;
		}
		{
			auto t = m.mul(a, b);
			second = t.start;
		}
		CHECK(first == second);
	}
}

