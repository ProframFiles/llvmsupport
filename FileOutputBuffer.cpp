//===- akjFileOutputBuffer.cpp - File Output Buffer -------------*- C++ -*-===//
//
//           originally from The LLVM Compiler Infrastructure
//
// Was distributed under the University of Illinois Open Source License.
//
//===----------------------------------------------------------------------===//
//
// Utility for creating a in-memory buffer that will be written to a file.
//
//===----------------------------------------------------------------------===//

#include "FileOutputBuffer.hpp"
#include "OwningPtr.hpp"
#include "SmallVector.hpp"
#include "RawOstream.hpp"
#include "SystemError.hpp"

using akj::sys::fs::mapped_file_region;

namespace akj {
FileOutputBuffer::FileOutputBuffer(mapped_file_region * R,
                                   cStringRef Path, cStringRef TmpPath)
  : Region(R)
  , FinalPath(Path)
  , TempPath(TmpPath) {
}

FileOutputBuffer::~FileOutputBuffer() {
  bool Existed;
  sys::fs::remove(Twine(TempPath), Existed);
}

error_code FileOutputBuffer::create(cStringRef FilePath,
                                    size_t Size,
                                    OwningPtr<FileOutputBuffer> &Result,
                                    unsigned Flags) {
  // If file already exists, it must be a regular file (to be mappable).
  sys::fs::file_status Stat;
  error_code EC = sys::fs::status(FilePath, Stat);
  switch (Stat.type()) {
    case sys::fs::file_type::file_not_found:
      // If file does not exist, we'll create one.
      break;
    case sys::fs::file_type::regular_file: {
        // If file is not currently writable, error out.
        // FIXME: There is no sys::fs:: api for checking this.
        // FIXME: In posix, you use the access() call to check this.
      }
      break;
    default:
      if (EC)
        return EC;
      else
        return make_error_code(errc::operation_not_permitted);
  }

  // Delete target file.
  bool Existed;
  EC = sys::fs::remove(FilePath, Existed);
  if (EC)
    return EC;

  unsigned Mode = sys::fs::all_read | sys::fs::all_write;
  // If requested, make the output file executable.
  if (Flags & F_executable)
    Mode |= sys::fs::all_exe;

  // Create new file in same directory but with random name.
  SmallString<128> TempFilePath;
  int FD;
  EC = sys::fs::createUniqueFile(Twine(FilePath) + ".tmp%%%%%%%", FD,
                                 TempFilePath, Mode);
  if (EC)
    return EC;

  OwningPtr<mapped_file_region> MappedFile(new mapped_file_region(
      FD, true, mapped_file_region::readwrite, Size, 0, EC));
  if (EC)
    return EC;

  Result.reset(new FileOutputBuffer(MappedFile.get(), FilePath, TempFilePath));
  if (Result)
    MappedFile.take();

  return error_code::success();
}

error_code FileOutputBuffer::commit(int64_t NewSmallerSize) {
  // Unmap buffer, letting OS flush dirty pages to file on disk.
  Region.reset(0);

  // If requested, resize file as part of commit.
  if ( NewSmallerSize != -1 ) {
    error_code EC = sys::fs::resize_file(Twine(TempPath), NewSmallerSize);
    if (EC)
      return EC;
  }

  // Rename file to final name.
  return sys::fs::rename(Twine(TempPath), Twine(FinalPath));
}
} // namespace
