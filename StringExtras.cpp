//===-- StringExtras.cpp - Implement the StringExtras header --------------===//
//
//           originally from The LLVM Compiler Infrastructure
//
// Was distributed under the University of Illinois Open Source License.
//
//===----------------------------------------------------------------------===//
//
// This file implements the StringExtras.h header
//
//===----------------------------------------------------------------------===//

#include "SmallVector.hpp"

#include "STLExtras.hpp"
#include "StringExtras.hpp"
using namespace akj;

/// StrInStrNoCase - Portable version of strcasestr.  Locates the first
/// occurrence of string 's1' in string 's2', ignoring case.  Returns
/// the offset of s2 in s1 or npos if s2 cannot be found.

cStringRef::size_type akj::StrInStrNoCase(cStringRef s1, cStringRef s2) {
  size_t N = s2.size(), M = s1.size();
  if (N > M)
    return cStringRef::npos;
  for (size_t i = 0, e = M - N + 1; i != e; ++i)
    if (s1.substr(i, N).equals_lower(s2))
      return i;
  return cStringRef::npos;
}

/// getToken - This function extracts one token from source, ignoring any
/// leading characters that appear in the Delimiters string, and ending the
/// token at any of the characters that appear in the Delimiters string.  If
/// there are no tokens in the source string, an empty string is returned.
/// The function returns a pair containing the extracted token and the
/// remaining tail string.
std::pair<cStringRef, cStringRef> akj::getToken(cStringRef Source,
                                               cStringRef Delimiters) {
  // Figure out where the token starts.
  cStringRef::size_type Start = Source.find_first_not_of(Delimiters);

  // Find the next occurrence of the delimiter.
  cStringRef::size_type End = Source.find_first_of(Delimiters, Start);

  return std::make_pair(Source.slice(Start, End), Source.substr(End));
}

/// SplitString - Split up the specified string according to the specified
/// delimiters, appending the result fragments to the output list.
void akj::SplitString(cStringRef Source,
                       cSmallVectorImpl<cStringRef> &OutFragments,
                       cStringRef Delimiters) {
  std::pair<cStringRef, cStringRef> S = getToken(Source, Delimiters);
  while (!S.first.empty()) {
    OutFragments.push_back(S.first);
    S = getToken(S.second, Delimiters);
  }
}
