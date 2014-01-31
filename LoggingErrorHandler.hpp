#pragma once
#include "StringRef.hpp"
#include "FatalError.hpp"
#include "Log.hpp"


namespace akj
{
	class LoggingErrorHandler : public FatalErrorHandler
	{
		public:
		virtual void ReportError(cStringRef error_msg);
	};

	inline void LoggingErrorHandler::ReportError(cStringRef error_msg)
	{
		Log::Critical(error_msg);
		exit(1);
	}
}
