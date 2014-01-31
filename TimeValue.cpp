//===-- TimeValue.cpp - Implement OS TimeValue Concept ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements the operating system TimeValue concept.
//
//===----------------------------------------------------------------------===//

#include "TimeValue.hpp"



namespace akj {
using namespace sys;

const TimeValue::SecondsType
  TimeValue::PosixZeroTimeSeconds = -946684800;
const TimeValue::SecondsType
  TimeValue::Win32ZeroTimeSeconds = -12591158400ULL;

const TimeValue TimeValue::MinTime       = TimeValue ( INT64_MIN,0 );
const TimeValue TimeValue::MaxTime       = TimeValue ( INT64_MAX,0 );
const TimeValue TimeValue::ZeroTime      = TimeValue ( 0,0 );
const TimeValue TimeValue::PosixZeroTime = TimeValue ( PosixZeroTimeSeconds,0 );
const TimeValue TimeValue::Win32ZeroTime = TimeValue ( Win32ZeroTimeSeconds,0 );

void
TimeValue::normalize( void ) {
  if ( nanos_ >= NANOSECONDS_PER_SECOND ) {
    do {
      seconds_++;
      nanos_ -= NANOSECONDS_PER_SECOND;
    } while ( nanos_ >= NANOSECONDS_PER_SECOND );
  } else if (nanos_ <= -NANOSECONDS_PER_SECOND ) {
    do {
      seconds_--;
      nanos_ += NANOSECONDS_PER_SECOND;
    } while (nanos_ <= -NANOSECONDS_PER_SECOND);
  }

  if (seconds_ >= 1 && nanos_ < 0) {
    seconds_--;
    nanos_ += NANOSECONDS_PER_SECOND;
  } else if (seconds_ < 0 && nanos_ > 0) {
    seconds_++;
    nanos_ -= NANOSECONDS_PER_SECOND;
  }
}

}

/// Include the platform specific portion of TimeValue class
#ifdef _WIN32

#include "SystemSpecific/Windows.hpp"

#include <time.h>

namespace akj {
	using namespace sys;

	//===----------------------------------------------------------------------===//
	//=== WARNING: Implementation here must contain only Win32 specific code.
	//===----------------------------------------------------------------------===//

	TimeValue TimeValue::now() {
		uint64_t ft;
		GetSystemTimeAsFileTime(reinterpret_cast<FILETIME *>(&ft));

		TimeValue t(0, 0);
		t.fromWin32Time(ft);
		return t;
	}

	std::string TimeValue::str() const {
		struct tm *LT;
#ifdef __MINGW32__
		// Old versions of mingw don't have _localtime64_s. Remove this once we drop support
		// for them.
		time_t OurTime = time_t(this->toEpochTime());
		LT = ::localtime(&OurTime);
		assert(LT);
#else
		struct tm Storage;
		__time64_t OurTime = this->toEpochTime();
		int Error = ::_localtime64_s(&Storage, &OurTime);
		assert(!Error);
		LT = &Storage;
#endif

		char Buffer[25];
		// FIXME: the windows version of strftime doesn't support %e
		strftime(Buffer, 25, "%b %d %H:%M %Y", LT);
		assert((Buffer[3] == ' ' && isdigit(Buffer[5]) && Buffer[6] == ' ') ||
			"Unexpected format in strftime()!");
		// Emulate %e on %d to mute '0'.
		if (Buffer[4] == '0')
			Buffer[4] = ' ';
		return std::string(Buffer);
	}


}



#else

#include "SystemSpecific/Unix.hpp"


namespace akj {
	using namespace sys;

	std::string TimeValue::str() const {
		time_t OurTime = time_t(this->toEpochTime());
		struct tm Storage;
		struct tm *LT = ::localtime_r(&OurTime, &Storage);
		assert(LT);
		char Buffer[25];
		strftime(Buffer, 25, "%b %e %H:%M %Y", LT);
		return std::string(Buffer);
	}

	TimeValue TimeValue::now() {
		struct timeval the_time;
		timerclear(&the_time);
		if (0 != ::gettimeofday(&the_time, 0)) {
			// This is *really* unlikely to occur because the only gettimeofday
			// errors concern the timezone parameter which we're passing in as 0.
			// In the unlikely case it does happen, just return MinTime, no error
			// message needed.
			return MinTime;
		}

		return TimeValue(
			static_cast<TimeValue::SecondsType>(the_time.tv_sec +
			PosixZeroTimeSeconds),
			static_cast<TimeValue::NanoSecondsType>(the_time.tv_usec *
			NANOSECONDS_PER_MICROSECOND));
	}

}



#endif

