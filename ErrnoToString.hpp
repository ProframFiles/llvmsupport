//===- akjErrnoToString.h - Portable+convenient errno handling -*- C++ -*-===//
//
//           originally from The LLVM Compiler Infrastructure
//
// Was distributed under the University of Illinois Open Source License.
//
//===----------------------------------------------------------------------===//
//
// This file declares some portable and convenient functions to deal with errno.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

namespace akj {
namespace sys {

/// Returns a string representation of the errno value, using whatever
/// thread-safe variant of strerror() is available.  Be sure to call this
/// immediately after the function that set errno, or errno may have been
/// overwritten by an intervening call.
std::string StrError();

/// Like the no-argument version above, but uses \p errnum instead of errno.
std::string StrError(int errnum);

}  // namespace sys
}  // namespace llvm

