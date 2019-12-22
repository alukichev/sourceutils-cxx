/*
MIT License

Copyright (c) 2019 Alexander Lukichev <alexander.lukichev@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#ifndef TEST_TABULATOR_H_
#define TEST_TABULATOR_H_

#include <array>
#include <cctype>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>

#include <type_traits>

namespace tabulator {

struct column {
	const char * const p;
	const std::size_t size;
	const std::size_t width;

	inline column(const char *s, std::size_t w) : p{s}, size{std::strlen(s)}, width{w} {}
	inline column(const std::string& s, std::size_t w) : p{s.c_str()}, size{s.size()}, width{w} {}
	inline column(const column&) = default;
};

namespace internal {

using std::array;
using std::enable_if;
using std::isblank;
using std::is_same;
using std::ostream;
using std::size_t;
using std::string;

// Thanks to https://www.fluentcpp.com/2019/01/25/variadic-number-function-parameters-type/
// and https://en.cppreference.com/w/cpp/experimental/conjunction#Example
template<bool...> struct bool_pack{};
template<class... Us>
using conjunction = is_same<bool_pack<true,Us::value...>, bool_pack<Us::value...,true> >;

template <class T, class... Ts>
using force_type = typename enable_if<conjunction<is_same<T, Ts>...>::value, int>::type;

struct colstate {
	size_t cp{0}; // index in the whole column text
	size_t lp{0}; // index in the currently emitted line

	inline char consume(const column& c) { return end(c) ? 0 : c.p[cp++]; }
	inline colstate& emit(ostream& os, char ch) { os.put(ch); ++lp; return *this; }
	inline colstate& breakLine(void) { lp = 0; return *this; }
	inline bool linebreak(char ch, const column& c) const;
	inline bool nextWordFits(const column& c) const;
	inline bool end(const column& c) const { return c.size <= cp; }
};

inline bool isws(char ch)
{
	return isblank(static_cast<unsigned char>(ch));
}

inline bool colstate::linebreak(char ch, const column& c) const
{
	// Line break: ch is \\n or (ws and next word cannot be emitted).
	return ch == '\n' || (isws(ch) && !nextWordFits(c));
}

inline bool colstate::nextWordFits(const column& c) const
{
	const size_t colwidth = c.width;
	const char *s = c.p;
	size_t l = lp;

	// Next word can be emitted: lp will be at most colwidth on next ws.
	for (size_t i = cp; s[i] && l < colwidth; ++i, ++l)
		if (isws(s[i]))
			return true;

	return l < colwidth;
}

inline void switch_col(ostream& os, colstate& state, size_t colwidth, char fill, const string& sep)
{
	const size_t inc = fill == '\t' ? 8 : 1;

	// Switch to next column: emit fill up to colwidth, emit sep
	for (size_t i = state.lp; i < colwidth; i += inc)
		os.put(fill);
	os << sep;
}

inline void emit_col(ostream& os, colstate& state, const column& c)
{
	// Emit column characters: consume ch. If line break, then stop, else emit.
	for (char ch = state.consume(c); !!ch; ch = state.consume(c)) {
		if (!state.linebreak(ch, c))
			state.emit(os, ch);
		else
			break;
	}
}

template <size_t N>
inline bool is_unconsumed(const array<colstate,N>& state, const array<column,N> c)
{
	for (size_t i = 0; i < N; ++i)
		if (!state[i].end(c[i]))
			return true;
	return false;
}

}

/** Output one or more columns of text into a stream.
 *
 * Performs line by line output of one or more strings into columns of given
 * widths, aligning text within a column to the left. Each line goes into the
 * given stream. Columns are separated with the given "sep" string, and the
 * space between the last character in a line and column separator is filled
 * with "fill" character. The operation is done in O(N) time.
 *
 * @param os		an ostream object to output text into
 * @param sep		a string to separate the columns with
 * @param fill		a character to fill the space between the last
 *            		character in a column on a given line and the separator
 * @param cols		a variable number of "column" structures
 *
 * Example:
 * @code
 *
 * 	#include <iostream>
 * 	#include "tabulator.h"
 *
 * 	using tabulator::tabulate;
 * 	using tabulator::column;
 *
 * 	...
 * 	tabulate(std::cout, " | ", ' ',
 * 			column{"abc def ghi", 6},
 * 			column{"123 4432 17 8989", 4}) << std::flush;
 *
 * @endcode
 *
 * @return Reference to the stream, so that it could be used later in the same
 * expression that has called the function.
 */
template <typename... Cols, internal::force_type<column, Cols...> = 0>
inline std::ostream& tabulate(std::ostream& os, const char *sep, char fill, const Cols&... cols)
{
	using namespace internal;

	array<colstate,sizeof...(cols)> state;
	array<column,sizeof...(cols)> c{ cols... };

	// Emit lines until all character pointers are at the end of their strings
	for (bool unconsumed = is_unconsumed(state, c); unconsumed; unconsumed = is_unconsumed(state, c)) {
		// Line emit: emit column characters then (if not last col, then switch to next column) then break line
		for (size_t col = 0; col < sizeof...(cols); ++col) {
			emit_col(os, state[col], c[col]);
			if ((col + 1) < sizeof...(cols))
				switch_col(os, state[col], c[col].width, fill, sep);
			state[col].breakLine();
		}
		os << '\n';
	}

	return os;
}

/** Output one or more space-filled columns of text into a stream.
 *
 * This is an overload of tabulate(ostream&, const char*, char, Cols...), with
 * "fill" parameter set to space character.
 *
 * @param os		an ostream object to output text into
 * @param sep		a string to separate the columns with
 * @param cols		a variable number of "column" structures
 *
 * Example:
 * @code
 *
 * 	#include <iostream>
 * 	#include "tabulator.h"
 *
 * 	using tabulator::tabulate;
 * 	using tabulator::column;
 *
 * 	...
 * 	tabulate(std::cout, " | ",
 * 			column{"abc def ghi", 6},
 * 			column{"123 4432 17 8989", 4}) << std::flush;
 *
 * @endcode
 *
 * @return Reference to the stream, so that it could be used later in the same
 * expression that has called the function.
 */
template <typename... Cols, internal::force_type<column, Cols...> = 0 >
inline std::ostream& tabulate(std::ostream& os, const char *sep, const Cols&... cols)
{
	return tabulate(os, sep, ' ', cols...);
}

/** Output one or more space-separated columns of text into a stream.
 *
 * This is an overload of tabulate(ostream&, const char*, char, Cols...), with
 * both "sep" and "fill" parameters set to one space character. Use this if
 * you want only spaces between columns of text.
 *
 * @param os		an ostream object to output text into
 * @param cols		a variable number of "column" structures
 *
 * Example:
 * @code
 *
 * 	#include <iostream>
 * 	#include "tabulator.h"
 *
 * 	using tabulator::tabulate;
 * 	using tabulator::column;
 *
 * 	...
 * 	tabulate(std::cout,
 * 			column{"abc def ghi", 6},
 * 			column{"123 4432 17 8989", 4}) << std::flush;
 *
 * @endcode
 *
 * @return Reference to the stream, so that it could be used later in the same
 * expression that has called the function.
 */
template <typename... Cols, internal::force_type<column, Cols...> = 0 >
inline std::ostream& tabulate(std::ostream& os, const Cols&... cols)
{
	return tabulate(os, " ", ' ', cols...);
}

}

#endif /* TEST_TABULATOR_H_ */
