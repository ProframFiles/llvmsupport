//===- akjProgram.h ------------------------------------*- C++ -*-===//
//
//           originally from The LLVM Compiler Infrastructure
//
// Was distributed under the University of Illinois Open Source License.
//
//===----------------------------------------------------------------------===//
//
// This file declares the llvm::sys::Program class.
//
//===----------------------------------------------------------------------===//

#pragma once
#include "ArrayRef.hpp"

#include "Path.hpp"
#include "SystemError.hpp"

namespace akj {
class error_code;
namespace sys {

  /// This is the OS-specific separator for PATH like environment variables:
  // a colon on Unix or a semicolon on Windows.
#if defined (_WIN32)
	const char EnvPathSeparator = ';';
#else
  const char EnvPathSeparator = ':';
#endif


  /// This static constructor (factory) will attempt to locate a program in
  /// the operating system's file system using some pre-determined set of
  /// locations to search (e.g. the PATH on Unix). Paths with slashes are
  /// returned unmodified.
  /// @returns A Path object initialized to the path of the program or a
  /// Path object that is empty (invalid) if the program could not be found.
  /// @brief Construct a Program by finding it by name.
  std::string FindProgramByName(const std::string& name);

  // These functions change the specified standard stream (stdin, stdout, or
  // stderr) to binary mode. They return errc::success if the specified stream
  // was changed. Otherwise a platform dependent error is returned.
  error_code ChangeStdinToBinary();
  error_code ChangeStdoutToBinary();
  error_code ChangeStderrToBinary();

  /// This function executes the program using the arguments provided.  The
  /// invoked program will inherit the stdin, stdout, and stderr file
  /// descriptors, the environment and other configuration settings of the
  /// invoking program.
  /// This function waits the program to finish.
  /// @returns an integer result code indicating the status of the program.
  /// A zero or positive value indicates the result code of the program.
  /// -1 indicates failure to execute
  /// -2 indicates a crash during execution or timeout
  int ExecuteAndWait(
      cStringRef Program, ///< Path of the program to be executed. It is
      /// presumed this is the result of the FindProgramByName method.
      const char **args, ///< A vector of strings that are passed to the
      ///< program.  The first element should be the name of the program.
      ///< The list *must* be terminated by a null char* entry.
      const char **env = 0, ///< An optional vector of strings to use for
      ///< the program's environment. If not provided, the current program's
      ///< environment will be used.
      const cStringRef **redirects = 0, ///< An optional array of pointers to
      ///< paths. If the array is null, no redirection is done. The array
      ///< should have a size of at least three. The inferior process's
      ///< stdin(0), stdout(1), and stderr(2) will be redirected to the
      ///< corresponding paths.
      ///< When an empty path is passed in, the corresponding file
      ///< descriptor will be disconnected (ie, /dev/null'd) in a portable
      ///< way.
      unsigned secondsToWait = 0, ///< If non-zero, this specifies the amount
      ///< of time to wait for the child process to exit. If the time
      ///< expires, the child is killed and this call returns. If zero,
      ///< this function will wait until the child finishes or forever if
      ///< it doesn't.
      unsigned memoryLimit = 0, ///< If non-zero, this specifies max. amount
      ///< of memory can be allocated by process. If memory usage will be
      ///< higher limit, the child is killed and this call returns. If zero
      ///< - no memory limit.
      std::string *ErrMsg = 0, ///< If non-zero, provides a pointer to a string
      ///< instance in which error messages will be returned. If the string
      ///< is non-empty upon return an error occurred while invoking the
      ///< program.
      bool *ExecutionFailed = 0);

  /// Similar to ExecuteAndWait, but return immediately.
  void ExecuteNoWait(cStringRef Program, const char **args, const char **env = 0,
                     const cStringRef **redirects = 0, unsigned memoryLimit = 0,
                     std::string *ErrMsg = 0);

  // Return true if the given arguments fit within system-specific
  // argument length limits.
  bool argumentsFitWithinSystemLimits(cArrayRef<const char*> Args);
}
}

