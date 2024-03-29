//===- Memory.cpp - Memory Handling Support ---------------------*- C++ -*-===//
//
//           originally from The LLVM Compiler Infrastructure
//
// Was distributed under the University of Illinois Open Source License.
//
//===----------------------------------------------------------------------===//
//
// This file defines some helpful functions for allocating memory and dealing
// with memory mapped files
//
//===----------------------------------------------------------------------===//

#include "Memory.hpp"
//#include "llvm/Support/Valgrind."


// Include the platform-specific parts of this class.
#ifdef _WIN32

#include <stdint.h>
#include "FatalError.hpp"
#include "ProcessUtils.hpp"

// The Windows.h header must be the last one included.
#include "SystemSpecific/Windows.hpp"


namespace {

	DWORD getWindowsProtectionFlags(unsigned Flags) {
		switch (Flags) {
			// Contrary to what you might expect, the Windows page protection flags
			// are not a bitwise combination of RWX values
		case akj::sys::Memory::MF_READ:
			return PAGE_READONLY;
		case akj::sys::Memory::MF_WRITE:
			// Note: PAGE_WRITE is not supported by VirtualProtect
			return PAGE_READWRITE;
		case akj::sys::Memory::MF_READ | akj::sys::Memory::MF_WRITE:
			return PAGE_READWRITE;
		case akj::sys::Memory::MF_READ | akj::sys::Memory::MF_EXEC:
			return PAGE_EXECUTE_READ;
			case akj::sys::Memory::MF_READ |
				akj::sys::Memory::MF_WRITE |
				akj::sys::Memory::MF_EXEC:
					return PAGE_EXECUTE_READWRITE;
			case akj::sys::Memory::MF_EXEC:
				return PAGE_EXECUTE;
			default:
				akj::FatalError::Die("Illegal memory protection flag specified!");
		}
		// Provide a default return value as required by some compilers.
		return PAGE_NOACCESS;
	}

	size_t getAllocationGranularity() {
		SYSTEM_INFO  Info;
		::GetSystemInfo(&Info);
		if (Info.dwPageSize > Info.dwAllocationGranularity)
			return Info.dwPageSize;
		else
			return Info.dwAllocationGranularity;
	}

} // namespace

namespace akj {
	namespace sys {

		//===----------------------------------------------------------------------===//
		//=== WARNING: Implementation here must contain only Win32 specific code
		//===          and must not be UNIX code
		//===----------------------------------------------------------------------===//

		MemoryBlock Memory::allocateMappedMemory(size_t NumBytes,
			const MemoryBlock *const NearBlock,
			unsigned Flags,
			error_code &EC) {
			EC = error_code::success();
			if (NumBytes == 0)
				return MemoryBlock();

			// While we'd be happy to allocate single pages, the Windows allocation
			// granularity may be larger than a single page (in practice, it is 64K)
			// so mapping less than that will create an unreachable fragment of memory.
			static const size_t Granularity = getAllocationGranularity();
			const size_t NumBlocks = (NumBytes + Granularity - 1) / Granularity;

			uintptr_t Start = NearBlock ? reinterpret_cast<uintptr_t>(NearBlock->base()) +
				NearBlock->size()
				: NULL;

			// If the requested address is not aligned to the allocation granularity,
			// round up to get beyond NearBlock. VirtualAlloc would have rounded down.
			if (Start && Start % Granularity != 0)
				Start += Granularity - Start % Granularity;

			DWORD Protect = getWindowsProtectionFlags(Flags);

			void *PA = ::VirtualAlloc(reinterpret_cast<void*>(Start),
				NumBlocks*Granularity,
				MEM_RESERVE | MEM_COMMIT, Protect);
			if (PA == NULL) {
				if (NearBlock) {
					// Try again without the NearBlock hint
					return allocateMappedMemory(NumBytes, NULL, Flags, EC);
				}
				EC = error_code(::GetLastError(), system_category());
				return MemoryBlock();
			}

			MemoryBlock Result;
			Result.Address = PA;
			Result.Size = NumBlocks*Granularity;
			;
			if (Flags & MF_EXEC)
				Memory::InvalidateInstructionCache(Result.Address, Result.Size);

			return Result;
		}

		error_code Memory::releaseMappedMemory(MemoryBlock &M) {
			if (M.Address == 0 || M.Size == 0)
				return error_code::success();

			if (!VirtualFree(M.Address, 0, MEM_RELEASE))
				return error_code(::GetLastError(), system_category());

			M.Address = 0;
			M.Size = 0;

			return error_code::success();
		}

		error_code Memory::protectMappedMemory(const MemoryBlock &M,
			unsigned Flags) {
			if (M.Address == 0 || M.Size == 0)
				return error_code::success();

			DWORD Protect = getWindowsProtectionFlags(Flags);

			DWORD OldFlags;
			if (!VirtualProtect(M.Address, M.Size, Protect, &OldFlags))
				return error_code(::GetLastError(), system_category());

			if (Flags & MF_EXEC)
				Memory::InvalidateInstructionCache(M.Address, M.Size);

			return error_code::success();
		}

		/// InvalidateInstructionCache - Before the JIT can run a block of code
		/// that has been emitted it must invalidate the instruction cache on some
		/// platforms.
		void Memory::InvalidateInstructionCache(
			const void *Addr, size_t Len) {
			FlushInstructionCache(GetCurrentProcess(), Addr, Len);
		}


		MemoryBlock Memory::AllocateRWX(size_t NumBytes,
			const MemoryBlock *NearBlock,
			std::string *ErrMsg) {
			MemoryBlock MB;
			error_code EC;
			MB = allocateMappedMemory(NumBytes, NearBlock,
				MF_READ | MF_WRITE | MF_EXEC, EC);
			if (EC != error_code::success() && ErrMsg) {
				MakeErrMsg(ErrMsg, EC.message());
			}
			return MB;
		}

		bool Memory::ReleaseRWX(MemoryBlock &M, std::string *ErrMsg) {
			error_code EC = releaseMappedMemory(M);
			if (EC == error_code::success())
				return false;
			MakeErrMsg(ErrMsg, EC.message());
			return true;
		}

		static DWORD getProtection(const void *addr) {
			MEMORY_BASIC_INFORMATION info;
			if (sizeof(info) == ::VirtualQuery(addr, &info, sizeof(info))) {
				return info.Protect;
			}
			return 0;
		}

		bool Memory::setWritable(MemoryBlock &M, std::string *ErrMsg) {
			if (!setRangeWritable(M.Address, M.Size)) {
				return MakeErrMsg(ErrMsg, "Cannot set memory to writeable: ");
			}
			return true;
		}

		bool Memory::setExecutable(MemoryBlock &M, std::string *ErrMsg) {
			if (!setRangeExecutable(M.Address, M.Size)) {
				return MakeErrMsg(ErrMsg, "Cannot set memory to executable: ");
			}
			return true;
		}

		bool Memory::setRangeWritable(const void *Addr, size_t Size) {
			DWORD prot = getProtection(Addr);
			if (!prot)
				return false;

			if (prot == PAGE_EXECUTE || prot == PAGE_EXECUTE_READ) {
				prot = PAGE_EXECUTE_READWRITE;
			}
			else if (prot == PAGE_NOACCESS || prot == PAGE_READONLY) {
				prot = PAGE_READWRITE;
			}

			DWORD oldProt;
			Memory::InvalidateInstructionCache(Addr, Size);
			return ::VirtualProtect(const_cast<LPVOID>(Addr), Size, prot, &oldProt)
				== TRUE;
		}

		bool Memory::setRangeExecutable(const void *Addr, size_t Size) {
			DWORD prot = getProtection(Addr);
			if (!prot)
				return false;

			if (prot == PAGE_NOACCESS) {
				prot = PAGE_EXECUTE;
			}
			else if (prot == PAGE_READONLY) {
				prot = PAGE_EXECUTE_READ;
			}
			else if (prot == PAGE_READWRITE) {
				prot = PAGE_EXECUTE_READWRITE;
			}

			DWORD oldProt;
			Memory::InvalidateInstructionCache(Addr, Size);
			return ::VirtualProtect(const_cast<LPVOID>(Addr), Size, prot, &oldProt)
				== TRUE;
		}

	} // namespace sys
} // namespace akj


#else

#include "SystemSpecific/Unix.hpp"

#include "_typedefs.hpp"
#include "FatalError.hpp"
#include "ProcessUtils.hpp"
#include <sys/mman.h>

#ifdef __APPLE__
#include <mach/mach.h>
#endif

#if defined(__mips__)
#  if defined(__OpenBSD__)
#    include <mips64/sysarch.h>
#  else
#    include <sys/cachectl.h>
#  endif
#endif

#ifdef __APPLE__
extern "C" void sys_icache_invalidate(const void *Addr, size_t len);
#else
extern "C" void __clear_cache(void *, void*);
#endif

namespace {

int getPosixProtectionFlags(unsigned Flags) {
  switch (Flags) {
  case akj::sys::Memory::MF_READ:
    return PROT_READ;
  case akj::sys::Memory::MF_WRITE:
    return PROT_WRITE;
  case akj::sys::Memory::MF_READ|akj::sys::Memory::MF_WRITE:
    return PROT_READ | PROT_WRITE;
  case akj::sys::Memory::MF_READ|akj::sys::Memory::MF_EXEC:
    return PROT_READ | PROT_EXEC;
  case akj::sys::Memory::MF_READ |
	 akj::sys::Memory::MF_WRITE |
	 akj::sys::Memory::MF_EXEC:
    return PROT_READ | PROT_WRITE | PROT_EXEC;
  case akj::sys::Memory::MF_EXEC:
#if defined(__FreeBSD__)
    // On PowerPC, having an executable page that has no read permission
    // can have unintended consequences.  The function InvalidateInstruction-
    // Cache uses instructions dcbf and icbi, both of which are treated by
    // the processor as loads.  If the page has no read permissions,
    // executing these instructions will result in a segmentation fault.
    // Somehow, this problem is not present on Linux, but it does happen
    // on FreeBSD.
    return PROT_READ | PROT_EXEC;
#else
    return PROT_EXEC;
#endif
  default:
    FatalError::Die("Illegal memory protection flag specified!");
  }
  // Provide a default return value as required by some compilers.
  return PROT_NONE;
}

} // namespace

namespace akj {
namespace sys {

MemoryBlock
Memory::allocateMappedMemory(size_t NumBytes,
                             const MemoryBlock *const NearBlock,
                             unsigned PFlags,
                             error_code &EC) {
  EC = error_code::success();
  if (NumBytes == 0)
    return MemoryBlock();

  static const size_t PageSize = process::get_self()->page_size();
  const size_t NumPages = (NumBytes+PageSize-1)/PageSize;

  int fd = -1;
#ifdef NEED_DEV_ZERO_FOR_MMAP
  static int zero_fd = open("/dev/zero", O_RDWR);
  if (zero_fd == -1) {
    EC = error_code(errno, system_category());
    return MemoryBlock();
  }
  fd = zero_fd;
#endif

  int MMFlags = MAP_PRIVATE |
  MAP_ANONYMOUS

  ; // Ends statement above

  int Protect = getPosixProtectionFlags(PFlags);

  // Use any near hint and the page size to set a page-aligned starting address
  uintptr_t Start = NearBlock ? reinterpret_cast<uintptr_t>(NearBlock->base()) +
                                      NearBlock->size() : 0;
  if (Start && Start % PageSize)
    Start += PageSize - Start % PageSize;

  void *Addr = ::mmap(reinterpret_cast<void*>(Start), PageSize*NumPages,
                      Protect, MMFlags, fd, 0);
  if (Addr == MAP_FAILED) {
    if (NearBlock) //Try again without a near hint
      return allocateMappedMemory(NumBytes, 0, PFlags, EC);

    EC = error_code(errno, system_category());
    return MemoryBlock();
  }

  MemoryBlock Result;
  Result.Address = Addr;
  Result.Size = NumPages*PageSize;

  if (PFlags & MF_EXEC)
    Memory::InvalidateInstructionCache(Result.Address, Result.Size);

  return Result;
}

error_code
Memory::releaseMappedMemory(MemoryBlock &M) {
  if (M.Address == 0 || M.Size == 0)
    return error_code::success();

  if (0 != ::munmap(M.Address, M.Size))
    return error_code(errno, system_category());

  M.Address = 0;
  M.Size = 0;

  return error_code::success();
}

error_code
Memory::protectMappedMemory(const MemoryBlock &M, unsigned Flags) {
  if (M.Address == 0 || M.Size == 0)
    return error_code::success();

  if (!Flags)
    return error_code(EINVAL, generic_category());

  int Protect = getPosixProtectionFlags(Flags);

  int Result = ::mprotect(M.Address, M.Size, Protect);
  if (Result != 0)
    return error_code(errno, system_category());

  if (Flags & MF_EXEC)
    Memory::InvalidateInstructionCache(M.Address, M.Size);

  return error_code::success();
}

/// AllocateRWX - Allocate a slab of memory with read/write/execute
/// permissions.  This is typically used for JIT applications where we want
/// to emit code to the memory then jump to it.  Getting this type of memory
/// is very OS specific.
///
MemoryBlock
Memory::AllocateRWX(size_t NumBytes, const MemoryBlock* NearBlock,
                    std::string *ErrMsg) {
  if (NumBytes == 0) return MemoryBlock();

  size_t PageSize = process::get_self()->page_size();
  size_t NumPages = (NumBytes+PageSize-1)/PageSize;

  int fd = -1;
#ifdef NEED_DEV_ZERO_FOR_MMAP
  static int zero_fd = open("/dev/zero", O_RDWR);
  if (zero_fd == -1) {
    MakeErrMsg(ErrMsg, "Can't open /dev/zero device");
    return MemoryBlock();
  }
  fd = zero_fd;
#endif

  int flags = MAP_PRIVATE |
#ifdef HAVE_MMAP_ANONYMOUS
  MAP_ANONYMOUS
#else
  MAP_ANON
#endif
  ;

  void* start = NearBlock ? (unsigned char*)NearBlock->base() +
                            NearBlock->size() : 0;

#if defined(__APPLE__) && defined(__arm__)
  void *pa = ::mmap(start, PageSize*NumPages, PROT_READ|PROT_EXEC,
                    flags, fd, 0);
#else
  void *pa = ::mmap(start, PageSize*NumPages, PROT_READ|PROT_WRITE|PROT_EXEC,
                    flags, fd, 0);
#endif
  if (pa == MAP_FAILED) {
    if (NearBlock) //Try again without a near hint
      return AllocateRWX(NumBytes, 0);

    MakeErrMsg(ErrMsg, "Can't allocate RWX Memory");
    return MemoryBlock();
  }

#if defined(__APPLE__) && defined(__arm__)
  kern_return_t kr = vm_protect(mach_task_self(), (vm_address_t)pa,
                                (vm_size_t)(PageSize*NumPages), 0,
                                VM_PROT_READ | VM_PROT_EXECUTE | VM_PROT_COPY);
  if (KERN_SUCCESS != kr) {
    MakeErrMsg(ErrMsg, "vm_protect max RX failed");
    return MemoryBlock();
  }

  kr = vm_protect(mach_task_self(), (vm_address_t)pa,
                  (vm_size_t)(PageSize*NumPages), 0,
                  VM_PROT_READ | VM_PROT_WRITE);
  if (KERN_SUCCESS != kr) {
    MakeErrMsg(ErrMsg, "vm_protect RW failed");
    return MemoryBlock();
  }
#endif

  MemoryBlock result;
  result.Address = pa;
  result.Size = NumPages*PageSize;

  return result;
}

bool Memory::ReleaseRWX(MemoryBlock &M, std::string *ErrMsg) {
  if (M.Address == 0 || M.Size == 0) return false;
  if (0 != ::munmap(M.Address, M.Size))
    return MakeErrMsg(ErrMsg, "Can't release RWX Memory");
  return false;
}

bool Memory::setWritable (MemoryBlock &M, std::string *ErrMsg) {
#if defined(__APPLE__) && defined(__arm__)
  if (M.Address == 0 || M.Size == 0) return false;
  Memory::InvalidateInstructionCache(M.Address, M.Size);
  kern_return_t kr = vm_protect(mach_task_self(), (vm_address_t)M.Address,
    (vm_size_t)M.Size, 0, VM_PROT_READ | VM_PROT_WRITE);
  return KERN_SUCCESS == kr;
#else
  return true;
#endif
}

bool Memory::setExecutable (MemoryBlock &M, std::string *ErrMsg) {
#if defined(__APPLE__) && defined(__arm__)
  if (M.Address == 0 || M.Size == 0) return false;
  Memory::InvalidateInstructionCache(M.Address, M.Size);
  kern_return_t kr = vm_protect(mach_task_self(), (vm_address_t)M.Address,
    (vm_size_t)M.Size, 0, VM_PROT_READ | VM_PROT_EXECUTE | VM_PROT_COPY);
  return KERN_SUCCESS == kr;
#elif defined(__arm__) || defined(__aarch64__)
  Memory::InvalidateInstructionCache(M.Address, M.Size);
  return true;
#else
  return true;
#endif
}

bool Memory::setRangeWritable(const void *Addr, size_t Size) {
#if defined(__APPLE__) && defined(__arm__)
  kern_return_t kr = vm_protect(mach_task_self(), (vm_address_t)Addr,
                                (vm_size_t)Size, 0,
                                VM_PROT_READ | VM_PROT_WRITE);
  return KERN_SUCCESS == kr;
#else
  return true;
#endif
}

bool Memory::setRangeExecutable(const void *Addr, size_t Size) {
#if defined(__APPLE__) && defined(__arm__)
  kern_return_t kr = vm_protect(mach_task_self(), (vm_address_t)Addr,
                                (vm_size_t)Size, 0,
                                VM_PROT_READ | VM_PROT_EXECUTE | VM_PROT_COPY);
  return KERN_SUCCESS == kr;
#else
  return true;
#endif
}

/// InvalidateInstructionCache - Before the JIT can run a block of code
/// that has been emitted it must invalidate the instruction cache on some
/// platforms.
void Memory::InvalidateInstructionCache(const void *Addr,
                                        size_t Len) {

// icache invalidation for PPC and ARM.
#if defined(__APPLE__)

#  if (defined(__POWERPC__) || defined (__ppc__) || \
     defined(_POWER) || defined(_ARCH_PPC)) || defined(__arm__)
  sys_icache_invalidate(const_cast<void *>(Addr), Len);
#  endif

#else

#  if (defined(__POWERPC__) || defined (__ppc__) || \
       defined(_POWER) || defined(_ARCH_PPC)) && defined(__GNUC__)
  const size_t LineSize = 32;

  const intptr_t Mask = ~(LineSize - 1);
  const intptr_t StartLine = ((intptr_t) Addr) & Mask;
  const intptr_t EndLine = ((intptr_t) Addr + Len + LineSize - 1) & Mask;

  for (intptr_t Line = StartLine; Line < EndLine; Line += LineSize)
    asm volatile("dcbf 0, %0" : : "r"(Line));
  asm volatile("sync");

  for (intptr_t Line = StartLine; Line < EndLine; Line += LineSize)
    asm volatile("icbi 0, %0" : : "r"(Line));
  asm volatile("isync");
#  elif (defined(__arm__) || defined(__aarch64__)) && defined(__GNUC__)
  // FIXME: Can we safely always call this for __GNUC__ everywhere?
  const char *Start = static_cast<const char *>(Addr);
  const char *End = Start + Len;
  __clear_cache(const_cast<char *>(Start), const_cast<char *>(End));
#  elif defined(__mips__)
  const char *Start = static_cast<const char *>(Addr);
#    if defined(ANDROID)
  // The declaration of "cacheflush" in Android bionic:
  // extern int cacheflush(long start, long end, long flags);
  const char *End = Start + Len;
  long LStart = reinterpret_cast<long>(const_cast<char *>(Start));
  long LEnd = reinterpret_cast<long>(const_cast<char *>(End));
  cacheflush(LStart, LEnd, BCACHE);
#    else
  cacheflush(const_cast<char *>(Start), Len, BCACHE);
#    endif
#  endif

#endif  // end apple

 // ValgrindDiscardTranslations(Addr, Len);
}

} // namespace sys
} // namespace akj

#endif

