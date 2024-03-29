//===- Errno.cpp - errno support --------------------------------*- C++ -*-===//
//
//           originally from The LLVM Compiler Infrastructure
//
// Was distributed under the University of Illinois Open Source License.
//
//===----------------------------------------------------------------------===//
//
// This file implements the errno wrappers.
//
//===----------------------------------------------------------------------===//

#include "ErrnoToString.hpp"

#include "RawOstream.hpp"
#include <string.h>

#include <errno.h>


//===----------------------------------------------------------------------===//
//=== WARNING: Implementation here must contain only TRULY operating system
//===          independent code.
//===----------------------------------------------------------------------===//

namespace akj {
namespace sys {


std::string StrError() {
  return StrError(errno);
}


std::string StrError(int errnum) {
  const int MaxErrStrLen = 2000;
  char buffer[MaxErrStrLen];
  buffer[0] = '\0';
  std::string str;
  if (errnum == 0)
    return str;

#if (defined(__linux__) || defined(__APPLE__))
  // strerror_r is thread-safe.
#if defined(__GLIBC__) && defined(_GNU_SOURCE)
  // glibc defines its own incompatible version of strerror_r
  // which may not use the buffer supplied.
  str = strerror_r(errnum, buffer, MaxErrStrLen - 1);
#else
  strerror_r(errnum, buffer, MaxErrStrLen - 1);
  str = buffer;
#endif
#else //HAVE_DECL_STRERROR_S // "Windows Secure API"
  strerror_s(buffer, MaxErrStrLen - 1, errnum);
  str = buffer;
//#else
  // Copy the thread un-safe result of strerror into
  // the buffer as fast as possible to minimize impact
  // of collision of strerror in multiple threads.
  //str = strerror(errnum);
//#else
  // Strange that this system doesn't even have strerror
  // but, oh well, just use a generic message
  //raw_string_ostream stream(str);
  //stream << "Error #" << errnum;
 // stream.flush();
#endif
  return str;
}

}  // namespace sys
}  // namespace akj
