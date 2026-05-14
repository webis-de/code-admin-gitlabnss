#pragma once

// Use std::format (C++20) if available, otherwise fall back to fmtlib.
// Both paths are exposed as std20::format so call sites are identical.

#if defined(__cpp_lib_format)
#include <format>
namespace std20 {
	using std::format;
	using std::format_args;
	using std::make_format_args;
	using std::vformat;
	template <typename... Args>
	using format_string = std::format_string<Args...>;
} // namespace std20
#else
#include <fmt/format.h>
namespace std20 {
	using fmt::format;
	using fmt::format_args;
	using fmt::make_format_args;
	using fmt::vformat;
	template <typename... Args>
	using format_string = fmt::format_string<Args...>;
} // namespace std20
#endif
