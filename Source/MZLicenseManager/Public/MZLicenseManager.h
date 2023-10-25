/*
 * Copyright MediaZ AS. All Rights Reserved.
 */

#pragma once
#include "Modules/ModuleInterface.h"

class MZLICENSEMANAGER_API FMZLicenseManager : public IModuleInterface
{
public:
	//Empty constructor
	FMZLicenseManager();

	//Called on startup of the module on Unreal Engine start
	virtual void StartupModule() override;

	//Called on shutdown of the module on Unreal Engine exit
	virtual void ShutdownModule() override;

	bool RegisterFeature(AActor* actor, USceneComponent* component, FProperty* property, FString featureName,
	                     uint32_t count, FString message = "");
	
	bool UnregisterFeature(AActor* actor, USceneComponent* component, FProperty* property, FString featureName);

private:

	bool UpdateFeature(bool register, AActor* actor, USceneComponent* component, FProperty* property, FString featureName, uint32_t count = 0, FString message = "");
	
};
