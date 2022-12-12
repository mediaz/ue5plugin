#if WITH_EDITOR
#include "MZActorFunctions.h"

//TODO make a class for functionsss

MZFunction::MZFunction(UObject* container, UFunction* function)
{
	Function = function;
	Container = container;
	Id = FGuid::NewGuid();

	static const FName NAME_DisplayName("DisplayName");
	static const FName NAME_Category("Category");
	FunctionName = function->GetFName().ToString();
#if WITH_EDITOR
	DisplayName = function->GetDisplayNameText().ToString();
	CategoryName = function->HasMetaData(NAME_Category) ? function->GetMetaData(NAME_Category) : "Default";
#else
	DisplayName = FunctionName;
	CategoryName = "Default";
#endif

}

flatbuffers::Offset<mz::fb::Node> MZFunction::Serialize(flatbuffers::FlatBufferBuilder& fbb)
{
	std::vector<flatbuffers::Offset<mz::fb::Pin>> pins;
	for (auto property : Properties)
	{
		pins.push_back(property->Serialize(fbb));
	}
	return mz::fb::CreateNodeDirect(fbb, (mz::fb::UUID*)&Id, TCHAR_TO_UTF8(*DisplayName), TCHAR_TO_UTF8(*Function->GetClass()->GetFName().ToString()), false, true, &pins, 0, mz::fb::NodeContents::Job, mz::fb::CreateJob(fbb, mz::fb::JobType::CPU).Union(), "UE5", 0, TCHAR_TO_UTF8(*CategoryName));
}

void MZFunction::Invoke() // runs in game thread
{
	//Function->SetMetaData()
	/*if (!Parameters)
	{
		
		return;
	}*/
	Container->Modify();
	Container->ProcessEvent(Function, Parameters);
}

#endif