#pragma once

// Use std::expected (C++23) if available, otherwise fall back to TartanLlama/expected.
// Both paths are exposed as std23:: so call sites are identical.

#if defined(__cpp_lib_expected)
#include <expected>
namespace std23 {
	using std::expected;
	using std::unexpect;
	using std::unexpect_t;
	using std::unexpected;
	template <typename E>
	using bad_expected_access = std::bad_expected_access<E>;
} // namespace std23
#else
#include <tl/expected.hpp>
namespace std23 {
	using tl::expected;
	using tl::unexpect;
	using tl::unexpect_t;
	using tl::unexpected;
	template <typename E>
	using bad_expected_access = tl::bad_expected_access<E>;
} // namespace std23
#endif
