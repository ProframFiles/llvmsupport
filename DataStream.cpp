//===--- llvm/Support/DataStream.cpp - Lazy streamed data -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements DataStreamer, which fetches bytes of Data from
// a stream source. It provides support for streaming (lazy reading) of
// bitcode. An example implementation of streaming from a file or stdin
// is included.
//
//===----------------------------------------------------------------------===//

#include "DataStream.hpp"
#include "FileSystem.hpp"
#include "ProgramUtils.hpp"
#include "SystemError.hpp"
#include <cerrno>
#include <cstdio>
#include <string>
#if !defined(_MSC_VER) && !defined(__MINGW32__)
#include <unistd.h>
#else
#include <io.h>
#endif
using namespace akj;

// Interface goals:
// * StreamableMemoryObject doesn't care about complexities like using
//   threads/async callbacks to actually overlap download+compile
// * Don't want to duplicate Data in memory
// * Don't need to know total Data len in advance
// Non-goals:
// StreamableMemoryObject already has random access so this interface only does
// in-order streaming (no arbitrary seeking, else we'd have to buffer all the
// Data here in addition to MemoryObject).  This also means that if we want
// to be able to to free Data, BitstreamBytes/BitcodeReader will implement it


namespace akj {
DataStreamer::~DataStreamer() {}
}

namespace {

// Very simple stream backed by a file. Mostly useful for stdin and debugging;
// actual file access is probably still best done with mmap.
class DataFileStreamer : public DataStreamer {
 int Fd;
public:
  DataFileStreamer() : Fd(0) {}
  virtual ~DataFileStreamer() {
    close(Fd);
  }
  virtual size_t GetBytes(unsigned char *buf, size_t len) AKJ_OVERRIDE {
    return read(Fd, buf, len);
  }

  error_code OpenFile(const std::string &Filename) {
    if (Filename == "-") {
      Fd = 0;
      sys::ChangeStdinToBinary();
      return error_code::success();
    }

    return sys::fs::openFileForRead(Filename, Fd);
  }
};

}

namespace akj {
DataStreamer *getDataFileStreamer(const std::string &Filename,
                                  std::string *StrError) {
  DataFileStreamer *s = new DataFileStreamer();
  if (error_code e = s->OpenFile(Filename)) {
    *StrError = std::string("Could not open ") + Filename + ": " +
        e.message() + "\n";
    return NULL;
  }
  return s;
}

}
