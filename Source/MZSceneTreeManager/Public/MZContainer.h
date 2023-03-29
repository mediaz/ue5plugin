#pragma once
#include "CoreMinimal.h"
#include "AppEvents_generated.h"
#include "RealityTrack.h"
#include "MZClient.h"

class MZSCENETREEMANAGER_API MZNewProperty;

class MZSCENETREEMANAGER_API MZContainerRef {
public:
	MZContainerRef(MZContainerRef* owner = nullptr) : Owner(owner) {}
	virtual ~MZContainerRef() = default;
	bool IsValid() {
		if (Owner && !Owner->IsValid())
			return false;
		return IsValidImpl() || UpdateRef();
	}
	uint8* Get() {
		if (!IsValid())
			return nullptr;
		return GetImpl();
	}
	virtual UObject* GetAsUObject() {
		return nullptr;
	}
	virtual void MarkUpdate(MZNewProperty* property) {

	}

	explicit operator bool() {
		return IsValid();
	}

protected:
	virtual bool UpdateRef() {
		return true;
	}
	virtual bool IsValidImpl() = 0;
	virtual uint8* GetImpl() = 0;
	MZContainerRef* Owner = nullptr;
};

class MZSCENETREEMANAGER_API MZActorRef : public MZContainerRef {
public:
	MZActorRef(TObjectPtr<AActor> actor, MZContainerRef* owner = nullptr);
	AActor* GetAsActor() {
		return (AActor*)Get();
	}
	AActor* operator->() {
		return GetAsActor();
	}
	UObject* GetAsUObject() override{
		return GetAsActor();
	}
protected:
	bool UpdateRef() override;
	bool IsValidImpl() override;
	uint8* GetImpl() override;
	UPROPERTY()
	TWeakObjectPtr<AActor> Actor;

	FGuid ActorGuid;
};

class MZSCENETREEMANAGER_API MZComponentRef : public MZContainerRef {
public:
	MZComponentRef(TObjectPtr<UActorComponent> actorComponent, MZActorRef* ownerActorRef);
	MZComponentRef(TObjectPtr<UActorComponent> actorComponent, MZComponentRef* ownerComponent);
	void MarkUpdate(MZNewProperty* property) override;
	
	UActorComponent* GetAsComponent() {
		return (UActorComponent*)Get();
	}
	UActorComponent* operator->() {
		return GetAsComponent();
	}
	UObject* GetAsUObject() override {
		return GetAsComponent();
	}
	MZActorRef* GetOwnerActor() {
		return OwnerActor;
	}
protected:
	bool UpdateRef() override;
	bool IsValidImpl() override;
	uint8* GetImpl() override;
private:
	UPROPERTY()
		TWeakObjectPtr<UActorComponent> Component;
	MZActorRef* OwnerActor;
	FName ComponentProperty;
	FString PathToComponent;
};

class MZSCENETREEMANAGER_API MZStructRef : public MZContainerRef {
public:
	MZStructRef(FStructProperty* structProperty, MZContainerRef* owner) : MZContainerRef(owner), StructProperty(structProperty)
	{ 
		assert(owner);
	}
private:
	bool UpdateRef() override 
	{
		return true;
	}
	bool IsValidImpl() override 
	{
		return true;
	}
	uint8* GetImpl() override {
		return StructProperty->ContainerPtrToValuePtr<uint8>(Owner->Get());
	}
	FStructProperty* StructProperty;
};

class MZSCENETREEMANAGER_API MZCustomContainerRef : public MZContainerRef {
public:
	MZCustomContainerRef(uint8* ptr, MZContainerRef* owner = nullptr) : MZContainerRef(owner), Ptr(ptr) {}

private:
	bool UpdateRef() override {
		return true;
	}
	bool IsValidImpl() {
		return Ptr;
	}
	uint8* GetImpl() {
		return Ptr;
	}
	uint8* Ptr;
};

class MZSCENETREEMANAGER_API MZProperty {
public:
	MZProperty(MZContainerRef* container) : Container(container) {}
	virtual ~MZProperty();
	virtual std::vector<uint8> GetPropertyValue() = 0;
	virtual bool SetPropertyValue(std::span<const uint8> data) = 0;
	virtual std::vector<flatbuffers::Offset<mz::fb::Pin>> Serialize(flatbuffers::FlatBufferBuilder& fbb);

	std::vector<flatbuffers::Offset<mz::fb::MetaDataEntry>> SerializeMetaData(flatbuffers::FlatBufferBuilder& fbb);

	FGuid Id;
	MZContainerRef* Container;

	FString DisplayName;
	FString CategoryName;
	FString UIMaxString;
	FString UIMinString;
	
	bool IsAdvanced = false;
	bool ReadOnly = false;
	std::string TypeName;

	mz::fb::ShowAs PinShowAs = mz::fb::ShowAs::PROPERTY;
	TMap<FString, FString> mzMetaDataMap;
	bool transient = true;
	bool IsChanged = false;
};

class MZSCENETREEMANAGER_API MZFProperty : public MZProperty {
public:
	MZFProperty(MZContainerRef* container) : MZProperty(container) 
	{
	}
	~MZFProperty() override;
	virtual FProperty* GetProperty() = 0;
};

class MZSCENETREEMANAGER_API MZFStructProperty : public MZFProperty {
	MZFStructProperty(FStructProperty* prop, MZContainerRef* container) : MZFProperty(container), 
		Property(prop), StructRef(prop, container)
	{
		CreateChildren();
	}

	std::vector<uint8> GetPropertyValue() override { return {}; }
	bool SetPropertyValue(std::span<const uint8> data) override { return false; }
	std::vector<flatbuffers::Offset<mz::fb::Pin>> Serialize(flatbuffers::FlatBufferBuilder& fbb) override;


private:
	void CreateChildren();
	MZStructRef StructRef;
	std::vector<MZFProperty> children;
	FStructProperty* Property;
};

template<typename T, typename CppType = T::TCppType>
class MZSCENETREEMANAGER_API MZFNumericProperty : public MZFProperty {
public:
	using CpppType = CppType;
	MZFNumericProperty(T* prop, MZContainerRef* container) : MZFProperty(container), Property(prop) {}
	std::vector<uint8> GetPropertyValue() override
	{
		CppType value = Property->GetPropertyValue(Container->Get());
		std::vector<uint8> vec(sizeof(CppType), 0);
		memcpy(vec.data(), &value, sizeof(CppType));
		return vec;
	}
	bool SetPropertyValue(std::span<const uint8> data) override
	{
		CppType value = *(CppType*)data.data();
		Property->SetPropertyValue_InContainer(Container->Get(), value);
		return true;
	}
	FProperty* GetProperty() override {
		return Property;
	}
private:
	T* Property;
};

struct ContainerPropertyPair {
	MZContainerRef* Container;
	FProperty* Property;
	bool operator==(const ContainerPropertyPair& other) {

	}
	//custom hash
};

struct MZPortal
{
	FGuid Id;
	FGuid SourceId;

	FString DisplayName;
	FString TypeName;
	FString CategoryName;
	mz::fb::ShowAs ShowAs;
};

class FMZPropertyManager
{
public:
	FMZPropertyManager();

	static FMZPropertyManager& GetInstance();

	void CreatePortal(FGuid PropertyId, mz::fb::ShowAs ShowAs);
	void CreatePortal(FProperty* uproperty, UObject* Container, mz::fb::ShowAs ShowAs);
	flatbuffers::Offset<mz::fb::Pin> SerializePortal(flatbuffers::FlatBufferBuilder& fbb, MZPortal Portal, MZProperty* SourceProperty);

	FMZClient* MZClient;

	TMap<FGuid, FGuid> PropertyToPortalPin;
	TMap<FGuid, MZPortal> PortalPinsById;

	// adds created property to PinToPropertyCache
	void AddProperty(MZProperty* property);
	// decides which MZFProperty to create, adds created property to ContainerFPropertyToPropertyCache and calls AddProperty
	TUniquePtr<MZFProperty> CreateFProperty(FProperty* fprop, MZContainerRef* container);

	void RemoveProperty(MZProperty* property);
	void RemoveFProperty(MZFProperty* property);

	TMap<FGuid, MZProperty*> PinToPropertyCache;
	TMap<ContainerPropertyPair, MZFProperty*> ContainerFPropertyToPropertyCache;

	void Reset(bool ResetPortals = true);
};