#include <iostream>
#include <vector>
#include <string>
#include <memory_resource>
#include <cstdlib>
#include <array>
#include "tracknew.hpp"

int main() {
	TrackNew::reset();


	std::array<std::byte, 200'000> buf;
	std::pmr::monotonic_buffer_resource pool{buf.data(), buf.size()};
	std::pmr::vector<std::pmr::string> vec;



	//std::vector<std::string> vec;
	//vec.reserve(1001);

	for (size_t i = 0; i < 1000; i++) {
		vec.emplace_back("just a non-SSO string");
	}

	TrackNew::status();
	return 0;
}