//===- Win32/Win32.h - Common Win32 Include File ----------------*- C++ -*-===//
//
//           originally from The LLVM Compiler Infrastructure
//
// Was distributed under the University of Illinois Open Source License.
//
//===----------------------------------------------------------------------===//
//
// This file defines things specific to Win32 implementations.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//=== WARNING: Implementation here must contain only generic Win32 code that
//===          is guaranteed to work on *all* Win32 variants.
//===----------------------------------------------------------------------===//
#pragma once

// mingw-w64 tends to define it as 0x0502 in its headers.
#undef _WIN32_WINNT

// Require at least Windows XP(5.1) API.
#define _WIN32_WINNT 0x0501
#define _WIN32_IE    0x0600 // MinGW at it again.
#define WIN32_LEAN_AND_MEAN

#include "SmallVector.hpp"

#include "StringRef.hpp"
#include "CompilerFeatures.hpp"
#include "SystemError.hpp"
#include <windows.h>
#include <wincrypt.h>
#include <shlobj.h>
#include <cassert>
#include <string>
#include <vector>

inline bool MakeErrMsg(std::string* ErrMsg, const std::string& prefix) {
  if (!ErrMsg)
    return true;
  char *buffer = NULL;
  FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM,
      NULL, GetLastError(), 0, (LPSTR)&buffer, 1, NULL);
  *ErrMsg = prefix + buffer;
  LocalFree(buffer);
  return true;
}

template <typename HandleTraits>
class ScopedHandle {
  typedef typename HandleTraits::handle_type handle_type;
  handle_type Handle;

  ScopedHandle(const ScopedHandle &other); // = delete;
  void operator=(const ScopedHandle &other); // = delete;
public:
  ScopedHandle()
    : Handle(HandleTraits::GetInvalid()) {}

  explicit ScopedHandle(handle_type h)
    : Handle(h) {}

  ~ScopedHandle() {
    if (HandleTraits::IsValid(Handle))
      HandleTraits::Close(Handle);
  }

  handle_type take() {
    handle_type t = Handle;
    Handle = HandleTraits::GetInvalid();
    return t;
  }

  ScopedHandle &operator=(handle_type h) {
    if (HandleTraits::IsValid(Handle))
      HandleTraits::Close(Handle);
    Handle = h;
    return *this;
  }

  // True if Handle is valid.
  explicit operator bool() const {
    return HandleTraits::IsValid(Handle) ? true : false;
  }

  operator handle_type() const {
    return Handle;
  }
};

struct CommonHandleTraits {
  typedef HANDLE handle_type;

  static handle_type GetInvalid() {
    return INVALID_HANDLE_VALUE;
  }

  static void Close(handle_type h) {
    ::CloseHandle(h);
  }

  static bool IsValid(handle_type h) {
    return h != GetInvalid();
  }
};

struct JobHandleTraits : CommonHandleTraits {
  static handle_type GetInvalid() {
    return NULL;
  }
};

struct CryptContextTraits : CommonHandleTraits {
  typedef HCRYPTPROV handle_type;

  static handle_type GetInvalid() {
    return 0;
  }

  static void Close(handle_type h) {
    ::CryptReleaseContext(h, 0);
  }

  static bool IsValid(handle_type h) {
    return h != GetInvalid();
  }
};

struct FindHandleTraits : CommonHandleTraits {
  static void Close(handle_type h) {
    ::FindClose(h);
  }
};

struct FileHandleTraits : CommonHandleTraits {};

typedef ScopedHandle<CommonHandleTraits> ScopedCommonHandle;
typedef ScopedHandle<FileHandleTraits>   ScopedFileHandle;
typedef ScopedHandle<CryptContextTraits> ScopedCryptContext;
typedef ScopedHandle<FindHandleTraits>   ScopedFindHandle;
typedef ScopedHandle<JobHandleTraits>    ScopedJobHandle;

namespace akj {
template <class T>
class cSmallVectorImpl;

template <class T>
typename cSmallVectorImpl<T>::const_pointer
c_str(cSmallVectorImpl<T> &str) {
  str.push_back(0);
  str.pop_back();
  return str.data();
}

namespace sys {
namespace windows {
error_code UTF8ToUTF16(cStringRef utf8,
                       cSmallVectorImpl<wchar_t> &utf16);
error_code UTF16ToUTF8(const wchar_t *utf16, size_t utf16_len,
                       cSmallVectorImpl<char> &utf8);
} // end namespace windows
} // end namespace sys
} // end namespace llvm.
