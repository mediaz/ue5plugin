#pragma once
#include "MZContainer.h"

struct MZSCENETREEMANAGER_API MZFunction
{
	MZFunction(UObject* container, UFunction* function);

	UFunction* Function;
	UObject* Container;
	FString FunctionName;
	FString DisplayName;
	FString CategoryName;
	FGuid Id;
	uint8* Parameters = nullptr;
	std::vector<TUniquePtr<MZProperty>> Properties;
	std::vector<MZProperty*> OutProperties;

	flatbuffers::Offset<mz::fb::Node> Serialize(flatbuffers::FlatBufferBuilder& fbb);

	void Invoke();
};

struct MZSCENETREEMANAGER_API MZCustomFunction
{
	FGuid Id;
	std::function<void(TMap<FGuid, std::vector<uint8>> pins)> Function;
	TMap<FGuid, std::string> Params;
	std::function<flatbuffers::Offset<mz::fb::Node>(flatbuffers::FlatBufferBuilder& fbb)> Serialize;
};
