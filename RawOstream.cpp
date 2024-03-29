//===--- akjRawOstream.cpp - Implement the raw_ostream classes ------------===//
//
//           originally from The LLVM Compiler Infrastructure
//
// Was distributed under the University of Illinois Open Source License.
//
//===----------------------------------------------------------------------===//
//
// This implements support for bulk buffered stream output.
//
//===----------------------------------------------------------------------===//

#include "RawOstream.hpp"

#include "STLExtras.hpp"
#include "SmallVector.hpp"
#include "StringExtras.hpp"
#include "CompilerFeatures.hpp"
#include "FatalError.hpp"
#include "FileSystem.hpp"
#include "Format.hpp"
#include "ProcessUtils.hpp"
#include "ProgramUtils.hpp"
#include "SystemError.hpp"
#include <cctype>
#include <cerrno>
#include <sys/stat.h>

// <fcntl.h> may provide O_BINARY.
#if defined(HAVE_FCNTL_H)
# include <fcntl.h>
#endif

#if defined(HAVE_UNISTD_H)
# include <unistd.h>
#endif
#if defined(HAVE_SYS_UIO_H) && defined(HAVE_WRITEV)
#  include <sys/uio.h>
#endif

#if defined(__CYGWIN__)
#include <io.h>
#endif

#if defined(_MSC_VER)
#include <io.h>
#ifndef STDIN_FILENO
# define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
# define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
# define STDERR_FILENO 2
#endif
#endif

using namespace akj;

raw_ostream::~raw_ostream() {
  // raw_ostream's subclasses should take care to flush the buffer
  // in their destructors.
  assert(OutBufCur == OutBufStart &&
         "raw_ostream destructor called with non-empty buffer!");

  if (BufferMode == InternalBuffer)
    delete [] OutBufStart;
}

// An out of line virtual method to provide a home for the class vtable.
void raw_ostream::handle() {}

size_t raw_ostream::preferred_buffer_size() const {
  // BUFSIZ is intended to be a reasonable default.
  return BUFSIZ;
}

void raw_ostream::SetBuffered() {
  // Ask the subclass to determine an appropriate buffer size.
  if (size_t Size = preferred_buffer_size())
    SetBufferSize(Size);
  else
    // It may return 0, meaning this stream should be unbuffered.
    SetUnbuffered();
}

void raw_ostream::SetBufferAndMode(char *BufferStart, size_t Size,
                                   BufferKind Mode) {
  assert(((Mode == Unbuffered && BufferStart == 0 && Size == 0) ||
          (Mode != Unbuffered && BufferStart && Size)) &&
         "stream must be unbuffered or have at least one byte");
  // Make sure the current buffer is free of content (we can't flush here; the
  // child buffer management logic will be in write_impl).
  assert(GetNumBytesInBuffer() == 0 && "Current buffer is non-empty!");

  if (BufferMode == InternalBuffer)
    delete [] OutBufStart;
  OutBufStart = BufferStart;
  OutBufEnd = OutBufStart+Size;
  OutBufCur = OutBufStart;
  BufferMode = Mode;

  assert(OutBufStart <= OutBufEnd && "Invalid size!");
}

raw_ostream &raw_ostream::operator<<(unsigned long N) {
  // Zero is a special case.
  if (N == 0)
    return *this << '0';

  char NumberBuffer[20];
  char *EndPtr = NumberBuffer+sizeof(NumberBuffer);
  char *CurPtr = EndPtr;

  while (N) {
    *--CurPtr = '0' + char(N % 10);
    N /= 10;
  }
  return write(CurPtr, EndPtr-CurPtr);
}

raw_ostream &raw_ostream::operator<<(long N) {
  if (N <  0) {
    *this << '-';
    // Avoid undefined behavior on LONG_MIN with a cast.
    N = -(unsigned long)N;
  }

  return this->operator<<(static_cast<unsigned long>(N));
}

raw_ostream &raw_ostream::operator<<(unsigned long long N) {
  // Output using 32-bit div/mod when possible.
  if (N == static_cast<unsigned long>(N))
    return this->operator<<(static_cast<unsigned long>(N));

  char NumberBuffer[20];
  char *EndPtr = NumberBuffer+sizeof(NumberBuffer);
  char *CurPtr = EndPtr;

  while (N) {
    *--CurPtr = '0' + char(N % 10);
    N /= 10;
  }
  return write(CurPtr, EndPtr-CurPtr);
}

raw_ostream &raw_ostream::operator<<(long long N) {
  if (N < 0) {
    *this << '-';
    // Avoid undefined behavior on INT64_MIN with a cast.
    N = -(unsigned long long)N;
  }

  return this->operator<<(static_cast<unsigned long long>(N));
}

raw_ostream &raw_ostream::write_hex(unsigned long long N) {
  // Zero is a special case.
  if (N == 0)
    return *this << '0';

  char NumberBuffer[20];
  char *EndPtr = NumberBuffer+sizeof(NumberBuffer);
  char *CurPtr = EndPtr;

  while (N) {
    uintptr_t x = N % 16;
    *--CurPtr = (x < 10 ? '0' + x : 'a' + x - 10);
    N /= 16;
  }

  return write(CurPtr, EndPtr-CurPtr);
}

raw_ostream &raw_ostream::write_escaped(cStringRef Str,
                                        bool UseHexEscapes) {
  for (unsigned i = 0, e = Str.size(); i != e; ++i) {
    unsigned char c = Str[i];

    switch (c) {
    case '\\':
      *this << '\\' << '\\';
      break;
    case '\t':
      *this << '\\' << 't';
      break;
    case '\n':
      *this << '\\' << 'n';
      break;
    case '"':
      *this << '\\' << '"';
      break;
    default:
      if (std::isprint(c)) {
        *this << c;
        break;
      }

      // Write out the escaped representation.
      if (UseHexEscapes) {
        *this << '\\' << 'x';
        *this << hexdigit((c >> 4 & 0xF));
        *this << hexdigit((c >> 0) & 0xF);
      } else {
        // Always use a full 3-character octal escape.
        *this << '\\';
        *this << char('0' + ((c >> 6) & 7));
        *this << char('0' + ((c >> 3) & 7));
        *this << char('0' + ((c >> 0) & 7));
      }
    }
  }

  return *this;
}

raw_ostream &raw_ostream::operator<<(const void *P) {
  *this << '0' << 'x';

  return write_hex((uintptr_t) P);
}

raw_ostream &raw_ostream::operator<<(double N) {
#ifdef _WIN32
  // On MSVCRT and compatible, output of %e is incompatible to Posix
  // by default. Number of exponent digits should be at least 2. "%+03d"
  // FIXME: Implement our formatter to here or Support/Format.h!
  int fpcl = _fpclass(N);

  // negative zero
  if (fpcl == _FPCLASS_NZ)
    return *this << "-0.000000e+00";

  char buf[16];
  unsigned len;
  len = snprintf(buf, sizeof(buf), "%e", N);
  if (len <= sizeof(buf) - 2) {
    if (len >= 5 && buf[len - 5] == 'e' && buf[len - 3] == '0') {
      int cs = buf[len - 4];
      if (cs == '+' || cs == '-') {
        int c1 = buf[len - 2];
        int c0 = buf[len - 1];
        if (isdigit(static_cast<unsigned char>(c1)) &&
            isdigit(static_cast<unsigned char>(c0))) {
          // Trim leading '0': "...e+012" -> "...e+12\0"
          buf[len - 3] = c1;
          buf[len - 2] = c0;
          buf[--len] = 0;
        }
      }
    }
    return this->operator<<(buf);
  }
#endif
  return this->operator<<(format("%e", N));
}



void raw_ostream::flush_nonempty() {
  assert(OutBufCur > OutBufStart && "Invalid call to flush_nonempty.");
  size_t Length = OutBufCur - OutBufStart;
  OutBufCur = OutBufStart;
  write_impl(OutBufStart, Length);
}

raw_ostream &raw_ostream::write(unsigned char C) {
  // Group exceptional cases into a single branch.
  if (AKJ_UNLIKELY(OutBufCur >= OutBufEnd)) {
    if (AKJ_UNLIKELY(!OutBufStart)) {
      if (BufferMode == Unbuffered) {
        write_impl(reinterpret_cast<char*>(&C), 1);
        return *this;
      }
      // Set up a buffer and start over.
      SetBuffered();
      return write(C);
    }

    flush_nonempty();
  }

  *OutBufCur++ = C;
  return *this;
}

raw_ostream &raw_ostream::write(const char *Ptr, size_t Size) {
  // Group exceptional cases into a single branch.
  if (AKJ_UNLIKELY(size_t(OutBufEnd - OutBufCur) < Size)) {
    if (AKJ_UNLIKELY(!OutBufStart)) {
      if (BufferMode == Unbuffered) {
        write_impl(Ptr, Size);
        return *this;
      }
      // Set up a buffer and start over.
      SetBuffered();
      return write(Ptr, Size);
    }

    size_t NumBytes = OutBufEnd - OutBufCur;

    // If the buffer is empty at this point we have a string that is larger
    // than the buffer. Directly write the chunk that is a multiple of the
    // preferred buffer size and put the remainder in the buffer.
    if (AKJ_UNLIKELY(OutBufCur == OutBufStart)) {
      size_t BytesToWrite = Size - (Size % NumBytes);
      write_impl(Ptr, BytesToWrite);
      size_t BytesRemaining = Size - BytesToWrite;
      if (BytesRemaining > size_t(OutBufEnd - OutBufCur)) {
        // Too much left over to copy into our buffer.
        return write(Ptr + BytesToWrite, BytesRemaining);
      }
      copy_to_buffer(Ptr + BytesToWrite, BytesRemaining);
      return *this;
    }

    // We don't have enough space in the buffer to fit the string in. Insert as
    // much as possible, flush and start over with the remainder.
    copy_to_buffer(Ptr, NumBytes);
    flush_nonempty();
    return write(Ptr + NumBytes, Size - NumBytes);
  }

  copy_to_buffer(Ptr, Size);

  return *this;
}

void raw_ostream::copy_to_buffer(const char *Ptr, size_t Size) {
  assert(Size <= size_t(OutBufEnd - OutBufCur) && "Buffer overrun!");

  // Handle short strings specially, memcpy isn't very good at very short
  // strings.
  switch (Size) {
  case 4: OutBufCur[3] = Ptr[3]; // FALL THROUGH
  case 3: OutBufCur[2] = Ptr[2]; // FALL THROUGH
  case 2: OutBufCur[1] = Ptr[1]; // FALL THROUGH
  case 1: OutBufCur[0] = Ptr[0]; // FALL THROUGH
  case 0: break;
  default:
    memcpy(OutBufCur, Ptr, Size);
    break;
  }

  OutBufCur += Size;
}

// Formatted output.
raw_ostream &raw_ostream::operator<<(const format_object_base &Fmt) {
  // If we have more than a few bytes left in our output buffer, try
  // formatting directly onto its end.
  size_t NextBufferSize = 127;
  size_t BufferBytesLeft = OutBufEnd - OutBufCur;
  if (BufferBytesLeft > 3) {
    size_t BytesUsed = Fmt.print(OutBufCur, BufferBytesLeft);

    // Common case is that we have plenty of space.
    if (BytesUsed <= BufferBytesLeft) {
      OutBufCur += BytesUsed;
      return *this;
    }

    // Otherwise, we overflowed and the return value tells us the size to try
    // again with.
    NextBufferSize = BytesUsed;
  }

  // If we got here, we didn't have enough space in the output buffer for the
  // string.  Try printing into a cSmallVector that is resized to have enough
  // space.  Iterate until we win.
  cSmallVector<char, 128> V;

  while (1) {
    V.resize(NextBufferSize);

    // Try formatting into the cSmallVector.
    size_t BytesUsed = Fmt.print(V.data(), NextBufferSize);

    // If BytesUsed fit into the vector, we win.
    if (BytesUsed <= NextBufferSize)
      return write(V.data(), BytesUsed);

    // Otherwise, try again with a new size.
    assert(BytesUsed > NextBufferSize && "Didn't grow buffer!?");
    NextBufferSize = BytesUsed;
  }
}

/// indent - Insert 'NumSpaces' spaces.
raw_ostream &raw_ostream::indent(unsigned NumSpaces) {
  static const char Spaces[] = "                                "
                               "                                "
                               "                ";

  // Usually the indentation is small, handle it with a fastpath.
  if (NumSpaces < array_lengthof(Spaces))
    return write(Spaces, NumSpaces);

  while (NumSpaces) {
    unsigned NumToWrite = std::min(NumSpaces,
                                   (unsigned)array_lengthof(Spaces)-1);
    write(Spaces, NumToWrite);
    NumSpaces -= NumToWrite;
  }
  return *this;
}


//===----------------------------------------------------------------------===//
//  Formatted Output
//===----------------------------------------------------------------------===//

// Out of line virtual method.
void format_object_base::home() {
}

//===----------------------------------------------------------------------===//
//  raw_fd_ostream
//===----------------------------------------------------------------------===//

/// raw_fd_ostream - Open the specified file for writing. If an error
/// occurs, information about the error is put into ErrorInfo, and the
/// stream should be immediately destroyed; the string will be empty
/// if no error occurred.
raw_fd_ostream::raw_fd_ostream(const char *Filename, std::string &ErrorInfo,
                               sys::fs::OpenFlags Flags)
    : Error(false), UseAtomicWrites(false), pos(0) {
  assert(Filename != 0 && "Filename is null");
  ErrorInfo.clear();

  // Handle "-" as stdout. Note that when we do this, we consider ourself
  // the owner of stdout. This means that we can do things like close the
  // file descriptor when we're done and set the "binary" flag globally.
  if (Filename[0] == '-' && Filename[1] == 0) {
    FD = STDOUT_FILENO;
    // If user requested binary then put stdout into binary mode if
    // possible.
    if (Flags & sys::fs::F_Binary)
      sys::ChangeStdoutToBinary();
    // Close stdout when we're done, to detect any output errors.
    ShouldClose = true;
    return;
  }

  error_code EC = sys::fs::openFileForWrite(Filename, FD, Flags);

  if (EC) {
    ErrorInfo = "Error opening output file '" + std::string(Filename) + "'";
    ShouldClose = false;
    return;
  }

  // Ok, we successfully opened the file, so it'll need to be closed.
  ShouldClose = true;
}

/// raw_fd_ostream - Open the specified file for writing. If an error
/// occurs, information about the error is put into ErrorInfo, and the
/// stream will be immediately destroyed (this can only be called by 
/// CreateFileOutStream, which should do this);
raw_fd_ostream::raw_fd_ostream(const char *Filename, error_code &ErrorInfo,
	sys::fs::OpenFlags Flags)
	: Error(false), UseAtomicWrites(false), pos(0) {
	assert(Filename != 0 && "Filename is null");
	ErrorInfo.clear();

	// Handle "-" as stdout. Note that when we do this, we consider ourself
	// the owner of stdout. This means that we can do things like close the
	// file descriptor when we're done and set the "binary" flag globally.
	if (Filename[0] == '-' && Filename[1] == 0) {
		FD = STDOUT_FILENO;
		// If user requested binary then put stdout into binary mode if
		// possible.
		if (Flags & sys::fs::F_Binary)
			sys::ChangeStdoutToBinary();
		// Close stdout when we're done, to detect any output errors.
		ShouldClose = true;
		return;
	}

	ErrorInfo = sys::fs::openFileForWrite(Filename, FD, Flags);

	if (ErrorInfo) {
		ShouldClose = false;
		return ;
	}

	// Ok, we successfully opened the file, so it'll need to be closed.
	ShouldClose = true;
}

akj::error_code raw_fd_ostream::Create(const Twine& filename, 
	OwningPtr<raw_fd_ostream>& out_stream,
	sys::fs::OpenFlags Flags /*= sys::fs::F_None*/)
{
	error_code EC;

	raw_fd_ostream* os = new raw_fd_ostream(filename.str().c_str(), EC, Flags);
	if(EC)
	{
		delete os;
		return EC;
	}

	out_stream.reset(os);
	return EC;
}


/// raw_fd_ostream ctor - FD is the file descriptor that this writes to.  If
/// ShouldClose is true, this closes the file when the stream is destroyed.
raw_fd_ostream::raw_fd_ostream(int fd, bool shouldClose, bool unbuffered)
  : raw_ostream(unbuffered), FD(fd),
    ShouldClose(shouldClose), Error(false), UseAtomicWrites(false) {
#ifdef O_BINARY
  // Setting STDOUT and STDERR to binary mode is necessary in Win32
  // to avoid undesirable linefeed conversion.
  if (fd == STDOUT_FILENO || fd == STDERR_FILENO)
    setmode(fd, O_BINARY);
#endif

  // Get the starting position.
  off_t loc = ::lseek(FD, 0, SEEK_CUR);
  if (loc == (off_t)-1)
    pos = 0;
  else
    pos = static_cast<uint64_t>(loc);
}

raw_fd_ostream::~raw_fd_ostream() {
  if (FD >= 0) {
    flush();
    if (ShouldClose)
      while (::close(FD) != 0)
        if (errno != EINTR) {
          error_detected();
          break;
        }
  }

#ifdef __MINGW32__
  // On mingw, global dtors should not call exit().
  // report_fatal_error() invokes exit(). We know report_fatal_error()
  // might not write messages to stderr when any errors were detected
  // on FD == 2.
  if (FD == 2) return;
#endif

  // If there are any pending errors, report them now. Clients wishing
  // to avoid report_fatal_error calls should check for errors with
  // has_error() and clear the error flag with clear_error() before
  // destructing raw_ostream objects which may have errors.
  if (has_error())
    FatalError::Die("IO failure on output stream.");
}


void raw_fd_ostream::write_impl(const char *Ptr, size_t Size) {
  assert(FD >= 0 && "File already closed.");
  pos += Size;

  do {
    size_t ret;

    // Check whether we should attempt to use atomic writes.
    if (AKJ_LIKELY(!UseAtomicWrites)) {
      ret = ::write(FD, Ptr, Size);
    } else {
      // Use ::writev() where available.
#if defined(HAVE_WRITEV)
      const void *Addr = static_cast<const void *>(Ptr);
      struct iovec IOV = {const_cast<void *>(Addr), Size };
      ret = ::writev(FD, &IOV, 1);
#else
      ret = ::write(FD, Ptr, Size);
#endif
    }

    if (ret < 0) {
      // If it's a recoverable error, swallow it and retry the write.
      //
      // Ideally we wouldn't ever see EAGAIN or EWOULDBLOCK here, since
      // raw_ostream isn't designed to do non-blocking I/O. However, some
      // programs, such as old versions of bjam, have mistakenly used
      // O_NONBLOCK. For compatibility, emulate blocking semantics by
      // spinning until the write succeeds. If you don't want spinning,
      // don't use O_NONBLOCK file descriptors with raw_ostream.
      if (errno == EINTR || errno == EAGAIN
#ifdef EWOULDBLOCK
          || errno == EWOULDBLOCK
#endif
          )
        continue;

      // Otherwise it's a non-recoverable error. Note it and quit.
      error_detected();
      break;
    }

    // The write may have written some or all of the data. Update the
    // size and buffer pointer to reflect the remainder that needs
    // to be written. If there are no bytes left, we're done.
    Ptr += ret;
    Size -= ret;
  } while (Size > 0);
}

void raw_fd_ostream::close() {
  assert(ShouldClose);
  ShouldClose = false;
  flush();
  while (::close(FD) != 0)
    if (errno != EINTR) {
      error_detected();
      break;
    }
  FD = -1;
}

uint64_t raw_fd_ostream::seek(uint64_t off) {
  flush();
  pos = ::lseek(FD, off, SEEK_SET);
  if (pos != off)
    error_detected();
  return pos;
}

size_t raw_fd_ostream::preferred_buffer_size() const {
#if !defined(_MSC_VER) && !defined(__MINGW32__) && !defined(__minix)
  // Windows and Minix have no st_blksize.
  assert(FD >= 0 && "File not yet open!");
  struct stat statbuf;
  if (fstat(FD, &statbuf) != 0)
    return 0;

  // If this is a terminal, don't use buffering. Line buffering
  // would be a more traditional thing to do, but it's not worth
  // the complexity.
  if (S_ISCHR(statbuf.st_mode) && isatty(FD))
    return 0;
  // Return the preferred block size.
  return statbuf.st_blksize;
#else
  return raw_ostream::preferred_buffer_size();
#endif
}

raw_ostream &raw_fd_ostream::changeColor(enum Colors colors, bool bold,
                                         bool bg) {
  if (sys::Process::ColorNeedsFlush())
    flush();
  const char *colorcode =
    (colors == SAVEDCOLOR) ? sys::Process::OutputBold(bg)
    : sys::Process::OutputColor(colors, bold, bg);
  if (colorcode) {
    size_t len = strlen(colorcode);
    write(colorcode, len);
    // don't account colors towards output characters
    pos -= len;
  }
  return *this;
}

raw_ostream &raw_fd_ostream::resetColor() {
  if (sys::Process::ColorNeedsFlush())
    flush();
  const char *colorcode = sys::Process::ResetColor();
  if (colorcode) {
    size_t len = strlen(colorcode);
    write(colorcode, len);
    // don't account colors towards output characters
    pos -= len;
  }
  return *this;
}

raw_ostream &raw_fd_ostream::reverseColor() {
  if (sys::Process::ColorNeedsFlush())
    flush();
  const char *colorcode = sys::Process::OutputReverse();
  if (colorcode) {
    size_t len = strlen(colorcode);
    write(colorcode, len);
    // don't account colors towards output characters
    pos -= len;
  }
  return *this;
}

bool raw_fd_ostream::is_displayed() const {
  return sys::Process::FileDescriptorIsDisplayed(FD);
}

bool raw_fd_ostream::has_colors() const {
  return sys::Process::FileDescriptorHasColors(FD);
}



//===----------------------------------------------------------------------===//
//  outs(), errs(), nulls()
//===----------------------------------------------------------------------===//

/// outs() - This returns a reference to a raw_ostream for standard output.
/// Use it like: outs() << "foo" << "bar";
raw_ostream &akj::outs() {
  // Set buffer settings to model stdout behavior.
  // Delete the file descriptor when the program exists, forcing error
  // detection. If you don't want this behavior, don't use outs().
  static raw_fd_ostream S(STDOUT_FILENO, true);
  return S;
}

/// errs() - This returns a reference to a raw_ostream for standard error.
/// Use it like: errs() << "foo" << "bar";
raw_ostream &akj::errs() {
  // Set standard error to be unbuffered by default.
  static raw_fd_ostream S(STDERR_FILENO, false, true);
  return S;
}

/// nulls() - This returns a reference to a raw_ostream which discards output.
raw_ostream &akj::nulls() {
  static raw_null_ostream S;
  return S;
}


//===----------------------------------------------------------------------===//
//  raw_string_ostream
//===----------------------------------------------------------------------===//

raw_string_ostream::~raw_string_ostream() {
  flush();
}

void raw_string_ostream::write_impl(const char *Ptr, size_t Size) {
  OS.append(Ptr, Size);
}

//===----------------------------------------------------------------------===//
//  raw_svector_ostream
//===----------------------------------------------------------------------===//

// The raw_svector_ostream implementation uses the cSmallVector itself as the
// buffer for the raw_ostream. We guarantee that the raw_ostream buffer is
// always pointing past the end of the vector, but within the vector
// capacity. This allows raw_ostream to write directly into the correct place,
// and we only need to set the vector size when the data is flushed.

raw_svector_ostream::raw_svector_ostream(cSmallVectorImpl<char> &O) : OS(O) {
  // Set up the initial external buffer. We make sure that the buffer has at
  // least 128 bytes free; raw_ostream itself only requires 64, but we want to
  // make sure that we don't grow the buffer unnecessarily on destruction (when
  // the data is flushed). See the FIXME below.
  OS.reserve(OS.size() + 128);
  SetBuffer(OS.end(), OS.capacity() - OS.size());
}

raw_svector_ostream::~raw_svector_ostream() {
  // FIXME: Prevent resizing during this flush().
  flush();
}

/// resync - This is called when the cSmallVector we're appending to is changed
/// outside of the raw_svector_ostream's control.  It is only safe to do this
/// if the raw_svector_ostream has previously been flushed.
void raw_svector_ostream::resync() {
  assert(GetNumBytesInBuffer() == 0 && "Didn't flush before mutating vector");

  if (OS.capacity() - OS.size() < 64)
    OS.reserve(OS.capacity() * 2);
  SetBuffer(OS.end(), OS.capacity() - OS.size());
}

void raw_svector_ostream::write_impl(const char *Ptr, size_t Size) {
  // If we're writing bytes from the end of the buffer into the smallvector, we
  // don't need to copy the bytes, just commit the bytes because they are
  // already in the right place.
  if (Ptr == OS.end()) {
    assert(OS.size() + Size <= OS.capacity() && "Invalid write_impl() call!");
    OS.set_size(OS.size() + Size);
  } else {
    assert(GetNumBytesInBuffer() == 0 &&
           "Should be writing from buffer if some bytes in it");
    // Otherwise, do copy the bytes.
    OS.append(Ptr, Ptr+Size);
  }

  // Grow the vector if necessary.
  if (OS.capacity() - OS.size() < 64)
    OS.reserve(OS.capacity() * 2);

  // Update the buffer position.
  SetBuffer(OS.end(), OS.capacity() - OS.size());
}

uint64_t raw_svector_ostream::current_pos() const {
   return OS.size();
}

cStringRef raw_svector_ostream::str() {
  flush();
  return cStringRef(OS.begin(), OS.size());
}

//===----------------------------------------------------------------------===//
//  raw_null_ostream
//===----------------------------------------------------------------------===//

raw_null_ostream::~raw_null_ostream() {
#ifndef NDEBUG
  // ~raw_ostream asserts that the buffer is empty. This isn't necessary
  // with raw_null_ostream, but it's better to have raw_null_ostream follow
  // the rules than to change the rules just for raw_null_ostream.
  flush();
#endif
}

void raw_null_ostream::write_impl(const char *Ptr, size_t Size) {
}

uint64_t raw_null_ostream::current_pos() const {
  return 0;
}
