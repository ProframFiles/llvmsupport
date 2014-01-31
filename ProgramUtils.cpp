//===-- Program.cpp - Implement OS Program Concept --------------*- C++ -*-===//
//
//           originally from The LLVM Compiler Infrastructure
//
// Was distributed under the University of Illinois Open Source License.
//
//===----------------------------------------------------------------------===//
//
//  This header file implements the operating system Program concept.
//
//===----------------------------------------------------------------------===//

#include "ProgramUtils.hpp"

#include "SystemError.hpp"
using namespace akj;
using namespace sys;

//===----------------------------------------------------------------------===//
//=== WARNING: Implementation here must contain only TRULY operating system
//===          independent code.
//===----------------------------------------------------------------------===//

static bool Execute(void **Data, cStringRef Program, const char **args,
                    const char **env, const cStringRef **Redirects,
                    unsigned memoryLimit, std::string *ErrMsg);

static int Wait(void *&Data, cStringRef Program, unsigned secondsToWait,
                std::string *ErrMsg);

int sys::ExecuteAndWait(cStringRef Program, const char **args, const char **envp,
                        const cStringRef **redirects, unsigned secondsToWait,
                        unsigned memoryLimit, std::string *ErrMsg,
                        bool *ExecutionFailed) {
  void *Data = 0;
  if (Execute(&Data, Program, args, envp, redirects, memoryLimit, ErrMsg)) {
    if (ExecutionFailed) *ExecutionFailed = false;
    return Wait(Data, Program, secondsToWait, ErrMsg);
  }
  if (ExecutionFailed) *ExecutionFailed = true;
  return -1;
}

void sys::ExecuteNoWait(cStringRef Program, const char **args, const char **envp,
                        const cStringRef **redirects, unsigned memoryLimit,
                        std::string *ErrMsg) {
  Execute(/*Data*/ 0, Program, args, envp, redirects, memoryLimit, ErrMsg);
}

// Include the platform-specific parts of this class.
#ifdef _WIN32

#include "SystemSpecific/Windows.hpp"

#include "OwningPtr.hpp"
#include "FileSystem.hpp"
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#include <malloc.h>

//===----------------------------------------------------------------------===//
//=== WARNING: Implementation here must contain only Win32 specific code
//===          and must not be UNIX code
//===----------------------------------------------------------------------===//

namespace {
	struct Win32ProcessInfo {
		HANDLE hProcess;
		DWORD  dwProcessId;
	};
}

namespace akj {
	using namespace sys;

	// This function just uses the PATH environment variable to find the program.
	std::string sys::FindProgramByName(const std::string &progName) {
		// Check some degenerate cases
		if (progName.length() == 0) // no program
			return "";
		std::string temp = progName;
		// Return paths with slashes verbatim.
		if (progName.find('\\') != std::string::npos ||
			progName.find('/') != std::string::npos)
			return temp;

		// At this point, the file name is valid and does not contain slashes.
		// Let Windows search for it.
		std::string buffer;
		buffer.resize(MAX_PATH);
		char *dummy = NULL;
		DWORD len = SearchPathA(NULL, progName.c_str(), ".exe", MAX_PATH,
			&buffer[0], &dummy);

		// See if it wasn't found.
		if (len == 0)
			return "";

		// See if we got the entire path.
		if (len < MAX_PATH)
			return buffer;

		// Buffer was too small; grow and retry.
		while (true) {
			buffer.resize(len + 1);
			DWORD len2 = SearchPathA(NULL, progName.c_str(), ".exe", len + 1, &buffer[0], &dummy);

			// It is unlikely the search failed, but it's always possible some file
			// was added or removed since the last search, so be paranoid...
			if (len2 == 0)
				return "";
			else if (len2 <= len)
				return buffer;

			len = len2;
		}
	}

	static HANDLE RedirectIO(const cStringRef *path, int fd, std::string* ErrMsg) {
		HANDLE h;
		if (path == 0) {
			DuplicateHandle(GetCurrentProcess(), (HANDLE)_get_osfhandle(fd),
				GetCurrentProcess(), &h,
				0, TRUE, DUPLICATE_SAME_ACCESS);
			return h;
		}

		std::string fname;
		if (path->empty())
			fname = "NUL";
		else
			fname = *path;

		SECURITY_ATTRIBUTES sa;
		sa.nLength = sizeof(sa);
		sa.lpSecurityDescriptor = 0;
		sa.bInheritHandle = TRUE;

		h = CreateFileA(fname.c_str(), fd ? GENERIC_WRITE : GENERIC_READ,
			FILE_SHARE_READ, &sa, fd == 0 ? OPEN_EXISTING : CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, NULL);
		if (h == INVALID_HANDLE_VALUE) {
			MakeErrMsg(ErrMsg, std::string(fname) + ": Can't open file for " +
				(fd ? "input: " : "output: "));
		}

		return h;
	}

	/// ArgNeedsQuotes - Check whether argument needs to be quoted when calling
	/// CreateProcess.
	static bool ArgNeedsQuotes(const char *Str) {
		return Str[0] == '\0' || strpbrk(Str, "\t \"&\'()*<>\\`^|") != 0;
	}

	/// CountPrecedingBackslashes - Returns the number of backslashes preceding Cur
	/// in the C string Start.
	static unsigned int CountPrecedingBackslashes(const char *Start,
		const char *Cur) {
		unsigned int Count = 0;
		--Cur;
		while (Cur >= Start && *Cur == '\\') {
			++Count;
			--Cur;
		}
		return Count;
	}

	/// EscapePrecedingEscapes - Append a backslash to Dst for every backslash
	/// preceding Cur in the Start string.  Assumes Dst has enough space.
	static char *EscapePrecedingEscapes(char *Dst, const char *Start,
		const char *Cur) {
		unsigned PrecedingEscapes = CountPrecedingBackslashes(Start, Cur);
		while (PrecedingEscapes > 0) {
			*Dst++ = '\\';
			--PrecedingEscapes;
		}
		return Dst;
	}

	/// ArgLenWithQuotes - Check whether argument needs to be quoted when calling
	/// CreateProcess and returns length of quoted arg with escaped quotes
	static unsigned int ArgLenWithQuotes(const char *Str) {
		const char *Start = Str;
		bool Quoted = ArgNeedsQuotes(Str);
		unsigned int len = Quoted ? 2 : 0;

		while (*Str != '\0') {
			if (*Str == '\"') {
				// We need to add a backslash, but ensure that it isn't escaped.
				unsigned PrecedingEscapes = CountPrecedingBackslashes(Start, Str);
				len += PrecedingEscapes + 1;
			}
			// Note that we *don't* need to escape runs of backslashes that don't
			// precede a double quote!  See MSDN:
			// http://msdn.microsoft.com/en-us/library/17w5ykft%28v=vs.85%29.aspx

			++len;
			++Str;
		}

		if (Quoted) {
			// Make sure the closing quote doesn't get escaped by a trailing backslash.
			unsigned PrecedingEscapes = CountPrecedingBackslashes(Start, Str);
			len += PrecedingEscapes + 1;
		}

		return len;
	}

}

static bool Execute(void **Data,
	cStringRef Program,
	const char** args,
	const char** envp,
	const cStringRef** redirects,
	unsigned memoryLimit,
	std::string* ErrMsg) {
	if (!sys::fs::can_execute(Program)) {
		if (ErrMsg)
			*ErrMsg = "program not executable";
		return false;
	}

	// Windows wants a command line, not an array of args, to pass to the new
	// process.  We have to concatenate them all, while quoting the args that
	// have embedded spaces (or are empty).

	// First, determine the length of the command line.
	unsigned len = 0;
	for (unsigned i = 0; args[i]; i++) {
		len += ArgLenWithQuotes(args[i]) + 1;
	}

	// Now build the command line.
	OwningArrayPtr<char> command(new char[len + 1]);
	char *p = command.get();

	for (unsigned i = 0; args[i]; i++) {
		const char *arg = args[i];
		const char *start = arg;

		bool needsQuoting = ArgNeedsQuotes(arg);
		if (needsQuoting)
			*p++ = '"';

		while (*arg != '\0') {
			if (*arg == '\"') {
				// Escape all preceding escapes (if any), and then escape the quote.
				p = EscapePrecedingEscapes(p, start, arg);
				*p++ = '\\';
			}

			*p++ = *arg++;
		}

		if (needsQuoting) {
			// Make sure our quote doesn't get escaped by a trailing backslash.
			p = EscapePrecedingEscapes(p, start, arg);
			*p++ = '"';
		}
		*p++ = ' ';
	}

	*p = 0;

	// The pointer to the environment block for the new process.
	OwningArrayPtr<char> envblock;

	if (envp) {
		// An environment block consists of a null-terminated block of
		// null-terminated strings. Convert the array of environment variables to
		// an environment block by concatenating them.

		// First, determine the length of the environment block.
		len = 0;
		for (unsigned i = 0; envp[i]; i++)
			len += strlen(envp[i]) + 1;

		// Now build the environment block.
		envblock.reset(new char[len + 1]);
		p = envblock.get();

		for (unsigned i = 0; envp[i]; i++) {
			const char *ev = envp[i];
			size_t len = strlen(ev) + 1;
			memcpy(p, ev, len);
			p += len;
		}

		*p = 0;
	}

	// Create a child process.
	STARTUPINFOA si;
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	si.hStdInput = INVALID_HANDLE_VALUE;
	si.hStdOutput = INVALID_HANDLE_VALUE;
	si.hStdError = INVALID_HANDLE_VALUE;

	if (redirects) {
		si.dwFlags = STARTF_USESTDHANDLES;

		si.hStdInput = RedirectIO(redirects[0], 0, ErrMsg);
		if (si.hStdInput == INVALID_HANDLE_VALUE) {
			MakeErrMsg(ErrMsg, "can't redirect stdin");
			return false;
		}
		si.hStdOutput = RedirectIO(redirects[1], 1, ErrMsg);
		if (si.hStdOutput == INVALID_HANDLE_VALUE) {
			CloseHandle(si.hStdInput);
			MakeErrMsg(ErrMsg, "can't redirect stdout");
			return false;
		}
		if (redirects[1] && redirects[2] && *(redirects[1]) == *(redirects[2])) {
			// If stdout and stderr should go to the same place, redirect stderr
			// to the handle already open for stdout.
			DuplicateHandle(GetCurrentProcess(), si.hStdOutput,
				GetCurrentProcess(), &si.hStdError,
				0, TRUE, DUPLICATE_SAME_ACCESS);
		}
		else {
			// Just redirect stderr
			si.hStdError = RedirectIO(redirects[2], 2, ErrMsg);
			if (si.hStdError == INVALID_HANDLE_VALUE) {
				CloseHandle(si.hStdInput);
				CloseHandle(si.hStdOutput);
				MakeErrMsg(ErrMsg, "can't redirect stderr");
				return false;
			}
		}
	}

	PROCESS_INFORMATION pi;
	memset(&pi, 0, sizeof(pi));

	fflush(stdout);
	fflush(stderr);
	std::string ProgramStr = Program;
	BOOL rc = CreateProcessA(ProgramStr.c_str(), command.get(), NULL, NULL, TRUE,
		0, envblock.get(), NULL, &si, &pi);
	DWORD err = GetLastError();

	// Regardless of whether the process got created or not, we are done with
	// the handles we created for it to inherit.
	CloseHandle(si.hStdInput);
	CloseHandle(si.hStdOutput);
	CloseHandle(si.hStdError);

	// Now return an error if the process didn't get created.
	if (!rc) {
		SetLastError(err);
		MakeErrMsg(ErrMsg, std::string("Couldn't execute program '") +
			ProgramStr + "'");
		return false;
	}
	if (Data) {
		Win32ProcessInfo* wpi = new Win32ProcessInfo;
		wpi->hProcess = pi.hProcess;
		wpi->dwProcessId = pi.dwProcessId;
		*Data = wpi;
	}

	// Make sure these get closed no matter what.
	ScopedCommonHandle hThread(pi.hThread);

	// Assign the process to a job if a memory limit is defined.
	ScopedJobHandle hJob;
	if (memoryLimit != 0) {
		hJob = CreateJobObject(0, 0);
		bool success = false;
		if (hJob) {
			JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli;
			memset(&jeli, 0, sizeof(jeli));
			jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_PROCESS_MEMORY;
			jeli.ProcessMemoryLimit = uintptr_t(memoryLimit) * 1048576;
			if (SetInformationJobObject(hJob, JobObjectExtendedLimitInformation,
				&jeli, sizeof(jeli))) {
				if (AssignProcessToJobObject(hJob, pi.hProcess))
					success = true;
			}
		}
		if (!success) {
			SetLastError(GetLastError());
			MakeErrMsg(ErrMsg, std::string("Unable to set memory limit"));
			TerminateProcess(pi.hProcess, 1);
			WaitForSingleObject(pi.hProcess, INFINITE);
			return false;
		}
	}

	// Don't leak the handle if the caller doesn't want it.
	if (!Data)
		CloseHandle(pi.hProcess);

	return true;
}

static int WaitAux(Win32ProcessInfo *wpi, unsigned secondsToWait,
	std::string *ErrMsg) {
	// Wait for the process to terminate.
	HANDLE hProcess = wpi->hProcess;
	DWORD millisecondsToWait = INFINITE;
	if (secondsToWait > 0)
		millisecondsToWait = secondsToWait * 1000;

	if (WaitForSingleObject(hProcess, millisecondsToWait) == WAIT_TIMEOUT) {
		if (!TerminateProcess(hProcess, 1)) {
			MakeErrMsg(ErrMsg, "Failed to terminate timed-out program.");
			// -2 indicates a crash or timeout as opposed to failure to execute.
			return -2;
		}
		WaitForSingleObject(hProcess, INFINITE);
	}

	// Get its exit status.
	DWORD status;
	BOOL rc = GetExitCodeProcess(hProcess, &status);
	DWORD err = GetLastError();

	if (!rc) {
		SetLastError(err);
		MakeErrMsg(ErrMsg, "Failed getting status for program.");
		// -2 indicates a crash or timeout as opposed to failure to execute.
		return -2;
	}

	if (!status)
		return 0;

	// Pass 10(Warning) and 11(Error) to the callee as negative value.
	if ((status & 0xBFFF0000U) == 0x80000000U)
		return (int)status;

	if (status & 0xFF)
		return status & 0x7FFFFFFF;

	return 1;
}

static int Wait(void *&Data, cStringRef Program, unsigned secondsToWait,
	std::string *ErrMsg) {
	Win32ProcessInfo *wpi = reinterpret_cast<Win32ProcessInfo *>(Data);
	int Ret = WaitAux(wpi, secondsToWait, ErrMsg);

	CloseHandle(wpi->hProcess);
	delete wpi;
	Data = 0;

	return Ret;
}

namespace akj {
	error_code sys::ChangeStdinToBinary(){
		int result = _setmode(_fileno(stdin), _O_BINARY);
		if (result == -1)
			return error_code(errno, generic_category());
		return make_error_code(errc::success);
	}

	error_code sys::ChangeStdoutToBinary(){
		int result = _setmode(_fileno(stdout), _O_BINARY);
		if (result == -1)
			return error_code(errno, generic_category());
		return make_error_code(errc::success);
	}

	error_code sys::ChangeStderrToBinary(){
		int result = _setmode(_fileno(stderr), _O_BINARY);
		if (result == -1)
			return error_code(errno, generic_category());
		return make_error_code(errc::success);
	}

	bool akj::sys::argumentsFitWithinSystemLimits(cArrayRef<const char*> Args) {
		// The documented max length of the command line passed to CreateProcess.
		static const size_t MaxCommandStringLength = 32768;
		size_t ArgLength = 0;
		for (cArrayRef<const char*>::iterator I = Args.begin(), E = Args.end();
			I != E; ++I) {
			// Account for the trailing space for every arg but the last one and the
			// trailing NULL of the last argument.
			ArgLength += ArgLenWithQuotes(*I) + 1;
			if (ArgLength > MaxCommandStringLength) {
				return false;
			}
		}
		return true;
	}

}



#else 

#include "SystemSpecific/Unix.hpp"

#include "CompilerFeatures.hpp"
#include "FileSystem.hpp"

#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#if HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif
#if HAVE_SIGNAL_H
#include <signal.h>
#endif
#if HAVE_FCNTL_H
#include <fcntl.h>
#endif
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_POSIX_SPAWN
#include <spawn.h>
#if !defined(__APPLE__)
extern char **environ;
#else
#include <crt_externs.h> // _NSGetEnviron
#endif
#endif

namespace akj {
	using namespace sys;

	// This function just uses the PATH environment variable to find the program.
	std::string
		sys::FindProgramByName(const std::string& progName) {

			// Check some degenerate cases
			if (progName.length() == 0) // no program
				return "";
			std::string temp = progName;
			// Use the given path verbatim if it contains any slashes; this matches
			// the behavior of sh(1) and friends.
			if (progName.find('/') != std::string::npos)
				return temp;

			// At this point, the file name is valid and does not contain slashes. Search
			// for it through the directories specified in the PATH environment variable.

			// Get the path. If its empty, we can't do anything to find it.
			const char *PathStr = getenv("PATH");
			if (PathStr == 0)
				return "";

			// Now we have a colon separated list of directories to search; try them.
			size_t PathLen = strlen(PathStr);
			while (PathLen) {
				// Find the first colon...
				const char *Colon = std::find(PathStr, PathStr + PathLen, ':');

				// Check to see if this first directory contains the executable...
				SmallString<128> FilePath(PathStr, Colon);
				sys::path::append(FilePath, progName);
				if (sys::fs::can_execute(Twine(FilePath)))
					return FilePath.str();                    // Found the executable!

				// Nope it wasn't in this directory, check the next path in the list!
				PathLen -= Colon - PathStr;
				PathStr = Colon;

				// Advance past duplicate colons
				while (*PathStr == ':') {
					PathStr++;
					PathLen--;
				}
			}
			return "";
		}

	static bool RedirectIO(const cStringRef *Path, int FD, std::string* ErrMsg) {
		if (Path == 0) // Noop
			return false;
		std::string File;
		if (Path->empty())
			// Redirect empty paths to /dev/null
			File = "/dev/null";
		else
			File = *Path;

		// Open the file
		int InFD = open(File.c_str(), FD == 0 ? O_RDONLY : O_WRONLY | O_CREAT, 0666);
		if (InFD == -1) {
			MakeErrMsg(ErrMsg, "Cannot open file '" + File + "' for "
				+ (FD == 0 ? "input" : "output"));
			return true;
		}

		// Install it as the requested FD
		if (dup2(InFD, FD) == -1) {
			MakeErrMsg(ErrMsg, "Cannot dup2");
			close(InFD);
			return true;
		}
		close(InFD);      // Close the original FD
		return false;
	}

#ifdef HAVE_POSIX_SPAWN
	static bool RedirectIO_PS(const std::string *Path, int FD, std::string *ErrMsg,
		posix_spawn_file_actions_t *FileActions) {
		if (Path == 0) // Noop
			return false;
		const char *File;
		if (Path->empty())
			// Redirect empty paths to /dev/null
			File = "/dev/null";
		else
			File = Path->c_str();

		if (int Err = posix_spawn_file_actions_addopen(
			FileActions, FD, File,
			FD == 0 ? O_RDONLY : O_WRONLY | O_CREAT, 0666))
			return MakeErrMsg(ErrMsg, "Cannot dup2", Err);
		return false;
	}
#endif

	static void TimeOutHandler(int Sig) {
	}

	static void SetMemoryLimits(unsigned size)
	{
#if HAVE_SYS_RESOURCE_H && HAVE_GETRLIMIT && HAVE_SETRLIMIT
		struct rlimit r;
		__typeof__(r.rlim_cur) limit = (__typeof__(r.rlim_cur)) (size)* 1048576;

		// Heap size
		getrlimit(RLIMIT_DATA, &r);
		r.rlim_cur = limit;
		setrlimit(RLIMIT_DATA, &r);
#ifdef RLIMIT_RSS
		// Resident set size.
		getrlimit(RLIMIT_RSS, &r);
		r.rlim_cur = limit;
		setrlimit(RLIMIT_RSS, &r);
#endif
#ifdef RLIMIT_AS  // e.g. NetBSD doesn't have it.
		// Don't set virtual memory limit if built with any Sanitizer. They need 80Tb
		// of virtual memory for shadow memory mapping.
#if !AKJ_MEMORY_SANITIZER_BUILD && !AKJ_ADDRESS_SANITIZER_BUILD
		// Virtual memory.
		getrlimit(RLIMIT_AS, &r);
		r.rlim_cur = limit;
		setrlimit(RLIMIT_AS, &r);
#endif
#endif
#endif
	}

}

static bool Execute(void **Data, cStringRef Program, const char **args,
	const char **envp, const cStringRef **redirects,
	unsigned memoryLimit, std::string *ErrMsg) {
	// If this OS has posix_spawn and there is no memory limit being implied, use
	// posix_spawn.  It is more efficient than fork/exec.
#ifdef HAVE_POSIX_SPAWN
	if (memoryLimit == 0) {
		posix_spawn_file_actions_t FileActionsStore;
		posix_spawn_file_actions_t *FileActions = 0;

		// If we call posix_spawn_file_actions_addopen we have to make sure the
		// c strings we pass to it stay alive until the call to posix_spawn,
		// so we copy any cStringRefs into this variable.
		std::string RedirectsStorage[3];

		if (redirects) {
			std::string *RedirectsStr[3] = { 0, 0, 0 };
			for (int I = 0; I < 3; ++I) {
				if (redirects[I]) {
					RedirectsStorage[I] = *redirects[I];
					RedirectsStr[I] = &RedirectsStorage[I];
				}
			}

			FileActions = &FileActionsStore;
			posix_spawn_file_actions_init(FileActions);

			// Redirect stdin/stdout.
			if (RedirectIO_PS(RedirectsStr[0], 0, ErrMsg, FileActions) ||
				RedirectIO_PS(RedirectsStr[1], 1, ErrMsg, FileActions))
				return false;
			if (redirects[1] == 0 || redirects[2] == 0 ||
				*redirects[1] != *redirects[2]) {
				// Just redirect stderr
				if (RedirectIO_PS(RedirectsStr[2], 2, ErrMsg, FileActions))
					return false;
			}
			else {
				// If stdout and stderr should go to the same place, redirect stderr
				// to the FD already open for stdout.
				if (int Err = posix_spawn_file_actions_adddup2(FileActions, 1, 2))
					return !MakeErrMsg(ErrMsg, "Can't redirect stderr to stdout", Err);
			}
		}

		if (!envp)
#if !defined(__APPLE__)
			envp = const_cast<const char **>(environ);
#else
			// environ is missing in dylibs.
			envp = const_cast<const char **>(*_NSGetEnviron());
#endif

		// Explicitly initialized to prevent what appears to be a valgrind false
		// positive.
		pid_t PID = 0;
		int Err = posix_spawn(&PID, Program.str().c_str(), FileActions, /*attrp*/0,
			const_cast<char **>(args), const_cast<char **>(envp));

		if (FileActions)
			posix_spawn_file_actions_destroy(FileActions);

		if (Err)
			return !MakeErrMsg(ErrMsg, "posix_spawn failed", Err);

		if (Data)
			*Data = reinterpret_cast<void*>(PID);
		return true;
	}
#endif

	// Create a child process.
	int child = fork();
	switch (child) {
		// An error occurred:  Return to the caller.
	case -1:
		MakeErrMsg(ErrMsg, "Couldn't fork");
		return false;

		// Child process: Execute the program.
	case 0: {
						// Redirect file descriptors...
						if (redirects) {
							// Redirect stdin
							if (RedirectIO(redirects[0], 0, ErrMsg)) { return false; }
							// Redirect stdout
							if (RedirectIO(redirects[1], 1, ErrMsg)) { return false; }
							if (redirects[1] && redirects[2] &&
								*(redirects[1]) == *(redirects[2])) {
								// If stdout and stderr should go to the same place, redirect stderr
								// to the FD already open for stdout.
								if (-1 == dup2(1, 2)) {
									MakeErrMsg(ErrMsg, "Can't redirect stderr to stdout");
									return false;
								}
							}
							else {
								// Just redirect stderr
								if (RedirectIO(redirects[2], 2, ErrMsg)) { return false; }
							}
						}

						// Set memory limits
						if (memoryLimit != 0) {
							SetMemoryLimits(memoryLimit);
						}

						// Execute!
						std::string PathStr = Program;
						if (envp != 0)
							execve(PathStr.c_str(),
							const_cast<char **>(args),
							const_cast<char **>(envp));
						else
							execv(PathStr.c_str(),
							const_cast<char **>(args));
						// If the execve() failed, we should exit. Follow Unix protocol and
						// return 127 if the executable was not found, and 126 otherwise.
						// Use _exit rather than exit so that atexit functions and static
						// object destructors cloned from the parent process aren't
						// redundantly run, and so that any data buffered in stdio buffers
						// cloned from the parent aren't redundantly written out.
						_exit(errno == ENOENT ? 127 : 126);
	}

		// Parent process: Break out of the switch to do our processing.
	default:
		break;
	}

	if (Data)
		*Data = reinterpret_cast<void*>(child);

	return true;
}

static int Wait(void *&Data, cStringRef Program, unsigned secondsToWait,
	std::string *ErrMsg) {
#ifdef HAVE_SYS_WAIT_H
	struct sigaction Act, Old;
	assert(Data && "invalid pid to wait on, process not started?");

	// Install a timeout handler.  The handler itself does nothing, but the simple
	// fact of having a handler at all causes the wait below to return with EINTR,
	// unlike if we used SIG_IGN.
	if (secondsToWait) {
		memset(&Act, 0, sizeof(Act));
		Act.sa_handler = TimeOutHandler;
		sigemptyset(&Act.sa_mask);
		sigaction(SIGALRM, &Act, &Old);
		alarm(secondsToWait);
	}

	// Parent process: Wait for the child process to terminate.
	int status;
	uint64_t pid = reinterpret_cast<uint64_t>(Data);
	pid_t child = static_cast<pid_t>(pid);
	while (waitpid(pid, &status, 0) != child)
	if (secondsToWait && errno == EINTR) {
		// Kill the child.
		kill(child, SIGKILL);

		// Turn off the alarm and restore the signal handler
		alarm(0);
		sigaction(SIGALRM, &Old, 0);

		// Wait for child to die
		if (wait(&status) != child)
			MakeErrMsg(ErrMsg, "Child timed out but wouldn't die");
		else
			MakeErrMsg(ErrMsg, "Child timed out", 0);

		return -2;   // Timeout detected
	}
	else if (errno != EINTR) {
		MakeErrMsg(ErrMsg, "Error waiting for child process");
		return -1;
	}

	// We exited normally without timeout, so turn off the timer.
	if (secondsToWait) {
		alarm(0);
		sigaction(SIGALRM, &Old, 0);
	}

	// Return the proper exit status. Detect error conditions
	// so we can return -1 for them and set ErrMsg informatively.
	int result = 0;
	if (WIFEXITED(status)) {
		result = WEXITSTATUS(status);
#ifdef HAVE_POSIX_SPAWN
		// The posix_spawn child process returns 127 on any kind of error.
		// Following the POSIX convention for command-line tools (which posix_spawn
		// itself apparently does not), check to see if the failure was due to some
		// reason other than the file not existing, and return 126 in this case.
		bool Exists;
		if (result == 127 && !akj::sys::fs::exists(Program, Exists) && Exists)
			result = 126;
#endif
		if (result == 127) {
			if (ErrMsg)
				*ErrMsg = akj::sys::StrError(ENOENT);
			return -1;
		}
		if (result == 126) {
			if (ErrMsg)
				*ErrMsg = "Program could not be executed";
			return -1;
		}
	}
	else if (WIFSIGNALED(status)) {
		if (ErrMsg) {
			*ErrMsg = strsignal(WTERMSIG(status));
#ifdef WCOREDUMP
			if (WCOREDUMP(status))
				*ErrMsg += " (core dumped)";
#endif
		}
		// Return a special value to indicate that the process received an unhandled
		// signal during execution as opposed to failing to execute.
		return -2;
	}
	return result;
#else
	if (ErrMsg)
		*ErrMsg = "Program::Wait is not implemented on this platform yet!";
	return -1;
#endif
}

namespace akj {

	error_code sys::ChangeStdinToBinary(){
		// Do nothing, as Unix doesn't differentiate between text and binary.
		return make_error_code(errc::success);
	}

	error_code sys::ChangeStdoutToBinary(){
		// Do nothing, as Unix doesn't differentiate between text and binary.
		return make_error_code(errc::success);
	}

	error_code sys::ChangeStderrToBinary(){
		// Do nothing, as Unix doesn't differentiate between text and binary.
		return make_error_code(errc::success);
	}

	bool akj::sys::argumentsFitWithinSystemLimits(cArrayRef<const char*> Args) {
		static long ArgMax = sysconf(_SC_ARG_MAX);

		// System says no practical limit.
		if (ArgMax == -1)
			return true;

		// Conservatively account for space required by environment variables.
		ArgMax /= 2;

		size_t ArgLength = 0;
		for (cArrayRef<const char*>::iterator I = Args.begin(), E = Args.end();
			I != E; ++I) {
			ArgLength += strlen(*I) + 1;
			if (ArgLength > size_t(ArgMax)) {
				return false;
			}
		}
		return true;
	}

}


#endif

