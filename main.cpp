#include <iostream>
#include <vector>
#include <string>
#include "tracknew.hpp"

int main() {
	TrackNew::reset();

	std::vector<std::string> vec;
	//vec.reserve(1001);

	for (size_t i = 0; i < 1000; i++) {
		vec.emplace_back("just a non-SSO string");
	}

	TrackNew::status();
	return 0;
}