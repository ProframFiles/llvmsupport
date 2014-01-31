//===-- akjNone.hpp - Simple null value for implicit construction ----*- C++ -*-=//
//
//           originally from The LLVM Compiler Infrastructure
//
// Was distributed under the University of Illinois Open Source License.
//
//===----------------------------------------------------------------------===//
//
//  This file provides None, an enumerator for use in implicit constructors
//  of various (usually templated) types to make such construction more
//  terse.
//
//===----------------------------------------------------------------------===//

#pragma once

namespace akj {
/// \brief A simple null object to allow implicit construction of Optional<T>
/// and similar types without having to spell out the specialization's name.
enum NoneType {
  None
};
}
