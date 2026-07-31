// Copyright Epic Games, Inc. All Rights Reserved.

#include "DWM_Dev.h"
#include "Modules/ModuleManager.h"

#if WITH_EDITOR
#include "DwmInteractiveDoorEditorTools.h"
#endif

class FDwmDevModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		FDefaultGameModuleImpl::StartupModule();

#if WITH_EDITOR
		FDwmInteractiveDoorEditorTools::Register();
#endif
	}

	virtual void ShutdownModule() override
	{
#if WITH_EDITOR
		FDwmInteractiveDoorEditorTools::Unregister();
#endif

		FDefaultGameModuleImpl::ShutdownModule();
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE(FDwmDevModule, DWM_Dev, "DWM_Dev");
