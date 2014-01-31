//===--- Compression.cpp - Compression implementation ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements compression functions.
//
//===----------------------------------------------------------------------===//

#include "Compression.hpp"
#include "OwningPtr.hpp"
#include "StringRef.hpp"
#include "CompilerFeatures.hpp"
#include "FatalError.hpp"
#include "MemoryBuffer.hpp"
#include <zlib.h>
#include "lz4.h"

namespace akj
{

static int encodeZlibCompressionLevel(zlib::CompressionLevel Level) {
  switch (Level) {
    case zlib::NoCompression: return 0;
    case zlib::BestSpeedCompression: return 1;
    case zlib::DefaultCompression: return Z_DEFAULT_COMPRESSION;
    case zlib::BestSizeCompression: return 9;
  }
  FatalError::Die("Invalid zlib::CompressionLevel!");
}

static zlib::Status encodeZlibReturnValue(int ReturnValue) {
  switch (ReturnValue) {
    case Z_OK: return zlib::StatusOK;
    case Z_MEM_ERROR: return zlib::StatusOutOfMemory;
    case Z_BUF_ERROR: return zlib::StatusBufferTooShort;
    case Z_STREAM_ERROR: return zlib::StatusInvalidArg;
    case Z_DATA_ERROR: return zlib::StatusInvalidData;
  }
  FatalError::Die("unknown zlib return status!");
  return zlib::StatusInvalidArg;
}

zlib::Status zlib::compress(cStringRef InputBuffer,
                            OwningPtr<MemoryBuffer> &CompressedBuffer,
                            CompressionLevel Level) {
  unsigned long CompressedSize = ::compressBound(InputBuffer.size());
  OwningArrayPtr<char> TmpBuffer(new char[CompressedSize]);
  int CLevel = encodeZlibCompressionLevel(Level);
  Status Res = encodeZlibReturnValue(::compress2(
      (Bytef *)TmpBuffer.get(), &CompressedSize,
      (const Bytef *)InputBuffer.data(), InputBuffer.size(), CLevel));
  if (Res == StatusOK) {
    CompressedBuffer.reset(MemoryBuffer::getMemBufferCopy(
        cStringRef(TmpBuffer.get(), CompressedSize)));
    // Tell MSan that memory initialized by zlib is valid.
    __msan_unpoison(CompressedBuffer->getBufferStart(), CompressedSize);
  }
  return Res;
}

zlib::Status zlib::uncompress(cStringRef InputBuffer,
                              OwningPtr<MemoryBuffer> &UncompressedBuffer,
                              size_t UncompressedSize) {
  OwningArrayPtr<char> TmpBuffer(new char[UncompressedSize]);
  Status Res = encodeZlibReturnValue(
      ::uncompress((Bytef *)TmpBuffer.get(), (uLongf *)&UncompressedSize,
                   (const Bytef *)InputBuffer.data(), InputBuffer.size()));
  if (Res == StatusOK) {
    UncompressedBuffer.reset(MemoryBuffer::getMemBufferCopy(
        cStringRef(TmpBuffer.get(), UncompressedSize)));
    // Tell MSan that memory initialized by zlib is valid.
    __msan_unpoison(UncompressedBuffer->getBufferStart(), UncompressedSize);
  }
  return Res;
}

uint32_t zlib::crc32(cStringRef Buffer) {
  return ::crc32(0, (const Bytef *)Buffer.data(), Buffer.size());
}


	namespace lz4
	{
		int compress(cStringRef in_buffer,
			OwningPtr<MemoryBuffer> &CompressedBuffer)
		{
			const size_t buf_size = LZ4_compressBound(in_buffer.size());
			OwningArrayPtr<char> tmp_buf(new char[buf_size]);
			int compressed_size =
				LZ4_compress(in_buffer.data(), tmp_buf.get(), in_buffer.size());
			CompressedBuffer.reset(MemoryBuffer::getMemBufferCopy(
				cStringRef(tmp_buf.get(), compressed_size)));
			return compressed_size < 0 ? -1 : compressed_size;
		}
	}



/*
	LzmaCompress(unsigned char *dest, size_t *destLen, const unsigned char *src, size_t srcLen,
		unsigned char *outProps, size_t *outPropsSize, / * *outPropsSize must be = 5 * /
		int level,      / * 0 <= level <= 9, default = 5 * /
		unsigned dictSize,  / * default = (1 << 24) * /
		int lc,        / * 0 <= lc <= 8, default = 3  * /
		int lp,        / * 0 <= lp <= 4, default = 0  * /
		int pb,        / * 0 <= pb <= 4, default = 2  * /
		int fb,        / * 5 <= fb <= 273, default = 32 * /
		int numThreads / * 1 or 2, default = 2 * /
		*/
}
