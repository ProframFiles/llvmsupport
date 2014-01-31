#pragma once
#include "Twine.hpp"

#include "OwningPtr.hpp"

#ifndef AKJ_STRINGIFY
	#define AKJ_STRINGIFY(thing) AKJ_DO_STRINGIFY(thing)
	#define AKJ_DO_STRINGIFY(thing) #thing
#endif


namespace akj
{
	class Twine;

	class FatalErrorHandler
	{
	public:
		virtual ~FatalErrorHandler();
		virtual void ReportAndDie(cStringRef error_msg) = 0;
	};



	class FatalError
	{
	public:
		//////////////////////////////////////////////////////////////////////////
		// This is not thread safe! only use it when there is no risk of contention
		// (ideally right at startup, before any threads get initialized)
		//////////////////////////////////////////////////////////////////////////
		static void InstallErrorHandler(FatalErrorHandler* handler);
		template <class tDerivedHandler>
		static void InstallErrorHandler()
		{
			sInstalledHandler.reset(new tDerivedHandler);
		}
		static void Die(const Twine& error_msg);
	private:
		static OwningPtr<FatalErrorHandler> sInstalledHandler;
	};
}
