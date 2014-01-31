//===-- akjProcessUtils.cpp - Implement OS Process Concept ------*- C++ -*-===//
//
//           originally from The LLVM Compiler Infrastructure
//
// Was distributed under the University of Illinois Open Source License.
//
//===----------------------------------------------------------------------===//
//
//  This header file implements the operating system Process concept.
//
//===----------------------------------------------------------------------===//

#include "FatalError.hpp"
#include "ProcessUtils.hpp"

using namespace akj;
using namespace sys;

//===----------------------------------------------------------------------===//
//=== WARNING: Implementation here must contain only TRULY operating system
//===          independent code.
//===----------------------------------------------------------------------===//

// Empty virtual destructor to anchor the vtable for the process class.
process::~process() {}

self_process *process::get_self() {
  // Use a function local static for thread safe initialization and allocate it
  // as a raw pointer to ensure it is never destroyed.
  static self_process *SP = new self_process();

  return SP;
}

#if defined(_MSC_VER)
// Visual Studio complains that the self_process destructor never exits. This
// doesn't make much sense, as that's the whole point of calling abort... Just
// silence this warning.
#pragma warning(push)
#pragma warning(disable:4722)
#endif

// The destructor for the self_process subclass must never actually be
// executed. There should be at most one instance of this class, and that
// instance should live until the process terminates to avoid the potential for
// racy accesses during shutdown.
self_process::~self_process() {
	assert(false);
}

/// \brief A helper function to compute the elapsed wall-time since the program
/// started.
///
/// Note that this routine actually computes the elapsed wall time since the
/// first time it was called. However, we arrange to have it called during the
/// startup of the process to get approximately correct results.
static TimeValue getElapsedWallTime() {
  static TimeValue &StartTime = *new TimeValue(TimeValue::now());
  return TimeValue::now() - StartTime;
}

/// \brief A special global variable to ensure we call \c getElapsedWallTime
/// during global initialization of the program.
///
/// Note that this variable is never referenced elsewhere. Doing so could
/// create race conditions during program startup or shutdown.
static volatile TimeValue DummyTimeValue = getElapsedWallTime();

// Implement this routine by using the static helpers above. They're already
// portable.
TimeValue self_process::get_wall_time() const {
  return getElapsedWallTime();
}


#if defined(_MSC_VER)
#pragma warning(pop)
#endif


#define COLOR(FGBG, CODE, BOLD) "\033[0;" BOLD FGBG CODE "m"

#define ALLCOLORS(FGBG,BOLD) {\
    COLOR(FGBG, "0", BOLD),\
    COLOR(FGBG, "1", BOLD),\
    COLOR(FGBG, "2", BOLD),\
    COLOR(FGBG, "3", BOLD),\
    COLOR(FGBG, "4", BOLD),\
    COLOR(FGBG, "5", BOLD),\
    COLOR(FGBG, "6", BOLD),\
    COLOR(FGBG, "7", BOLD)\
  }

static const char colorcodes[2][2][8][10] = {
 { ALLCOLORS("3",""), ALLCOLORS("3","1;") },
 { ALLCOLORS("4",""), ALLCOLORS("4","1;") }
};

// Include the platform-specific parts of this class.
#ifdef _WIN32

#include "SystemSpecific/Windows.hpp"

#include "SmallVector.hpp"
#include "SystemError.hpp"
#include <direct.h>
#include <io.h>
#include <malloc.h>
#include <psapi.h>

#ifdef __MINGW32__
#if (HAVE_LIBPSAPI != 1)
#error "libpsapi.a should be present"
#endif
#else
#pragma comment(lib, "psapi.lib")
#endif

//===----------------------------------------------------------------------===//
//=== WARNING: Implementation here must contain only Win32 specific code
//===          and must not be UNIX code
//===----------------------------------------------------------------------===//

#ifdef __MINGW32__
// This ban should be lifted when MinGW 1.0+ has defined this value.
#  define _HEAPOK (-2)
#endif

namespace akj{
	namespace sys{


		process::id_type self_process::get_id() {
			return GetCurrentProcessId();
		}

		static TimeValue getTimeValueFromFILETIME(FILETIME Time) {
			ULARGE_INTEGER TimeInteger;
			TimeInteger.LowPart = Time.dwLowDateTime;
			TimeInteger.HighPart = Time.dwHighDateTime;

			// FILETIME's are # of 100 nanosecond ticks (1/10th of a microsecond)
			return TimeValue(
				static_cast<TimeValue::SecondsType>(TimeInteger.QuadPart / 10000000),
				static_cast<TimeValue::NanoSecondsType>(
				(TimeInteger.QuadPart % 10000000) * 100));
		}

		TimeValue self_process::get_user_time() const {
			FILETIME ProcCreate, ProcExit, KernelTime, UserTime;
			if (GetProcessTimes(GetCurrentProcess(), &ProcCreate, &ProcExit, &KernelTime,
				&UserTime) == 0)
				return TimeValue();

			return getTimeValueFromFILETIME(UserTime);
		}

		TimeValue self_process::get_system_time() const {
			FILETIME ProcCreate, ProcExit, KernelTime, UserTime;
			if (GetProcessTimes(GetCurrentProcess(), &ProcCreate, &ProcExit, &KernelTime,
				&UserTime) == 0)
				return TimeValue();

			return getTimeValueFromFILETIME(KernelTime);
		}

		// This function retrieves the page size using GetSystemInfo and is present
		// solely so it can be called once to initialize the self_process member below.
		static unsigned getPageSize() {
			// NOTE: A 32-bit application running under WOW64 is supposed to use
			// GetNativeSystemInfo.  However, this interface is not present prior
			// to Windows XP so to use it requires dynamic linking.  It is not clear
			// how this affects the reported page size, if at all.  One could argue
			// that LLVM ought to run as 64-bits on a 64-bit system, anyway.
			SYSTEM_INFO info;
			GetSystemInfo(&info);
			// FIXME: FileOffset in MapViewOfFile() should be aligned to not dwPageSize,
			// but dwAllocationGranularity.
			return static_cast<unsigned>(info.dwPageSize);
		}

		// This constructor guaranteed to be run exactly once on a single thread, and
		// sets up various process invariants that can be queried cheaply from then on.
		self_process::self_process() : PageSize(getPageSize()) {
		}


		size_t
			Process::GetMallocUsage()
		{
				_HEAPINFO hinfo;
				hinfo._pentry = NULL;

				size_t size = 0;

				while (_heapwalk(&hinfo) == _HEAPOK)
					size += hinfo._size;

				return size;
			}

		void Process::GetTimeUsage(TimeValue &elapsed, TimeValue &user_time,
			TimeValue &sys_time) {
			elapsed = TimeValue::now();

			FILETIME ProcCreate, ProcExit, KernelTime, UserTime;
			if (GetProcessTimes(GetCurrentProcess(), &ProcCreate, &ProcExit, &KernelTime,
				&UserTime) == 0)
				return;

			user_time = getTimeValueFromFILETIME(UserTime);
			sys_time = getTimeValueFromFILETIME(KernelTime);
		}

		// Some LLVM programs such as bugpoint produce core files as a normal part of
		// their operation. To prevent the disk from filling up, this configuration
		// item does what's necessary to prevent their generation.
		void Process::PreventCoreFiles() {
			// Windows does have the concept of core files, called minidumps.  However,
			// disabling minidumps for a particular application extends past the lifetime
			// of that application, which is the incorrect behavior for this API.
			// Additionally, the APIs require elevated privileges to disable and re-
			// enable minidumps, which makes this untenable. For more information, see
			// WerAddExcludedApplication and WerRemoveExcludedApplication (Vista and
			// later).
			//
			// Windows also has modal pop-up message boxes.  As this method is used by
			// bugpoint, preventing these pop-ups is additionally important.
			SetErrorMode(SEM_FAILCRITICALERRORS |
				SEM_NOGPFAULTERRORBOX |
				SEM_NOOPENFILEERRORBOX);
		}

		/// Returns the environment variable \arg Name's value as a string encoded in
		/// UTF-8. \arg Name is assumed to be in UTF-8 encoding.h
		Optional<std::string> Process::GetEnv(cStringRef Name) {
			// Convert the argument to UTF-16 to pass it to _wgetenv().
			cSmallVector<wchar_t, 128> NameUTF16;
			if (error_code ec = windows::UTF8ToUTF16(Name, NameUTF16))
				return None;

			// Environment variable can be encoded in non-UTF8 encoding, and there's no
			// way to know what the encoding is. The only reliable way to look up
			// multibyte environment variable is to use GetEnvironmentVariableW().
			std::vector<wchar_t> Buf(16);
			size_t Size = 0;
			for (;;) {
				Size = GetEnvironmentVariableW(&NameUTF16[0], &Buf[0], Buf.size());
				if (Size < Buf.size())
					break;
				// Try again with larger buffer.
				Buf.resize(Size + 1);
			}
			if (Size == 0)
				return None;

			// Convert the result from UTF-16 to UTF-8.
			cSmallVector<char, 128> Res;
			if (error_code ec = windows::UTF16ToUTF8(&Buf[0], Size, Res))
				return None;
			return std::string(&Res[0]);
		}

		bool Process::StandardInIsUserInput() {
			return FileDescriptorIsDisplayed(0);
		}

		bool Process::StandardOutIsDisplayed() {
			return FileDescriptorIsDisplayed(1);
		}

		bool Process::StandardErrIsDisplayed() {
			return FileDescriptorIsDisplayed(2);
		}

		bool Process::FileDescriptorIsDisplayed(int fd) {
			DWORD Mode;  // Unused
			return (GetConsoleMode((HANDLE)_get_osfhandle(fd), &Mode) != 0);
		}

		unsigned Process::StandardOutColumns() {
			unsigned Columns = 0;
			CONSOLE_SCREEN_BUFFER_INFO csbi;
			if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
				Columns = csbi.dwSize.X;
			return Columns;
		}

		unsigned Process::StandardErrColumns() {
			unsigned Columns = 0;
			CONSOLE_SCREEN_BUFFER_INFO csbi;
			if (GetConsoleScreenBufferInfo(GetStdHandle(STD_ERROR_HANDLE), &csbi))
				Columns = csbi.dwSize.X;
			return Columns;
		}

		// The terminal always has colors.
		bool Process::FileDescriptorHasColors(int fd) {
			return FileDescriptorIsDisplayed(fd);
		}

		bool Process::StandardOutHasColors() {
			return FileDescriptorHasColors(1);
		}

		bool Process::StandardErrHasColors() {
			return FileDescriptorHasColors(2);
		}

		static bool UseANSI = false;
		void Process::UseANSIEscapeCodes(bool enable) {
			UseANSI = enable;
		}

		namespace {
			class DefaultColors
			{
			private:
				WORD defaultColor;
			public:
				DefaultColors()
					:defaultColor(GetCurrentColor()) {}
				static unsigned GetCurrentColor() {
					CONSOLE_SCREEN_BUFFER_INFO csbi;
					if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
						return csbi.wAttributes;
					return 0;
				}
				WORD operator()() const { return defaultColor; }
			};

			DefaultColors defaultColors;
		}

		bool Process::ColorNeedsFlush() {
			return !UseANSI;
		}

		const char *Process::OutputBold(bool bg) {
			if (UseANSI) return "\033[1m";

			WORD colors = DefaultColors::GetCurrentColor();
			if (bg)
				colors |= BACKGROUND_INTENSITY;
			else
				colors |= FOREGROUND_INTENSITY;
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), colors);
			return 0;
		}

		const char *Process::OutputColor(char code, bool bold, bool bg) {
			if (UseANSI) return colorcodes[bg ? 1 : 0][bold ? 1 : 0][code & 7];

			WORD colors;
			if (bg) {
				colors = ((code & 1) ? BACKGROUND_RED : 0) |
					((code & 2) ? BACKGROUND_GREEN : 0) |
					((code & 4) ? BACKGROUND_BLUE : 0);
				if (bold)
					colors |= BACKGROUND_INTENSITY;
			}
			else {
				colors = ((code & 1) ? FOREGROUND_RED : 0) |
					((code & 2) ? FOREGROUND_GREEN : 0) |
					((code & 4) ? FOREGROUND_BLUE : 0);
				if (bold)
					colors |= FOREGROUND_INTENSITY;
			}
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), colors);
			return 0;
		}

		static WORD GetConsoleTextAttribute(HANDLE hConsoleOutput) {
			CONSOLE_SCREEN_BUFFER_INFO info;
			GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info);
			return info.wAttributes;
		}

		const char *Process::OutputReverse() {
			if (UseANSI) return "\033[7m";

			const WORD attributes
				= GetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE));

			const WORD foreground_mask = FOREGROUND_BLUE | FOREGROUND_GREEN |
				FOREGROUND_RED | FOREGROUND_INTENSITY;
			const WORD background_mask = BACKGROUND_BLUE | BACKGROUND_GREEN |
				BACKGROUND_RED | BACKGROUND_INTENSITY;
			const WORD color_mask = foreground_mask | background_mask;

			WORD new_attributes =
				((attributes & FOREGROUND_BLUE) ? BACKGROUND_BLUE : 0) |
				((attributes & FOREGROUND_GREEN) ? BACKGROUND_GREEN : 0) |
				((attributes & FOREGROUND_RED) ? BACKGROUND_RED : 0) |
				((attributes & FOREGROUND_INTENSITY) ? BACKGROUND_INTENSITY : 0) |
				((attributes & BACKGROUND_BLUE) ? FOREGROUND_BLUE : 0) |
				((attributes & BACKGROUND_GREEN) ? FOREGROUND_GREEN : 0) |
				((attributes & BACKGROUND_RED) ? FOREGROUND_RED : 0) |
				((attributes & BACKGROUND_INTENSITY) ? FOREGROUND_INTENSITY : 0) |
				0;
			new_attributes = (attributes & ~color_mask) | (new_attributes & color_mask);

			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), new_attributes);
			return 0;
		}

		const char *Process::ResetColor() {
			if (UseANSI) return "\033[0m";
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), defaultColors());
			return 0;
		}
	} // end namespace sys
} // end namespace akj


#else

#ifndef AKJ_DEFINED_UNIX_FS
#define AKJ_DEFINED_UNIX_FS

#include "SystemSpecific/Unix.hpp"

#include "ProcessUtils.hpp"
#include <limits.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
//#if HAVE_DIRENT_H
# include <dirent.h>
# define NAMLEN(dirent) strlen((dirent)->d_name)
#if AKJ_IS_THIS_NEEDED
# define dirent direct
# define NAMLEN(dirent) (dirent)->d_namlen
# if HAVE_SYS_NDIR_H
#  include <sys/ndir.h>
# endif
# if HAVE_SYS_DIR_H
#  include <sys/dir.h>
# endif
# if HAVE_NDIR_H
#  include <ndir.h>
# endif
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

// Both stdio.h and cstdio are included via different pathes and
// stdcxx's cstdio doesn't include stdio.h, so it doesn't #undef the macros
// either.
#undef ferror
#undef feof

// For GNU Hurd
#if defined(__GNU__) && !defined(PATH_MAX)
# define PATH_MAX 4096
#endif

#define MAXPATHLEN 160


using namespace akj;

namespace {
	/// This class automatically closes the given file descriptor when it goes out
	/// of scope. You can take back explicit ownership of the file descriptor by
	/// calling take(). The destructor does not verify that close was successful.
	/// Therefore, never allow this class to call close on a file descriptor that
	/// has been read from or written to.
	struct AutoFD {
		int FileDescriptor;

		AutoFD(int fd) : FileDescriptor(fd) {}
		~AutoFD() {
			if (FileDescriptor >= 0)
				::close(FileDescriptor);
		}

		int take() {
			int ret = FileDescriptor;
			FileDescriptor = -1;
			return ret;
		}

		operator int() const { return FileDescriptor; }
	};

	error_code TempDir(cSmallVectorImpl<char> &result) {
		// FIXME: Don't use TMPDIR if program is SUID or SGID enabled.
		const char *dir = 0;
		(dir = std::getenv("TMPDIR")) ||
			(dir = std::getenv("TMP")) ||
			(dir = std::getenv("TEMP")) ||
			(dir = std::getenv("TEMPDIR")) ||
#ifdef P_tmpdir
			(dir = P_tmpdir) ||
#endif
			(dir = "/tmp");

		result.clear();
		cStringRef d(dir);
		result.append(d.begin(), d.end());
		return error_code::success();
	}
}

static error_code createUniqueEntity(const Twine &Model, int &ResultFD,
	cSmallVectorImpl<char> &ResultPath,
	bool MakeAbsolute, unsigned Mode,
	FSEntity Type) {
	SmallString<128> ModelStorage;
	Model.toVector(ModelStorage);

	if (MakeAbsolute) {
		// Make model absolute by prepending a temp directory if it's not already.
		bool absolute = sys::path::is_absolute(Twine(ModelStorage));
		if (!absolute) {
			SmallString<128> TDir;
			if (error_code ec = TempDir(TDir)) return ec;
			sys::path::append(TDir, Twine(ModelStorage));
			ModelStorage.swap(TDir);
		}
	}

	// From here on, DO NOT modify model. It may be needed if the randomly chosen
	// path already exists.
	ResultPath = ModelStorage;
	// Null terminate.
	ResultPath.push_back(0);
	ResultPath.pop_back();

retry_random_path:
	// Replace '%' with random chars.
	for (unsigned i = 0, e = ModelStorage.size(); i != e; ++i) {
		if (ModelStorage[i] == '%')
			ResultPath[i] = "0123456789abcdef"[sys::Process::GetRandomNumber() & 15];
	}

	// Try to open + create the file.
	switch (Type) {
	case FS_File: {
									int RandomFD = ::open(ResultPath.begin(), O_RDWR | O_CREAT | O_EXCL, Mode);
									if (RandomFD == -1) {
										int SavedErrno = errno;
										// If the file existed, try again, otherwise, error.
										if (SavedErrno == errc::file_exists)
											goto retry_random_path;
										return error_code(SavedErrno, system_category());
									}

									ResultFD = RandomFD;
									return error_code::success();
	}

	case FS_Name: {
									bool Exists;
									error_code EC = sys::fs::exists(ResultPath.begin(), Exists);
									if (EC)
										return EC;
									if (Exists)
										goto retry_random_path;
									return error_code::success();
	}

	case FS_Dir: {
								 bool Existed;
								 error_code EC = sys::fs::create_directory(ResultPath.begin(), Existed);
								 if (EC)
									 return EC;
								 if (Existed)
									 goto retry_random_path;
								 return error_code::success();
	}
	}
	FatalError::Die("Invalid Type");
}

namespace akj {
	namespace sys  {
		namespace fs {
#if defined(__FreeBSD__) || defined (__NetBSD__) || defined(__Bitrig__) || \
	defined(__OpenBSD__) || defined(__minix) || defined(__FreeBSD_kernel__) || \
	defined(__linux__) || defined(__CYGWIN__)
			static int
				test_dir(char buf[PATH_MAX], char ret[PATH_MAX],
				const char *dir, const char *bin)
			{
					struct stat sb;

					snprintf(buf, PATH_MAX, "%s/%s", dir, bin);
					if (realpath(buf, ret) == NULL)
						return (1);
					if (stat(buf, &sb) != 0)
						return (1);

					return (0);
				}

			static char *
				getprogpath(char ret[PATH_MAX], const char *bin)
			{
					char *pv, *s, *t, buf[PATH_MAX];

					/* First approach: absolute path. */
					if (bin[0] == '/') {
						if (test_dir(buf, ret, "/", bin) == 0)
							return (ret);
						return (NULL);
					}

					/* Second approach: relative path. */
					if (strchr(bin, '/') != NULL) {
						if (getcwd(buf, PATH_MAX) == NULL)
							return (NULL);
						if (test_dir(buf, ret, buf, bin) == 0)
							return (ret);
						return (NULL);
					}

					/* Third approach: $PATH */
					if ((pv = getenv("PATH")) == NULL)
						return (NULL);
					s = pv = strdup(pv);
					if (pv == NULL)
						return (NULL);
					while ((t = strsep(&s, ":")) != NULL) {
						if (test_dir(buf, ret, t, bin) == 0) {
							free(pv);
							return (ret);
						}
					}
					free(pv);
					return (NULL);
				}
#endif // __FreeBSD__ || __NetBSD__ || __FreeBSD_kernel__

			/// GetMainExecutable - Return the path to the main executable, given the
			/// value of argv[0] from program startup.
			std::string getMainExecutable(const char *argv0, void *MainAddr) {
#if defined(__APPLE__)
				// On OS X the executable path is saved to the stack by dyld. Reading it
				// from there is much faster than calling dladdr, especially for large
				// binaries with symbols.
				char exe_path[MAXPATHLEN];
				uint32_t size = sizeof(exe_path);
				if (_NSGetExecutablePath(exe_path, &size) == 0) {
					char link_path[MAXPATHLEN];
					if (realpath(exe_path, link_path))
						return link_path;
				}
#elif defined(__FreeBSD__) || defined (__NetBSD__) || defined(__Bitrig__) || \
	defined(__OpenBSD__) || defined(__minix) || defined(__FreeBSD_kernel__)
				char exe_path[PATH_MAX];

				if (getprogpath(exe_path, argv0) != NULL)
					return exe_path;
#elif defined(__linux__) || defined(__CYGWIN__)
				char exe_path[MAXPATHLEN];
				cStringRef aPath("/proc/self/exe");
				if (sys::fs::exists(aPath)) {
					// /proc is not always mounted under Linux (chroot for example).
					size_t len = readlink(aPath.str().c_str(), exe_path, sizeof(exe_path));
					if (len >= 0)
						return cStringRef(exe_path, len);
				}
				else {
					// Fall back to the classical detection.
					if (getprogpath(exe_path, argv0) != NULL)
						return exe_path;
				}
#elif defined(HAVE_DLFCN_H)
				// Use dladdr to get executable path if available.
				Dl_info DLInfo;
				int err = dladdr(MainAddr, &DLInfo);
				if (err == 0)
					return "";

				// If the filename is a symlink, we need to resolve and return the location of
				// the actual executable.
				char link_path[MAXPATHLEN];
				if (realpath(DLInfo.dli_fname, link_path))
					return link_path;
#else
#error GetMainExecutable is not implemented on this host yet.
#endif
				return "";
			}

			TimeValue file_status::getLastModificationTime() const {
				TimeValue Ret;
				Ret.fromEpochTime(fs_st_mtime);
				return Ret;
			}

			UniqueID file_status::getUniqueID() const {
				return UniqueID(fs_st_dev, fs_st_ino);
			}

			error_code current_path(cSmallVectorImpl<char> &result) {
				result.clear();

				const char *pwd = ::getenv("PWD");
				llvm::sys::fs::file_status PWDStatus, DotStatus;
				if (pwd && llvm::sys::path::is_absolute(pwd) &&
					!llvm::sys::fs::status(pwd, PWDStatus) &&
					!llvm::sys::fs::status(".", DotStatus) &&
					PWDStatus.getUniqueID() == DotStatus.getUniqueID()) {
					result.append(pwd, pwd + strlen(pwd));
					return error_code::success();
				}

#ifdef MAXPATHLEN
				result.reserve(MAXPATHLEN);
#else
				// For GNU Hurd
				result.reserve(1024);
#endif

				while (true) {
					if (::getcwd(result.data(), result.capacity()) == 0) {
						// See if there was a real error.
						if (errno != errc::not_enough_memory)
							return error_code(errno, system_category());
						// Otherwise there just wasn't enough space.
						result.reserve(result.capacity() * 2);
					}
					else
						break;
				}

				result.set_size(strlen(result.data()));
				return error_code::success();
			}

			error_code create_directory(const Twine &path, bool &existed) {
				SmallString<128> path_storage;
				cStringRef p = path.toNullTerminatedStringRef(path_storage);

				if (::mkdir(p.begin(), S_IRWXU | S_IRWXG) == -1) {
					if (errno != errc::file_exists)
						return error_code(errno, system_category());
					existed = true;
				}
				else
					existed = false;

				return error_code::success();
			}

			error_code create_hard_link(const Twine &to, const Twine &from) {
				// Get arguments.
				SmallString<128> from_storage;
				SmallString<128> to_storage;
				cStringRef f = from.toNullTerminatedStringRef(from_storage);
				cStringRef t = to.toNullTerminatedStringRef(to_storage);

				if (::link(t.begin(), f.begin()) == -1)
					return error_code(errno, system_category());

				return error_code::success();
			}

			error_code create_symlink(const Twine &to, const Twine &from) {
				// Get arguments.
				SmallString<128> from_storage;
				SmallString<128> to_storage;
				cStringRef f = from.toNullTerminatedStringRef(from_storage);
				cStringRef t = to.toNullTerminatedStringRef(to_storage);

				if (::symlink(t.begin(), f.begin()) == -1)
					return error_code(errno, system_category());

				return error_code::success();
			}

			error_code remove(const Twine &path, bool &existed) {
				SmallString<128> path_storage;
				cStringRef p = path.toNullTerminatedStringRef(path_storage);

				struct stat buf;
				if (stat(p.begin(), &buf) != 0) {
					if (errno != errc::no_such_file_or_directory)
						return error_code(errno, system_category());
					existed = false;
					return error_code::success();
				}

				// Note: this check catches strange situations. In all cases, LLVM should
				// only be involved in the creation and deletion of regular files.  This
				// check ensures that what we're trying to erase is a regular file. It
				// effectively prevents LLVM from erasing things like /dev/null, any block
				// special file, or other things that aren't "regular" files.
				if (!S_ISREG(buf.st_mode) && !S_ISDIR(buf.st_mode))
					return make_error_code(errc::operation_not_permitted);

				if (::remove(p.begin()) == -1) {
					if (errno != errc::no_such_file_or_directory)
						return error_code(errno, system_category());
					existed = false;
				}
				else
					existed = true;

				return error_code::success();
			}

			error_code rename(const Twine &from, const Twine &to) {
				// Get arguments.
				SmallString<128> from_storage;
				SmallString<128> to_storage;
				cStringRef f = from.toNullTerminatedStringRef(from_storage);
				cStringRef t = to.toNullTerminatedStringRef(to_storage);

				if (::rename(f.begin(), t.begin()) == -1)
					return error_code(errno, system_category());

				return error_code::success();
			}

			error_code resize_file(const Twine &path, uint64_t size) {
				SmallString<128> path_storage;
				cStringRef p = path.toNullTerminatedStringRef(path_storage);

				if (::truncate(p.begin(), size) == -1)
					return error_code(errno, system_category());

				return error_code::success();
			}

			error_code exists(const Twine &path, bool &result) {
				SmallString<128> path_storage;
				cStringRef p = path.toNullTerminatedStringRef(path_storage);

				if (::access(p.begin(), F_OK) == -1) {
					if (errno != errc::no_such_file_or_directory)
						return error_code(errno, system_category());
					result = false;
				}
				else
					result = true;

				return error_code::success();
			}

			bool can_write(const Twine &Path) {
				SmallString<128> PathStorage;
				cStringRef P = Path.toNullTerminatedStringRef(PathStorage);
				return 0 == access(P.begin(), W_OK);
			}

			bool can_execute(const Twine &Path) {
				SmallString<128> PathStorage;
				cStringRef P = Path.toNullTerminatedStringRef(PathStorage);

				if (0 != access(P.begin(), R_OK | X_OK))
					return false;
				struct stat buf;
				if (0 != stat(P.begin(), &buf))
					return false;
				if (!S_ISREG(buf.st_mode))
					return false;
				return true;
			}

			bool equivalent(file_status A, file_status B) {
				assert(status_known(A) && status_known(B));
				return A.fs_st_dev == B.fs_st_dev &&
					A.fs_st_ino == B.fs_st_ino;
			}

			error_code equivalent(const Twine &A, const Twine &B, bool &result) {
				file_status fsA, fsB;
				if (error_code ec = status(A, fsA)) return ec;
				if (error_code ec = status(B, fsB)) return ec;
				result = equivalent(fsA, fsB);
				return error_code::success();
			}

			static error_code fillStatus(int StatRet, const struct stat &Status,
				file_status &Result) {
				if (StatRet != 0) {
					error_code ec(errno, system_category());
					if (ec == errc::no_such_file_or_directory)
						Result = file_status(file_type::file_not_found);
					else
						Result = file_status(file_type::status_error);
					return ec;
				}

				file_type Type = file_type::type_unknown;

				if (S_ISDIR(Status.st_mode))
					Type = file_type::directory_file;
				else if (S_ISREG(Status.st_mode))
					Type = file_type::regular_file;
				else if (S_ISBLK(Status.st_mode))
					Type = file_type::block_file;
				else if (S_ISCHR(Status.st_mode))
					Type = file_type::character_file;
				else if (S_ISFIFO(Status.st_mode))
					Type = file_type::fifo_file;
				else if (S_ISSOCK(Status.st_mode))
					Type = file_type::socket_file;

				perms Perms = static_cast<perms>(Status.st_mode);
				Result =
					file_status(Type, Perms, Status.st_dev, Status.st_ino, Status.st_mtime,
					Status.st_uid, Status.st_gid, Status.st_size);

				return error_code::success();
			}

			error_code status(const Twine &Path, file_status &Result) {
				SmallString<128> PathStorage;
				cStringRef P = Path.toNullTerminatedStringRef(PathStorage);

				struct stat Status;
				int StatRet = ::stat(P.begin(), &Status);
				return fillStatus(StatRet, Status, Result);
			}

			error_code status(int FD, file_status &Result) {
				struct stat Status;
				int StatRet = ::fstat(FD, &Status);
				return fillStatus(StatRet, Status, Result);
			}

			error_code setLastModificationAndAccessTime(int FD, TimeValue Time) {
				timespec Times[2];
				Times[0].tv_sec = Time.toPosixTime();
				Times[0].tv_nsec = 0;
				Times[1] = Times[0];
				if (::futimens(FD, Times))


					{return error_code(errno, system_category());}
				return error_code::success();
			}

			error_code mapped_file_region::init(int FD, bool CloseFD, uint64_t Offset) {
				AutoFD ScopedFD(FD);
				if (!CloseFD)
					ScopedFD.take();

				// Figure out how large the file is.
				struct stat FileInfo;
				if (fstat(FD, &FileInfo) == -1)
					return error_code(errno, system_category());
				uint64_t FileSize = FileInfo.st_size;

				if (Size == 0)
					Size = FileSize;
				else if (FileSize < Size) {
					// We need to grow the file.
					if (ftruncate(FD, Size) == -1)
						return error_code(errno, system_category());
				}

				int flags = (Mode == readwrite) ? MAP_SHARED : MAP_PRIVATE;
				int prot = (Mode == readonly) ? PROT_READ : (PROT_READ | PROT_WRITE);

				flags |= MAP_FILE;
				Mapping = ::mmap(0, Size, prot, flags, FD, Offset);
				if (Mapping == MAP_FAILED)
					return error_code(errno, system_category());
				return error_code::success();
			}

			mapped_file_region::mapped_file_region(const Twine &path,
				mapmode mode,
				uint64_t length,
				uint64_t offset,
				error_code &ec)
				: Mode(mode)
				, Size(length)
				, Mapping() {
				// Make sure that the requested size fits within SIZE_T.
				if (length > std::numeric_limits<size_t>::max()) {
					ec = make_error_code(errc::invalid_argument);
					return;
				}

				SmallString<128> path_storage;
				cStringRef name = path.toNullTerminatedStringRef(path_storage);
				int oflags = (mode == readonly) ? O_RDONLY : O_RDWR;
				int ofd = ::open(name.begin(), oflags);
				if (ofd == -1) {
					ec = error_code(errno, system_category());
					return;
				}

				ec = init(ofd, true, offset);
				if (ec)
					Mapping = 0;
			}

			mapped_file_region::mapped_file_region(int fd,
				bool closefd,
				mapmode mode,
				uint64_t length,
				uint64_t offset,
				error_code &ec)
				: Mode(mode)
				, Size(length)
				, Mapping() {
				// Make sure that the requested size fits within SIZE_T.
				if (length > std::numeric_limits<size_t>::max()) {
					ec = make_error_code(errc::invalid_argument);
					return;
				}

				ec = init(fd, closefd, offset);
				if (ec)
					Mapping = 0;
			}

			mapped_file_region::~mapped_file_region() {
				if (Mapping)
					::munmap(Mapping, Size);
			}

#if AKJ_HAS_RVALUE_REFERENCES
			mapped_file_region::mapped_file_region(mapped_file_region &&other)
				: Mode(other.Mode), Size(other.Size), Mapping(other.Mapping) {
				other.Mapping = 0;
			}
#endif

			mapped_file_region::mapmode mapped_file_region::flags() const {
				assert(Mapping && "Mapping failed but used anyway!");
				return Mode;
			}

			uint64_t mapped_file_region::size() const {
				assert(Mapping && "Mapping failed but used anyway!");
				return Size;
			}

			char *mapped_file_region::data() const {
				assert(Mapping && "Mapping failed but used anyway!");
				assert(Mode != readonly && "Cannot get non const data for readonly mapping!");
				return reinterpret_cast<char*>(Mapping);
			}

			const char *mapped_file_region::const_data() const {
				assert(Mapping && "Mapping failed but used anyway!");
				return reinterpret_cast<const char*>(Mapping);
			}

			int mapped_file_region::alignment() {
				return process::get_self()->page_size();
			}

			error_code detail::directory_iterator_construct(detail::DirIterState &it,
				cStringRef path){
				SmallString<128> path_null(path);
				DIR *directory = ::opendir(path_null.c_str());
				if (directory == 0)
					return error_code(errno, system_category());

				it.IterationHandle = reinterpret_cast<intptr_t>(directory);
				// Add something for replace_filename to replace.
				path::append(path_null, ".");
				it.CurrentEntry = directory_entry(path_null.str());
				return directory_iterator_increment(it);
			}

			error_code detail::directory_iterator_destruct(detail::DirIterState &it) {
				if (it.IterationHandle)
					::closedir(reinterpret_cast<DIR *>(it.IterationHandle));
				it.IterationHandle = 0;
				it.CurrentEntry = directory_entry();
				return error_code::success();
			}

			error_code detail::directory_iterator_increment(detail::DirIterState &it) {
				errno = 0;
				dirent *cur_dir = ::readdir(reinterpret_cast<DIR *>(it.IterationHandle));
				if (cur_dir == 0 && errno != 0) {
					return error_code(errno, system_category());
				}
				else if (cur_dir != 0) {
					cStringRef name(cur_dir->d_name, NAMLEN(cur_dir));
					if ((name.size() == 1 && name[0] == '.') ||
						(name.size() == 2 && name[0] == '.' && name[1] == '.'))
						return directory_iterator_increment(it);
					it.CurrentEntry.replace_filename(name);
				}
				else
					return directory_iterator_destruct(it);

				return error_code::success();
			}

			error_code get_magic(const Twine &path, uint32_t len,
				cSmallVectorImpl<char> &result) {
				SmallString<128> PathStorage;
				cStringRef Path = path.toNullTerminatedStringRef(PathStorage);
				result.set_size(0);

				// Open path.
				std::FILE *file = std::fopen(Path.data(), "rb");
				if (file == 0)
					return error_code(errno, system_category());

				// Reserve storage.
				result.reserve(len);

				// Read magic!
				size_t size = std::fread(result.data(), 1, len, file);
				if (std::ferror(file) != 0) {
					std::fclose(file);
					return error_code(errno, system_category());
				}
				else if (size != len) {
					if (std::feof(file) != 0) {
						std::fclose(file);
						result.set_size(size);
						return make_error_code(errc::value_too_large);
					}
				}
				std::fclose(file);
				result.set_size(size);
				return error_code::success();
			}

			error_code map_file_pages(const Twine &path, off_t file_offset, size_t size,
				bool map_writable, void *&result) {
				SmallString<128> path_storage;
				cStringRef name = path.toNullTerminatedStringRef(path_storage);
				int oflags = map_writable ? O_RDWR : O_RDONLY;
				int ofd = ::open(name.begin(), oflags);
				if (ofd == -1)
					return error_code(errno, system_category());
				AutoFD fd(ofd);
				int flags = map_writable ? MAP_SHARED : MAP_PRIVATE;
				int prot = map_writable ? (PROT_READ | PROT_WRITE) : PROT_READ;
#ifdef MAP_FILE
				flags |= MAP_FILE;
#endif
				result = ::mmap(0, size, prot, flags, fd, file_offset);
				if (result == MAP_FAILED) {
					return error_code(errno, system_category());
				}

				return error_code::success();
			}

			error_code unmap_file_pages(void *base, size_t size) {
				if (::munmap(base, size) == -1)
					return error_code(errno, system_category());

				return error_code::success();
			}

			error_code openFileForRead(const Twine &Name, int &ResultFD) {
				SmallString<128> Storage;
				cStringRef P = Name.toNullTerminatedStringRef(Storage);
				while ((ResultFD = open(P.begin(), O_RDONLY)) < 0) {
					if (errno != EINTR)
						return error_code(errno, system_category());
				}
				return error_code::success();
			}

			error_code openFileForWrite(const Twine &Name, int &ResultFD,
				sys::fs::OpenFlags Flags, unsigned Mode) {
				// Verify that we don't have both "append" and "excl".
				assert((!(Flags & sys::fs::F_Excl) || !(Flags & sys::fs::F_Append)) &&
					"Cannot specify both 'excl' and 'append' file creation flags!");

				int OpenFlags = O_WRONLY | O_CREAT;

				if (Flags & F_Append)
					OpenFlags |= O_APPEND;
				else
					OpenFlags |= O_TRUNC;

				if (Flags & F_Excl)
					OpenFlags |= O_EXCL;

				SmallString<128> Storage;
				cStringRef P = Name.toNullTerminatedStringRef(Storage);
				while ((ResultFD = open(P.begin(), OpenFlags, Mode)) < 0) {
					if (errno != EINTR)
						return error_code(errno, system_category());
				}
				return error_code::success();
			}

		} // end namespace fs
	} // end namespace sys
} // end namespace akj

#endif
#endif

