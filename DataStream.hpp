//===---- akjDataStream.hpp - Lazy bitcode streaming ----*- C++ -*-===//
//
//               Originally from The LLVM Compiler Infrastructure
//
// This file was distributed under the University of Illinois Open Source
// License.
//
//===----------------------------------------------------------------------===//
//
// This header defines DataStreamer, which fetches bytes of data from
// a stream source. It provides support for streaming (lazy reading) of
// data, e.g. bitcode
//
//===----------------------------------------------------------------------===//


#pragma once

#include <string>

namespace akj {

class DataStreamer {
public:
  /// Fetch bytes [start-end) from the stream, and write them to the
  /// buffer pointed to by buf. Returns the number of bytes actually written.
  virtual size_t GetBytes(unsigned char *buf, size_t len) = 0;

  virtual ~DataStreamer();
};

DataStreamer *getDataFileStreamer(const std::string &Filename,
                                  std::string *Err);

}

