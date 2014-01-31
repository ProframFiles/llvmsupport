//===--- akjArrayRef.hpp - Array Reference Wrapper -------------------*- C++ -*-===//
//
//           originally from The LLVM Compiler Infrastructure
//
// Was distributed under the University of Illinois Open Source License.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "None.hpp"

#include "SmallVector.hpp"
#include "CompilerFeatures.hpp"
#include <vector>

namespace akj {

  /// ArrayRef - Represent a constant reference to an array (0 or more elements
  /// consecutively in memory), i.e. a start pointer and a length.  It allows
  /// various APIs to take consecutive elements easily and conveniently.
  ///
  /// This class does not own the underlying data, it is expected to be used in
  /// situations where the data resides in some other buffer, whose lifetime
  /// extends past that of the ArrayRef. For this reason, it is not in general
  /// safe to store an ArrayRef.
  ///
  /// This is intended to be trivially copyable, so it should be passed by
  /// value.
  template<typename T>
  class cArrayRef {
  public:
    typedef const T *iterator;
    typedef const T *const_iterator;
    typedef size_t size_type;

    typedef std::reverse_iterator<iterator> reverse_iterator;

  private:
    /// The start of the array, in an external buffer.
    const T *Data;

    /// The number of elements.
    size_type Length;

  public:
    /// @name Constructors
    /// @{

    /// Construct an empty ArrayRef.
    /*implicit*/ cArrayRef() : Data(0), Length(0) {}

    /// Construct an empty ArrayRef from None.
    /*implicit*/ cArrayRef(NoneType) : Data(0), Length(0) {}

    /// Construct an ArrayRef from a single element.
    /*implicit*/ cArrayRef(const T &OneElt)
      : Data(&OneElt), Length(1) {}

    /// Construct an ArrayRef from a pointer and length.
    /*implicit*/ cArrayRef(const T *data, size_t length)
      : Data(data), Length(length) {}

    /// Construct an ArrayRef from a range.
    cArrayRef(const T *begin, const T *end)
      : Data(begin), Length(end - begin) {}

    /// Construct an ArrayRef from a SmallVector. This is templated in order to
    /// avoid instantiating SmallVectorTemplateCommon<T> whenever we
    /// copy-construct an ArrayRef.
    template<typename U>
    /*implicit*/ cArrayRef(const cSmallVectorTemplateCommon<T, U> &Vec)
      : Data(Vec.data()), Length(Vec.size()) {
    }

    /// Construct an ArrayRef from a std::vector.
    template<typename A>
    /*implicit*/ cArrayRef(const std::vector<T, A> &Vec)
      : Data(Vec.empty() ? (T*)0 : &Vec[0]), Length(Vec.size()) {}

    /// Construct an ArrayRef from a C array.
    template <size_t N>
    /*implicit*/ AKJ_CONSTEXPR cArrayRef(const T (&Arr)[N])
      : Data(Arr), Length(N) {}

    /// @}
    /// @name Simple Operations
    /// @{

    iterator begin() const { return Data; }
    iterator end() const { return Data + Length; }

    reverse_iterator rbegin() const { return reverse_iterator(end()); }
    reverse_iterator rend() const { return reverse_iterator(begin()); }

    /// empty - Check if the array is empty.
    bool empty() const { return Length == 0; }

    const T *data() const { return Data; }

    /// size - Get the array size.
    size_t size() const { return Length; }

    /// front - Get the first element.
    const T &front() const {
      assert(!empty());
      return Data[0];
    }

    /// back - Get the last element.
    const T &back() const {
      assert(!empty());
      return Data[Length-1];
    }

    /// equals - Check for element-wise equality.
    bool equals(cArrayRef RHS) const {
      if (Length != RHS.Length)
        return false;
      for (size_type i = 0; i != Length; i++)
        if (Data[i] != RHS.Data[i])
          return false;
      return true;
    }

    /// slice(n) - Chop off the first N elements of the array.
    cArrayRef<T> slice(unsigned N) const {
      assert(N <= size() && "Invalid specifier");
      return cArrayRef<T>(data()+N, size()-N);
    }

    /// slice(n, m) - Chop off the first N elements of the array, and keep M
    /// elements in the array.
    cArrayRef<T> slice(unsigned N, unsigned M) const {
      assert(N+M <= size() && "Invalid specifier");
      return cArrayRef<T>(data()+N, M);
    }

    /// @}
    /// @name Operator Overloads
    /// @{
    const T &operator[](size_t Index) const {
      assert(Index < Length && "Invalid index!");
      return Data[Index];
    }

    /// @}
    /// @name Expensive Operations
    /// @{
    std::vector<T> vec() const {
      return std::vector<T>(Data, Data+Length);
    }

    /// @}
    /// @name Conversion operators
    /// @{
    operator std::vector<T>() const {
      return std::vector<T>(Data, Data+Length);
    }

    /// @}
  };

  /// MutableArrayRef - Represent a mutable reference to an array (0 or more
  /// elements consecutively in memory), i.e. a start pointer and a length.  It
  /// allows various APIs to take and modify consecutive elements easily and
  /// conveniently.
  ///
  /// This class does not own the underlying data, it is expected to be used in
  /// situations where the data resides in some other buffer, whose lifetime
  /// extends past that of the MutableArrayRef. For this reason, it is not in
  /// general safe to store a MutableArrayRef.
  ///
  /// This is intended to be trivially copyable, so it should be passed by
  /// value.
  template<typename T>
  class cMutableArrayRef : public cArrayRef<T> {
  public:
    typedef T *iterator;

    /// Construct an empty MutableArrayRef.
    /*implicit*/ cMutableArrayRef() : cArrayRef<T>() {}

    /// Construct an empty MutableArrayRef from None.
    /*implicit*/ cMutableArrayRef(NoneType) : cArrayRef<T>() {}

    /// Construct an MutableArrayRef from a single element.
    /*implicit*/ cMutableArrayRef(T &OneElt) : cArrayRef<T>(OneElt) {}

    /// Construct an MutableArrayRef from a pointer and length.
    /*implicit*/ cMutableArrayRef(T *data, size_t length)
      : cArrayRef<T>(data, length) {}

    /// Construct an MutableArrayRef from a range.
    cMutableArrayRef(T *begin, T *end) : cArrayRef<T>(begin, end) {}

    /// Construct an MutableArrayRef from a SmallVector.
    /*implicit*/ cMutableArrayRef(cSmallVectorImpl<T> &Vec)
    : cArrayRef<T>(Vec) {}

    /// Construct a MutableArrayRef from a std::vector.
    /*implicit*/ cMutableArrayRef(std::vector<T> &Vec)
    : cArrayRef<T>(Vec) {}

    /// Construct an MutableArrayRef from a C array.
    template <size_t N>
    /*implicit*/ cMutableArrayRef(T (&Arr)[N])
      : cArrayRef<T>(Arr) {}

    T *data() const { return const_cast<T*>(cArrayRef<T>::data()); }

    iterator begin() const { return data(); }
    iterator end() const { return data() + this->size(); }

    /// front - Get the first element.
    T &front() const {
      assert(!this->empty());
      return data()[0];
    }

    /// back - Get the last element.
    T &back() const {
      assert(!this->empty());
      return data()[this->size()-1];
    }

    /// slice(n) - Chop off the first N elements of the array.
    cMutableArrayRef<T> slice(unsigned N) const {
      assert(N <= this->size() && "Invalid specifier");
      return cMutableArrayRef<T>(data()+N, this->size()-N);
    }

    /// slice(n, m) - Chop off the first N elements of the array, and keep M
    /// elements in the array.
    cMutableArrayRef<T> slice(unsigned N, unsigned M) const {
      assert(N+M <= this->size() && "Invalid specifier");
      return cMutableArrayRef<T>(data()+N, M);
    }

    /// @}
    /// @name Operator Overloads
    /// @{
    T &operator[](size_t Index) const {
      assert(Index < this->size() && "Invalid index!");
      return data()[Index];
    }
  };

  /// @name ArrayRef Convenience constructors
  /// @{

  /// Construct an ArrayRef from a single element.
  template<typename T>
  cArrayRef<T> makeArrayRef(const T &OneElt) {
    return OneElt;
  }

  /// Construct an ArrayRef from a pointer and length.
  template<typename T>
  cArrayRef<T> makeArrayRef(const T *data, size_t length) {
    return cArrayRef<T>(data, length);
  }

  /// Construct an ArrayRef from a range.
  template<typename T>
  cArrayRef<T> makeArrayRef(const T *begin, const T *end) {
    return cArrayRef<T>(begin, end);
  }

  /// Construct an ArrayRef from a SmallVector.
  template <typename T>
  cArrayRef<T> makeArrayRef(const cSmallVectorImpl<T> &Vec) {
    return Vec;
  }

  /// Construct an ArrayRef from a SmallVector.
  template <typename T, unsigned N>
  cArrayRef<T> makeArrayRef(const cSmallVector<T, N> &Vec) {
    return Vec;
  }

  /// Construct an ArrayRef from a std::vector.
  template<typename T>
  cArrayRef<T> makeArrayRef(const std::vector<T> &Vec) {
    return Vec;
  }

  /// Construct an ArrayRef from a C array.
  template<typename T, size_t N>
  cArrayRef<T> makeArrayRef(const T (&Arr)[N]) {
    return cArrayRef<T>(Arr);
  }

  /// @}
  /// @name ArrayRef Comparison Operators
  /// @{

  template<typename T>
  inline bool operator==(cArrayRef<T> LHS, cArrayRef<T> RHS) {
    return LHS.equals(RHS);
  }

  template<typename T>
  inline bool operator!=(cArrayRef<T> LHS, cArrayRef<T> RHS) {
    return !(LHS == RHS);
  }

  /// @}

  // ArrayRefs can be treated like a POD type.
  template <typename T> struct isPodLike;
  template <typename T> struct isPodLike<cArrayRef<T> > {
    static const bool value = true;
  };
}
