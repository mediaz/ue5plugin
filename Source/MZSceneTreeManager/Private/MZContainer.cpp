#include "MZContainer.h"
#include "MZSceneTreeManager.h"
#include "EngineUtils.h"

MZActorRef::MZActorRef(TObjectPtr<AActor> actor, MZContainerRef* owner) : MZContainerRef(owner)
{
	if (actor)
	{
		Actor = TWeakObjectPtr<AActor>(actor);
		ActorGuid = Actor->GetActorGuid();
	}
}

bool MZActorRef::UpdateRef()
{
	Actor = nullptr;
	UWorld* World;
	if (FMZSceneTreeManager::daWorld)
	{
		World = FMZSceneTreeManager::daWorld;
	}
	else
	{
		World = GEngine->GetWorldContextFromGameViewport(GEngine->GameViewport)->World();
	}

	if (!ActorGuid.IsValid())
	{
		return false;
	}

	TMap<FGuid, AActor*> sceneActorMap;
	if (::IsValid(World))
	{
		for (TActorIterator<AActor> ActorItr(World); ActorItr; ++ActorItr)
		{
			if (ActorItr->GetActorGuid() == ActorGuid)
			{
				Actor = TWeakObjectPtr<AActor>(*ActorItr);
				return true;
			}
		}
	}
	return false;
}

bool MZActorRef::IsValidImpl()
{
	return Actor.IsValid();
}

uint8* MZActorRef::GetImpl()
{
	return reinterpret_cast<uint8*>(Actor.Get());
}

MZComponentRef::MZComponentRef(TObjectPtr<UActorComponent> actorComponent, MZActorRef* ownerActorRef) : MZContainerRef(nullptr), OwnerActor(ownerActorRef)
{
	if (actorComponent)
	{
		Component = TWeakObjectPtr<UActorComponent>(actorComponent);
		ComponentProperty = Component->GetFName();
		PathToComponent = Component->GetPathName(actorComponent->GetOwner());
	}
}

MZComponentRef::MZComponentRef(TObjectPtr<UActorComponent> actorComponent, MZComponentRef* ownerComponent) : MZContainerRef(nullptr), OwnerActor(ownerComponent->GetOwnerActor())
{
}

void MZComponentRef::MarkUpdate(MZNewProperty* property)
{
}

bool MZComponentRef::UpdateRef()
{
	auto* owner = Component->GetOwner();

	auto comp = FindObject<UActorComponent>(owner, *PathToComponent);
	if (comp)
	{
		Component = TWeakObjectPtr<UActorComponent>(comp);
		return true;
	}

	return false;
}

bool MZComponentRef::IsValidImpl()
{
	return PathToComponent.IsEmpty();
}

uint8* MZComponentRef::GetImpl()
{
	return (uint8*)Component.Get();
}

MZProperty::~MZProperty()
{
	FMZPropertyManager::GetInstance()->RemoveProperty(this);
}

std::vector<flatbuffers::Offset<mz::fb::Pin>> MZProperty::Serialize(flatbuffers::FlatBufferBuilder& fbb)
{
	std::vector<flatbuffers::Offset<mz::fb::MetaDataEntry>> metadata = SerializeMetaData(fbb);

	std::vector<flatbuffers::Offset<mz::fb::Pin>> vec;
	auto data = GetPropertyValue();
	vec.push_back(mz::fb::CreatePinDirect(fbb, (mz::fb::UUID*)&Id, TCHAR_TO_UTF8(*DisplayName), TypeName.c_str(), PinShowAs, mz::fb::CanShowAs::INPUT_OUTPUT_PROPERTY, TCHAR_TO_UTF8(*CategoryName), 0, &data, 0, 0, 0, 0, 0, ReadOnly, IsAdvanced, transient, &metadata, 0, mz::fb::PinContents::JobPin));
	return vec;
}

std::vector<flatbuffers::Offset<mz::fb::MetaDataEntry>> MZProperty::SerializeMetaData(flatbuffers::FlatBufferBuilder& fbb)
{
	std::vector<flatbuffers::Offset<mz::fb::MetaDataEntry>> metadata;
	for (auto [key, value] : mzMetaDataMap)
	{
		metadata.push_back(mz::fb::CreateMetaDataEntryDirect(fbb, TCHAR_TO_UTF8(*key), TCHAR_TO_UTF8(*value)));
	}

	return metadata;
}

FMZPropertyManager& FMZPropertyManager::GetInstance() {
	static FMZPropertyManager manager = FModuleManager::GetModuleChecked<FMZSceneTreeManager>("MZSceneTreeManager").MZPropertyManager;
	return &manager;
}

MZFProperty::~MZFProperty()
{
	FMZPropertyManager::GetInstance()->RemoveFProperty(this);
}
