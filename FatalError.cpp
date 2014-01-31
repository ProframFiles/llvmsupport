#include "FatalError.hpp"
#include "StringRef.hpp"
#include "SmallString.hpp"
#include "Twine.hpp"

namespace akj 
{
	namespace
	{
		SmallString<1024> sConversionBuffer;
	}


	class cAssertingErrorHandler : public FatalErrorHandler 
	{
	public:
		virtual void ReportAndDie(cStringRef error_msg)
		{
			std::string str = error_msg;
			assert(false);
			exit(1);
		}
	};

	OwningPtr<FatalErrorHandler> FatalError::sInstalledHandler(new cAssertingErrorHandler);

	FatalErrorHandler::~FatalErrorHandler(){}


	void FatalError::Die(const Twine& error_msg)
	{
		if (sInstalledHandler)
		{
			cStringRef str = error_msg.toNullTerminatedStringRef(sConversionBuffer);
			sInstalledHandler->ReportAndDie(str);
		}
		exit(1);
	}

	void FatalError::InstallErrorHandler(FatalErrorHandler* handler)
	{
		sInstalledHandler.reset(handler);
	}



}
