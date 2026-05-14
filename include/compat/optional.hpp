#pragma once

#include <optional> // always needed: std::optional used as lift() parameter type

// Use std::optional (C++23 monadic ops) if available, otherwise fall back to
// tl::optional which provides and_then / transform / or_else on older compilers.
// Both paths are exposed as std23::optional so call sites are identical.
//
// Wrinkle: std::optional returned by external libraries (e.g. toml++) is a
// different type from tl::optional when the fallback is in effect.  Use
// std23::lift(std_opt) to safely convert std::optional → std23::optional.

// 202106L = basic C++23 optional (GCC 11, no monadic ops yet).
// 202110L = monadic operations finalised via LWG 3621 (GCC 12+, Clang 17+).
#if defined(__cpp_lib_optional) && __cpp_lib_optional >= 202110L
#include <optional>
namespace std23 {
	using std::bad_optional_access;
	using std::make_optional;
	using std::nullopt;
	using std::nullopt_t;
	using std::optional;

	template <typename T>
	constexpr optional<T> lift(std::optional<T> o) {
		return o;
	}
} // namespace std23
#else
#include <tl/optional.hpp>
namespace std23 {
	using tl::bad_optional_access;
	using tl::make_optional;
	using tl::nullopt;
	using tl::nullopt_t;
	using tl::optional;

	// Convert a plain std::optional (e.g. from toml++) into std23::optional
	// so that monadic methods (and_then, transform, or_else) are available.
	template <typename T>
	constexpr optional<T> lift(std::optional<T> o) {
		if (o)
			return optional<T>{std::move(*o)};
		return nullopt;
	}
} // namespace std23
#endif
