//===-- akjCompression.hpp ---Compression----------------*- C++ -*-===//
//
//           originally from The LLVM Compiler Infrastructure
//
// Was distributed under the University of Illinois Open Source License.
//
//===----------------------------------------------------------------------===//
//
// This file contains basic functions for compression/uncompression.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

namespace akj {

class MemoryBuffer;
template<typename T> class OwningPtr;
class cStringRef;

namespace zlib {

enum CompressionLevel {
  NoCompression,
  DefaultCompression,
  BestSpeedCompression,
  BestSizeCompression
};

enum Status {
  StatusOK,
  StatusUnsupported,  // zlib is unavaliable
  StatusOutOfMemory,  // there was not enough memory
  StatusBufferTooShort,  // there was not enough room in the output buffer
  StatusInvalidArg,  // invalid input parameter
  StatusInvalidData  // data was corrupted or incomplete
};

Status compress(cStringRef InputBuffer,
                OwningPtr<MemoryBuffer> &CompressedBuffer,
                CompressionLevel Level = DefaultCompression);

Status uncompress(cStringRef InputBuffer,
                  OwningPtr<MemoryBuffer> &UncompressedBuffer,
                  size_t UncompressedSize);

uint32_t crc32(cStringRef Buffer);

}  // End of namespace zlib

namespace lz4 {
	int compress(cStringRef InputBuffer,
		OwningPtr<MemoryBuffer> &CompressedBuffer);
}


} // End of namespace llvm


