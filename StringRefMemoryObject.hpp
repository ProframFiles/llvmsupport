//===- llvm/Support/StringRefMemoryObject.h ---------------------*- C++ -*-===//
//
//           originally from The LLVM Compiler Infrastructure
//
// Was distributed under the University of Illinois Open Source License.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the StringRefMemObject class, a simple
// wrapper around StringRef implementing the MemoryObject interface.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "StringRef.hpp"

#include "CompilerFeatures.hpp"
#include "MemoryObject.hpp"

namespace akj {

/// StringRefMemoryObject - Simple StringRef-backed MemoryObject
class StringRefMemoryObject : public MemoryObject {
  cStringRef Bytes;
  uint64_t Base;
public:
  StringRefMemoryObject(cStringRef Bytes, uint64_t Base = 0)
    : Bytes(Bytes), Base(Base) {}

  uint64_t getBase() const AKJ_OVERRIDE { return Base; }
  uint64_t getExtent() const AKJ_OVERRIDE { return Bytes.size(); }

  int readByte(uint64_t Addr, uint8_t *Byte) const AKJ_OVERRIDE;
  int readBytes(uint64_t Addr, uint64_t Size, uint8_t *Buf) const AKJ_OVERRIDE;
};

}
