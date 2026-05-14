#pragma once

#include <span>

// Use std::ospanstream (C++23) if available, otherwise fall back to the
// deprecated-but-ubiquitous std::ostrstream which provides identical
// semantics: writes into a caller-owned char buffer.

#if defined(__cpp_lib_spanstream)
#include <spanstream>
namespace std23 {
	using std::ispanstream;
	using std::ospanstream;
	using std::spanbuf;
} // namespace std23
#else
#include <strstream>
namespace std23 {
	// Drop-in for std::ospanstream: accepts std::span<char>, writes into it.
	// freeze(false) in the destructor prevents ostrstream from calling delete[]
	// on the caller-owned buffer.
	class ospanstream : public std::ostrstream {
	public:
		explicit ospanstream(std::span<char> buf) : std::ostrstream(buf.data(), static_cast<int>(buf.size())) {}
		~ospanstream() { freeze(false); }
	};
} // namespace std23
#endif
