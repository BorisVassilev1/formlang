#pragma once
#include <unordered_map>
#include <unordered_set>
#include <string>

#include "hashing.hpp"

namespace fl {
template <class K, class V, class H = fl::hash<K>>
using unordered_map = std::unordered_map<K, V, H>;

template <class K, class H = fl::hash<K>>
using unordered_set = std::unordered_set<K, H>;

template <class K, class V, class H = fl::hash<K>>
using unordered_multimap = std::unordered_multimap<K, V, H>;

template <class T>
std::string toString(T &&t) {
	// if(t.begin() == t.end()) return "";
	return std::string(t.begin(), t.end());
}

template <class U, class V>
auto commonPrefix(U &&a, V &&b) {
	auto it1 = std::begin(a);
	auto it2 = std::begin(b);
	while (it1 != std::end(a) && it2 != std::end(b) && *it1 == *it2) {
		++it1;
		++it2;
	}
	return std::ranges::subrange(std::begin(a), it1);
}

template <class U, class V>
auto commonPrefixLen(U &&a, V &&b) {
	auto it1 = std::begin(a);
	auto it2 = std::begin(b);
	while (it1 != std::end(a) && it2 != std::end(b) && *it1 == *it2) {
		++it1;
		++it2;
	}
	return std::distance(std::begin(a), it1);
}
}	  // namespace fl
