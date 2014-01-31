//===- akjSmallString.hpp - 'Normally small' strings --------*- C++ -*-===//
//
//           originally from The LLVM Compiler Infrastructure
//
// Was distributed under the University of Illinois Open Source License.
//
//===----------------------------------------------------------------------===//
//
// This file defines the SmallString class.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "SmallVector.hpp"

#include "StringRef.hpp"

namespace akj {

/// SmallString - A SmallString is just a cSmallVector with methods and accessors
/// that make it work better as a string (e.g. operator+ etc).
template<unsigned InternalLen>
class SmallString : public cSmallVector<char, InternalLen> {
public:
  /// Default ctor - Initialize to empty.
  SmallString() {}

  /// Initialize from a cStringRef.
  SmallString(cStringRef S) : cSmallVector<char, InternalLen>(S.begin(), S.end()) {}

  /// Initialize with a range.
  template<typename ItTy>
  SmallString(ItTy S, ItTy E) : cSmallVector<char, InternalLen>(S, E) {}

  /// Copy ctor.
  SmallString(const SmallString &RHS) : cSmallVector<char, InternalLen>(RHS) {}

  // Note that in order to add new overloads for append & assign, we have to
  // duplicate the inherited versions so as not to inadvertently hide them.

  /// @}
  /// @name String Assignment
  /// @{

  /// Assign from a repeated element.
  void assign(size_t NumElts, char Elt) {
    this->cSmallVectorImpl<char>::assign(NumElts, Elt);
  }

  /// Assign from an iterator pair.
  template<typename in_iter>
  void assign(in_iter S, in_iter E) {
    this->clear();
    cSmallVectorImpl<char>::append(S, E);
  }

  /// Assign from a cStringRef.
  void assign(cStringRef RHS) {
    this->clear();
    cSmallVectorImpl<char>::append(RHS.begin(), RHS.end());
  }

  /// Assign from a cSmallVector.
  void assign(const cSmallVectorImpl<char> &RHS) {
    this->clear();
    cSmallVectorImpl<char>::append(RHS.begin(), RHS.end());
  }

  /// @}
  /// @name String Concatenation
  /// @{

  /// Append from an iterator pair.
  template<typename in_iter>
  void append(in_iter S, in_iter E) {
    cSmallVectorImpl<char>::append(S, E);
  }

  void append(size_t NumInputs, char Elt) {
    cSmallVectorImpl<char>::append(NumInputs, Elt);
  }


  /// Append from a cStringRef.
  void append(cStringRef RHS) {
    cSmallVectorImpl<char>::append(RHS.begin(), RHS.end());
  }

  /// Append from a cSmallVector.
  void append(const cSmallVectorImpl<char> &RHS) {
    cSmallVectorImpl<char>::append(RHS.begin(), RHS.end());
  }

  /// @}
  /// @name String Comparison
  /// @{

  /// Check for string equality.  This is more efficient than compare() when
  /// the relative ordering of inequal strings isn't needed.
  bool equals(cStringRef RHS) const {
    return str().equals(RHS);
  }

  /// Check for string equality, ignoring case.
  bool equals_lower(cStringRef RHS) const {
    return str().equals_lower(RHS);
  }

  /// Compare two strings; the result is -1, 0, or 1 if this string is
  /// lexicographically less than, equal to, or greater than the \p RHS.
  int compare(cStringRef RHS) const {
    return str().compare(RHS);
  }

  /// compare_lower - Compare two strings, ignoring case.
  int compare_lower(cStringRef RHS) const {
    return str().compare_lower(RHS);
  }

  /// compare_numeric - Compare two strings, treating sequences of digits as
  /// numbers.
  int compare_numeric(cStringRef RHS) const {
    return str().compare_numeric(RHS);
  }

  /// @}
  /// @name String Predicates
  /// @{

  /// startswith - Check if this string starts with the given \p Prefix.
  bool startswith(cStringRef Prefix) const {
    return str().startswith(Prefix);
  }

  /// endswith - Check if this string ends with the given \p Suffix.
  bool endswith(cStringRef Suffix) const {
    return str().endswith(Suffix);
  }

  /// @}
  /// @name String Searching
  /// @{

  /// find - Search for the first character \p C in the string.
  ///
  /// \return - The index of the first occurrence of \p C, or npos if not
  /// found.
  size_t find(char C, size_t From = 0) const {
    return str().find(C, From);
  }

  /// Search for the first string \p Str in the string.
  ///
  /// \returns The index of the first occurrence of \p Str, or npos if not
  /// found.
  size_t find(cStringRef Str, size_t From = 0) const {
    return str().find(Str, From);
  }

  /// Search for the last character \p C in the string.
  ///
  /// \returns The index of the last occurrence of \p C, or npos if not
  /// found.
  size_t rfind(char C, size_t From = cStringRef::npos) const {
    return str().rfind(C, From);
  }

  /// Search for the last string \p Str in the string.
  ///
  /// \returns The index of the last occurrence of \p Str, or npos if not
  /// found.
  size_t rfind(cStringRef Str) const {
    return str().rfind(Str);
  }

  /// Find the first character in the string that is \p C, or npos if not
  /// found. Same as find.
  size_t find_first_of(char C, size_t From = 0) const {
    return str().find_first_of(C, From);
  }

  /// Find the first character in the string that is in \p Chars, or npos if
  /// not found.
  ///
  /// Complexity: O(size() + Chars.size())
  size_t find_first_of(cStringRef Chars, size_t From = 0) const {
    return str().find_first_of(Chars, From);
  }

  /// Find the first character in the string that is not \p C or npos if not
  /// found.
  size_t find_first_not_of(char C, size_t From = 0) const {
    return str().find_first_not_of(C, From);
  }

  /// Find the first character in the string that is not in the string
  /// \p Chars, or npos if not found.
  ///
  /// Complexity: O(size() + Chars.size())
  size_t find_first_not_of(cStringRef Chars, size_t From = 0) const {
    return str().find_first_not_of(Chars, From);
  }

  /// Find the last character in the string that is \p C, or npos if not
  /// found.
  size_t find_last_of(char C, size_t From = cStringRef::npos) const {
    return str().find_last_of(C, From);
  }

  /// Find the last character in the string that is in \p C, or npos if not
  /// found.
  ///
  /// Complexity: O(size() + Chars.size())
  size_t find_last_of(
      cStringRef Chars, size_t From = cStringRef::npos) const {
    return str().find_last_of(Chars, From);
  }

  /// @}
  /// @name Helpful Algorithms
  /// @{

  /// Return the number of occurrences of \p C in the string.
  size_t count(char C) const {
    return str().count(C);
  }

  /// Return the number of non-overlapped occurrences of \p Str in the
  /// string.
  size_t count(cStringRef Str) const {
    return str().count(Str);
  }

  /// @}
  /// @name Substring Operations
  /// @{

  /// Return a reference to the substring from [Start, Start + N).
  ///
  /// \param Start The index of the starting character in the substring; if
  /// the index is npos or greater than the length of the string then the
  /// empty substring will be returned.
  ///
  /// \param N The number of characters to included in the substring. If \p N
  /// exceeds the number of characters remaining in the string, the string
  /// suffix (starting with \p Start) will be returned.
  cStringRef substr(size_t Start, size_t N = cStringRef::npos) const {
    return str().substr(Start, N);
  }

  /// Return a reference to the substring from [Start, End).
  ///
  /// \param Start The index of the starting character in the substring; if
  /// the index is npos or greater than the length of the string then the
  /// empty substring will be returned.
  ///
  /// \param End The index following the last character to include in the
  /// substring. If this is npos, or less than \p Start, or exceeds the
  /// number of characters remaining in the string, the string suffix
  /// (starting with \p Start) will be returned.
  cStringRef slice(size_t Start, size_t End) const {
    return str().slice(Start, End);
  }

  // Extra methods.

  /// Explicit conversion to cStringRef.
  cStringRef str() const { return cStringRef(this->begin(), this->size()); }

  // TODO: Make this const, if it's safe...
  const char* c_str() {
    this->push_back(0);
    this->pop_back();
    return this->data();
  }

  /// Implicit conversion to cStringRef.
  operator cStringRef() const { return str(); }

  // Extra operators.
  const SmallString &operator=(cStringRef RHS) {
    this->clear();
    return *this += RHS;
  }

  SmallString &operator+=(cStringRef RHS) {
    this->append(RHS.begin(), RHS.end());
    return *this;
  }
  SmallString &operator+=(char C) {
    this->push_back(C);
    return *this;
  }
};

}

