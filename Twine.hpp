//===-- akjTwine.hpp - Fast Temporary String Concatenation -------*- C++ -*-===//
//
//           originally from The LLVM Compiler Infrastructure
//
// Was distributed under the University of Illinois Open Source License.
//
//===----------------------------------------------------------------------===//

#pragma once
#include "StringRef.hpp"

#include <stdint.h>
#include <cassert>
#include <string>

namespace akj {
  template <typename T>
  class cSmallVectorImpl;
  class cStringRef;
  class raw_ostream;

  /// Twine - A lightweight data structure for efficiently representing the
  /// concatenation of temporary values as strings.
  ///
  /// A Twine is a kind of rope, it represents a concatenated string using a
  /// binary-tree, where the string is the preorder of the nodes. Since the
  /// Twine can be efficiently rendered into a buffer when its result is used,
  /// it avoids the cost of generating temporary values for intermediate string
  /// results -- particularly in cases when the Twine result is never
  /// required. By explicitly tracking the type of leaf nodes, we can also avoid
  /// the creation of temporary strings for conversions operations (such as
  /// appending an integer to a string).
  ///
  /// A Twine is not intended for use directly and should not be stored, its
  /// implementation relies on the ability to store pointers to temporary stack
  /// objects which may be deallocated at the end of a statement. Twines should
  /// only be used accepted as const references in arguments, when an API wishes
  /// to accept possibly-concatenated strings.
  ///
  /// Twines support a special 'null' value, which always concatenates to form
  /// itself, and renders as an empty string. This can be returned from APIs to
  /// effectively nullify any concatenations performed on the result.
  ///
  /// \b Implementation
  ///
  /// Given the nature of a Twine, it is not possible for the Twine's
  /// concatenation method to construct interior nodes; the result must be
  /// represented inside the returned value. For this reason a Twine object
  /// actually holds two values, the left- and right-hand sides of a
  /// concatenation. We also have nullary Twine objects, which are effectively
  /// sentinel values that represent empty strings.
  ///
  /// Thus, a Twine can effectively have zero, one, or two children. The \see
  /// isNullary(), \see isUnary(), and \see isBinary() predicates exist for
  /// testing the number of children.
  ///
  /// We maintain a number of invariants on Twine objects (FIXME: Why):
  ///  - Nullary twines are always represented with their Kind on the left-hand
  ///    side, and the Empty kind on the right-hand side.
  ///  - Unary twines are always represented with the value on the left-hand
  ///    side, and the Empty kind on the right-hand side.
  ///  - If a Twine has another Twine as a child, that child should always be
  ///    binary (otherwise it could have been folded into the parent).
  ///
  /// These invariants are check by \see isValid().
  ///
  /// \b Efficiency Considerations
  ///
  /// The Twine is designed to yield efficient and small code for common
  /// situations. For this reason, the concat() method is inlined so that
  /// concatenations of leaf nodes can be optimized into stores directly into a
  /// single stack allocated object.
  ///
  /// In practice, not all compilers can be trusted to optimize concat() fully,
  /// so we provide two additional methods (and accompanying operator+
  /// overloads) to guarantee that particularly important cases (cstring plus
  /// cStringRef) codegen as desired.
  class Twine {
    /// NodeKind - Represent the type of an argument.
    enum NodeKind {
      /// An empty string; the result of concatenating anything with it is also
      /// empty.
      NullKind,

      /// The empty string.
      EmptyKind,

      /// A pointer to a Twine instance.
      TwineKind,

      /// A pointer to a C string instance.
      CStringKind,

      /// A pointer to an std::string instance.
      StdStringKind,

      /// A pointer to a cStringRef instance.
      StringRefKind,

      /// A char value reinterpreted as a pointer, to render as a character.
      CharKind,

      /// An unsigned int value reinterpreted as a pointer, to render as an
      /// unsigned decimal integer.
      DecUIKind,

      /// An int value reinterpreted as a pointer, to render as a signed
      /// decimal integer.
      DecIKind,

      /// A pointer to an unsigned long value, to render as an unsigned decimal
      /// integer.
      DecULKind,

      /// A pointer to a long value, to render as a signed decimal integer.
      DecLKind,

      /// A pointer to an unsigned long long value, to render as an unsigned
      /// decimal integer.
      DecULLKind,

      /// A pointer to a long long value, to render as a signed decimal integer.
      DecLLKind,

      /// A pointer to a uint64_t value, to render as an unsigned hexadecimal
      /// integer.
      UHexKind
    };

    union Child
    {
      const Twine *twine;
      const char *cString;
      const std::string *stdString;
      const cStringRef *cStringRef;
      char character;
      unsigned int decUI;
      int decI;
      const unsigned long *decUL;
      const long *decL;
      const unsigned long long *decULL;
      const long long *decLL;
      const uint64_t *uHex;
    };

  private:
    /// LHS - The prefix in the concatenation, which may be uninitialized for
    /// Null or Empty kinds.
    Child LHS;
    /// RHS - The suffix in the concatenation, which may be uninitialized for
    /// Null or Empty kinds.
    Child RHS;
    // enums stored as unsigned chars to save on space while some compilers
    // don't support specifying the backing type for an enum
    /// LHSKind - The NodeKind of the left hand side, \see getLHSKind().
    unsigned char LHSKind;
    /// RHSKind - The NodeKind of the left hand side, \see getLHSKind().
    unsigned char RHSKind;

  private:
    /// Construct a nullary twine; the kind must be NullKind or EmptyKind.
    explicit Twine(NodeKind Kind)
      : LHSKind(Kind), RHSKind(EmptyKind) {
      assert(isNullary() && "Invalid kind!");
    }

    /// Construct a binary twine.
    explicit Twine(const Twine &_LHS, const Twine &_RHS)
      : LHSKind(TwineKind), RHSKind(TwineKind) {
      LHS.twine = &_LHS;
      RHS.twine = &_RHS;
      assert(isValid() && "Invalid twine!");
    }

    /// Construct a twine from explicit values.
    explicit Twine(Child _LHS, NodeKind _LHSKind,
                   Child _RHS, NodeKind _RHSKind)
      : LHS(_LHS), RHS(_RHS), LHSKind(_LHSKind), RHSKind(_RHSKind) {
      assert(isValid() && "Invalid twine!");
    }

    /// isNull - Check for the null twine.
    bool isNull() const {
      return getLHSKind() == NullKind;
    }

    /// isEmpty - Check for the empty twine.
    bool isEmpty() const {
      return getLHSKind() == EmptyKind;
    }

    /// isNullary - Check if this is a nullary twine (null or empty).
    bool isNullary() const {
      return isNull() || isEmpty();
    }

    /// isUnary - Check if this is a unary twine.
    bool isUnary() const {
      return getRHSKind() == EmptyKind && !isNullary();
    }

    /// isBinary - Check if this is a binary twine.
    bool isBinary() const {
      return getLHSKind() != NullKind && getRHSKind() != EmptyKind;
    }

    /// isValid - Check if this is a valid twine (satisfying the invariants on
    /// order and number of arguments).
    bool isValid() const {
      // Nullary twines always have Empty on the RHS.
      if (isNullary() && getRHSKind() != EmptyKind)
        return false;

      // Null should never appear on the RHS.
      if (getRHSKind() == NullKind)
        return false;

      // The RHS cannot be non-empty if the LHS is empty.
      if (getRHSKind() != EmptyKind && getLHSKind() == EmptyKind)
        return false;

      // A twine child should always be binary.
      if (getLHSKind() == TwineKind &&
          !LHS.twine->isBinary())
        return false;
      if (getRHSKind() == TwineKind &&
          !RHS.twine->isBinary())
        return false;

      return true;
    }

    /// getLHSKind - Get the NodeKind of the left-hand side.
    NodeKind getLHSKind() const { return (NodeKind) LHSKind; }

    /// getRHSKind - Get the NodeKind of the right-hand side.
    NodeKind getRHSKind() const { return (NodeKind) RHSKind; }

    /// printOneChild - Print one child from a twine.
    void printOneChild(raw_ostream &OS, Child Ptr, NodeKind Kind) const;

    /// printOneChildRepr - Print the representation of one child from a twine.
    void printOneChildRepr(raw_ostream &OS, Child Ptr,
                           NodeKind Kind) const;

  public:
    /// @name Constructors
    /// @{

    /// Construct from an empty string.
    /*implicit*/ Twine() : LHSKind(EmptyKind), RHSKind(EmptyKind) {
      assert(isValid() && "Invalid twine!");
    }

    /// Construct from a C string.
    ///
    /// We take care here to optimize "" into the empty twine -- this will be
    /// optimized out for string constants. This allows Twine arguments have
    /// default "" values, without introducing unnecessary string constants.
    /*implicit*/ Twine(const char *Str)
      : RHSKind(EmptyKind) {
      if (Str[0] != '\0') {
        LHS.cString = Str;
        LHSKind = CStringKind;
      } else
        LHSKind = EmptyKind;

      assert(isValid() && "Invalid twine!");
    }

    /// Construct from an std::string.
    /*implicit*/ Twine(const std::string &Str)
      : LHSKind(StdStringKind), RHSKind(EmptyKind) {
      LHS.stdString = &Str;
      assert(isValid() && "Invalid twine!");
    }

    /// Construct from a cStringRef.
    /*implicit*/ Twine(const cStringRef &Str)
      : LHSKind(StringRefKind), RHSKind(EmptyKind) {
      LHS.cStringRef = &Str;
      assert(isValid() && "Invalid twine!");
    }

    /// Construct from a char.
    explicit Twine(char Val)
      : LHSKind(CharKind), RHSKind(EmptyKind) {
      LHS.character = Val;
    }

    /// Construct from a signed char.
    explicit Twine(signed char Val)
      : LHSKind(CharKind), RHSKind(EmptyKind) {
      LHS.character = static_cast<char>(Val);
    }

    /// Construct from an unsigned char.
    explicit Twine(unsigned char Val)
      : LHSKind(CharKind), RHSKind(EmptyKind) {
      LHS.character = static_cast<char>(Val);
    }

    /// Construct a twine to print \p Val as an unsigned decimal integer.
    explicit Twine(unsigned Val)
      : LHSKind(DecUIKind), RHSKind(EmptyKind) {
      LHS.decUI = Val;
    }

    /// Construct a twine to print \p Val as a signed decimal integer.
    explicit Twine(int Val)
      : LHSKind(DecIKind), RHSKind(EmptyKind) {
      LHS.decI = Val;
    }

    /// Construct a twine to print \p Val as an unsigned decimal integer.
    explicit Twine(const unsigned long &Val)
      : LHSKind(DecULKind), RHSKind(EmptyKind) {
      LHS.decUL = &Val;
    }

    /// Construct a twine to print \p Val as a signed decimal integer.
    explicit Twine(const long &Val)
      : LHSKind(DecLKind), RHSKind(EmptyKind) {
      LHS.decL = &Val;
    }

    /// Construct a twine to print \p Val as an unsigned decimal integer.
    explicit Twine(const unsigned long long &Val)
      : LHSKind(DecULLKind), RHSKind(EmptyKind) {
      LHS.decULL = &Val;
    }

    /// Construct a twine to print \p Val as a signed decimal integer.
    explicit Twine(const long long &Val)
      : LHSKind(DecLLKind), RHSKind(EmptyKind) {
      LHS.decLL = &Val;
    }

    // FIXME: Unfortunately, to make sure this is as efficient as possible we
    // need extra binary constructors from particular types. We can't rely on
    // the compiler to be smart enough to fold operator+()/concat() down to the
    // right thing. Yet.

    /// Construct as the concatenation of a C string and a cStringRef.
    /*implicit*/ Twine(const char *_LHS, const cStringRef &_RHS)
      : LHSKind(CStringKind), RHSKind(StringRefKind) {
      LHS.cString = _LHS;
      RHS.cStringRef = &_RHS;
      assert(isValid() && "Invalid twine!");
    }

    /// Construct as the concatenation of a cStringRef and a C string.
    /*implicit*/ Twine(const cStringRef &_LHS, const char *_RHS)
      : LHSKind(StringRefKind), RHSKind(CStringKind) {
      LHS.cStringRef = &_LHS;
      RHS.cString = _RHS;
      assert(isValid() && "Invalid twine!");
    }

    /// Create a 'null' string, which is an empty string that always
    /// concatenates to form another empty string.
    static Twine createNull() {
      return Twine(NullKind);
    }

    /// @}
    /// @name Numeric Conversions
    /// @{

    // Construct a twine to print \p Val as an unsigned hexadecimal integer.
    static Twine utohexstr(const uint64_t &Val) {
      Child LHS, RHS;
      LHS.uHex = &Val;
      RHS.twine = 0;
      return Twine(LHS, UHexKind, RHS, EmptyKind);
    }

    /// @}
    /// @name Predicate Operations
    /// @{

    /// isTriviallyEmpty - Check if this twine is trivially empty; a false
    /// return value does not necessarily mean the twine is empty.
    bool isTriviallyEmpty() const {
      return isNullary();
    }

    /// isSinglecStringRef - Return true if this twine can be dynamically
    /// accessed as a single cStringRef value with getSinglecStringRef().
    bool isSingleStringRef() const {
      if (getRHSKind() != EmptyKind) return false;

      switch (getLHSKind()) {
      case EmptyKind:
      case CStringKind:
      case StdStringKind:
      case StringRefKind:
        return true;
      default:
        return false;
      }
    }

    /// @}
    /// @name String Operations
    /// @{

    Twine concat(const Twine &Suffix) const;

    /// @}
    /// @name Output & Conversion.
    /// @{

    /// str - Return the twine contents as a std::string.
    std::string str() const;

    /// toVector - Write the concatenated string into the given SmallString or
    /// cSmallVector.
    void toVector(cSmallVectorImpl<char> &Out) const;

    /// getSinglecStringRef - This returns the twine as a single cStringRef.  This
    /// method is only valid if isSinglecStringRef() is true.
    cStringRef getSingleStringRef() const {
      assert(isSingleStringRef() &&"This cannot be had as a single cStringRef!");
      switch (getLHSKind()) {
			default: assert(false);
      case EmptyKind:      return cStringRef();
      case CStringKind:    return cStringRef(LHS.cString);
      case StdStringKind:  return cStringRef(*LHS.stdString);
      case StringRefKind:  return *LHS.cStringRef;
      }
    }

    /// tocStringRef - This returns the twine as a single cStringRef if it can be
    /// represented as such. Otherwise the twine is written into the given
    /// cSmallVector and a cStringRef to the cSmallVector's data is returned.
    cStringRef toStringRef(cSmallVectorImpl<char> &Out) const;

    /// toNullTerminatedcStringRef - This returns the twine as a single null
    /// terminated cStringRef if it can be represented as such. Otherwise the
    /// twine is written into the given cSmallVector and a cStringRef to the
    /// cSmallVector's data is returned.
    ///
    /// The returned cStringRef's size does not include the null terminator.
    cStringRef toNullTerminatedStringRef(cSmallVectorImpl<char> &Out) const;

    /// Write the concatenated string represented by this twine to the
    /// stream \p OS.
    void print(raw_ostream &OS) const;

    /// Dump the concatenated string represented by this twine to stderr.
    void dump() const;

    /// Write the representation of this twine to the stream \p OS.
    void printRepr(raw_ostream &OS) const;

    /// Dump the representation of this twine to stderr.
    void dumpRepr() const;

    /// @}
  };

  /// @name Twine Inline Implementations
  /// @{

  inline Twine Twine::concat(const Twine &Suffix) const {
    // Concatenation with null is null.
    if (isNull() || Suffix.isNull())
      return Twine(NullKind);

    // Concatenation with empty yields the other side.
    if (isEmpty())
      return Suffix;
    if (Suffix.isEmpty())
      return *this;

    // Otherwise we need to create a new node, taking care to fold in unary
    // twines.
    Child NewLHS, NewRHS;
    NewLHS.twine = this;
    NewRHS.twine = &Suffix;
    NodeKind NewLHSKind = TwineKind, NewRHSKind = TwineKind;
    if (isUnary()) {
      NewLHS = LHS;
      NewLHSKind = getLHSKind();
    }
    if (Suffix.isUnary()) {
      NewRHS = Suffix.LHS;
      NewRHSKind = Suffix.getLHSKind();
    }

    return Twine(NewLHS, NewLHSKind, NewRHS, NewRHSKind);
  }

  inline Twine operator+(const Twine &LHS, const Twine &RHS) {
    return LHS.concat(RHS);
  }

  /// Additional overload to guarantee simplified codegen; this is equivalent to
  /// concat().

  inline Twine operator+(const char *LHS, const cStringRef &RHS) {
    return Twine(LHS, RHS);
  }

  /// Additional overload to guarantee simplified codegen; this is equivalent to
  /// concat().

  inline Twine operator+(const cStringRef &LHS, const char *RHS) {
    return Twine(LHS, RHS);
  }

  inline raw_ostream &operator<<(raw_ostream &OS, const Twine &RHS) {
    RHS.print(OS);
    return OS;
  }

  /// @}
}
