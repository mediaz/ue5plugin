/*
 * Copyright MediaZ AS. All Rights Reserved.
 */

#pragma once
#include "CoreMinimal.h"
#include <vector>
#pragma warning (disable : 4800)
#pragma warning (disable : 4668)
#include "AppEvents_generated.h"
#include "MZDataStructures/Public/MZTrack.h"
#include "MZClient.h"

class MZStructProperty;

class MZSCENETREEMANAGER_API MZActorReference
{
public:
	MZActorReference(TObjectPtr<AActor> actor);
	MZActorReference();
	
	AActor* Get();
	
	explicit operator bool() {
		return !!(Get());
	}

	AActor* operator->()
	{
		return Get();
	}

	bool UpdateActorPointer(UWorld* World);
	bool UpdateActualActorPointer();

	bool InvalidReference = false;
private:
	
	UPROPERTY()
	TWeakObjectPtr<AActor> Actor;
	
	FGuid ActorGuid;
	
};

class MZSCENETREEMANAGER_API MZComponentReference
{

public:
	MZComponentReference(TObjectPtr<UActorComponent> actorComponent);
	MZComponentReference();

	UActorComponent* Get();
	AActor* GetOwnerActor();

	explicit operator bool() {
		return !!(Get());
	}

	UActorComponent* operator->()
	{
		return Get();
	}

	bool UpdateActualComponentPointer();
	MZActorReference Actor;
	bool InvalidReference = false;

private:
	UPROPERTY()
	TWeakObjectPtr<UActorComponent> Component;
	

	FName ComponentProperty;
	FString PathToComponent;

};

class MZSCENETREEMANAGER_API MZProperty : public TSharedFromThis<MZProperty>
{
public:
	MZProperty(UObject* Container, FProperty* UProperty, FString ParentCategory = FString(), uint8 * StructPtr = nullptr, MZStructProperty* parentProperty = nullptr);

	void SetPropValue(void* val, size_t size, uint8* customContainer = nullptr);
	UObject* GetRawObjectContainer();
	void* GetRawContainer();

	virtual std::vector<uint8> UpdatePinValue(uint8* customContainer = nullptr);
	//std::vector<uint8> GetValue(uint8* customContainer = nullptr);
	void MarkState();
	virtual flatbuffers::Offset<mz::fb::Pin> Serialize(flatbuffers::FlatBufferBuilder& fbb);
	std::vector<flatbuffers::Offset<mz::fb::MetaDataEntry>> SerializeMetaData(flatbuffers::FlatBufferBuilder& fbb);
	virtual flatbuffers::Offset<mz::fb::Visualizer> SerializeVisualizer(flatbuffers::FlatBufferBuilder& fbb) {return 0;};
	
	FProperty* Property;

	MZActorReference ActorContainer;
	MZComponentReference ComponentContainer;
	UObject* ObjectPtr = nullptr;
	uint8* StructPtr = nullptr;

	FString PropertyName;
	FString DisplayName;
	FString CategoryName;
	FString ToolTipText;
	FString UIMaxString;
	FString UIMinString;
	FString EditConditionPropertyName;
	FProperty* EditConditionProperty;
	bool IsAdvanced = false;
	bool ReadOnly = false;
	std::string TypeName;
	FGuid Id;
	std::vector<uint8_t> data; //wrt mediaZ standarts
	std::vector<uint8_t> default_val; //wrt mediaZ standarts
	std::vector<uint8_t> min_val; //wrt mediaZ standarts
	std::vector<uint8_t> max_val; //wrt mediaZ standarts
	mz::fb::ShowAs PinShowAs = mz::fb::ShowAs::PROPERTY;
	mz::fb::CanShowAs PinCanShowAs = mz::fb::CanShowAs::INPUT_OUTPUT_PROPERTY;
	std::vector<TSharedPtr<MZProperty>> childProperties;
	TMap<FString, FString> mzMetaDataMap;
	bool transient = true;
	bool IsChanged = false;

	virtual ~MZProperty() {}
protected:
	virtual void SetPropValue_Internal(void* val, size_t size, uint8* customContainer = nullptr);
	virtual void SetProperty_InCont(void* container, void* val);

private:
	void CallOnChangedFunction();

};

template<typename T, mz::tmp::StrLiteral LitType, typename CppType = T::TCppType>
requires std::is_base_of_v<FProperty, T>
class MZNumericProperty : public MZProperty {
public:
	MZNumericProperty(UObject* container, T* uproperty, FString parentCategory = FString(), uint8* StructPtr = nullptr, MZStructProperty* parentProperty = nullptr) :
		MZProperty(container, uproperty, parentCategory, StructPtr, parentProperty), Property(uproperty)
	{
		data = std::vector<uint8_t>(sizeof(CppType), 0);
		TypeName = LitType.val;
	}
	T* Property;
	virtual std::vector<uint8> UpdatePinValue(uint8* customContainer = nullptr) override 
	{
		void* container = nullptr;
		if (customContainer) container = customContainer;
		else if (ComponentContainer) container = ComponentContainer.Get();
		else if (ActorContainer) container = ActorContainer.Get();
		else if (ObjectPtr && IsValid(ObjectPtr)) container = ObjectPtr;
		else if (StructPtr) container = StructPtr;

		if (container)
		{
			CppType val = static_cast<CppType>(Property->GetPropertyValue_InContainer(container));
			memcpy(data.data(), &val, data.size());
		}

		return data;
	}

protected:
	virtual void SetProperty_InCont(void* container, void* val) override 
	{
		Property->SetPropertyValue_InContainer(container, (*(CppType*)val));
	}
};

using MZBoolProperty = MZNumericProperty<FBoolProperty, "bool">;
using MZFloatProperty = MZNumericProperty<FFloatProperty, "float">;
using MZDoubleProperty = MZNumericProperty<FDoubleProperty, "double">;
using MZInt8Property = MZNumericProperty<FInt8Property, "byte">;
using MZInt16Property = MZNumericProperty<FInt16Property, "short">;
using MZIntProperty = MZNumericProperty<FIntProperty, "int">;
using MZInt64Property = MZNumericProperty<FInt64Property, "long">;
using MZByteProperty = MZNumericProperty<FByteProperty, "ubyte">;
using MZUInt16Property = MZNumericProperty<FUInt16Property, "ushort">;
using MZUInt32Property = MZNumericProperty<FUInt32Property, "uint">;
using MZUInt64Property = MZNumericProperty<FUInt64Property, "ulong">;

class MZEnumProperty : public MZProperty
{
public:
	MZEnumProperty(UObject* container, FEnumProperty* enumprop, FNumericProperty* numericprop,  UEnum* uenum, FString parentCategory = FString(), uint8* StructPtr = nullptr, MZStructProperty* parentProperty = nullptr)
		: MZProperty(container, (FProperty*)(enumprop ? (FProperty*)enumprop : (FProperty*)numericprop), parentCategory, StructPtr, parentProperty), Enum(uenum), IndexProp(numericprop), EnumProperty(enumprop)
	{
		data = std::vector<uint8_t>(1, 0); 
		TypeName = "string";

		int EnumSize = Enum->NumEnums();
		for(int i = 0; i < EnumSize; i++)
		{
			NameMap.Add(Enum->GetNameByIndex(i).ToString(), Enum->GetValueByIndex(i));
		}

		
		flatbuffers::FlatBufferBuilder mb;

		std::vector<std::string> NameList;
		for (const auto& [name, _] : NameMap)
		{
			NameList.push_back(TCHAR_TO_UTF8(*name));
		}

		auto offset = mz::app::CreateUpdateStringList(mb, mz::fb::CreateStringList(mb, mb.CreateString(TCHAR_TO_UTF8(*Enum->GetFName().ToString())), mb.CreateVectorOfStrings(NameList)));
		mb.Finish(offset);

		auto buf = mb.Release();
		auto root = flatbuffers::GetRoot<mz::app::UpdateStringList>(buf.data());
		auto MZClient = &FModuleManager::LoadModuleChecked<FMZClient>("MZClient");
		MZClient->AppServiceClient->UpdateStringList(*root);
	}

	FString MediaZListName;
	TMap<FString, int64> NameMap;
	FString CurrentName;
	int64 CurrentValue;
	UEnum* Enum;
	FNumericProperty* IndexProp;
	FEnumProperty* EnumProperty;
	virtual flatbuffers::Offset<mz::fb::Pin> Serialize(flatbuffers::FlatBufferBuilder& fbb) override;
	virtual flatbuffers::Offset<mz::fb::Visualizer> SerializeVisualizer(flatbuffers::FlatBufferBuilder& fbb) override;
	virtual void SetPropValue_Internal(void* val, size_t size, uint8* customContainer = nullptr) override;
	virtual std::vector<uint8> UpdatePinValue(uint8* customContainer = nullptr) override; 

};

class MZTextProperty : public MZProperty
{
public:
	MZTextProperty(UObject* container, FTextProperty* uproperty, FString parentCategory = FString(), uint8* StructPtr = nullptr, MZStructProperty* parentProperty = nullptr)
		: MZProperty(container, uproperty, parentCategory, StructPtr, parentProperty), textprop(uproperty) 
	{
		data = std::vector<uint8_t>(1, 0);
		TypeName = "string";
	}

	FTextProperty* textprop;
	virtual void SetPropValue_Internal(void* val, size_t size, uint8* customContainer = nullptr) override;
	virtual std::vector<uint8> UpdatePinValue(uint8* customContainer = nullptr) override;

};

class MZNameProperty : public MZProperty
{
public:
	MZNameProperty(UObject* container, FNameProperty* uproperty, FString parentCategory = FString(), uint8* StructPtr = nullptr, MZStructProperty* parentProperty = nullptr)
		: MZProperty(container, uproperty, parentCategory, StructPtr, parentProperty), nameprop(uproperty) 
	{
		data = std::vector<uint8_t>(1, 0);
		TypeName = "string";
	}

	FNameProperty* nameprop;
	virtual void SetPropValue_Internal(void* val, size_t size, uint8* customContainer = nullptr) override;
	virtual std::vector<uint8> UpdatePinValue(uint8* customContainer = nullptr) override;

};

class MZStringProperty : public MZProperty
{
public:
	MZStringProperty(UObject* container, FStrProperty* uproperty, FString parentCategory = FString(), uint8* StructPtr = nullptr, MZStructProperty* parentProperty = nullptr)
		: MZProperty(container, uproperty, parentCategory, StructPtr, parentProperty), stringprop(uproperty)
	{
		data = std::vector<uint8_t>(1, 0);
		TypeName = "string";
	}

	FStrProperty* stringprop;
	virtual void SetPropValue_Internal(void* val, size_t size, uint8* customContainer = nullptr) override;
	virtual std::vector<uint8> UpdatePinValue(uint8* customContainer = nullptr) override;

};

class MZObjectProperty : public MZProperty
{
public:
	MZObjectProperty(UObject* container, FObjectProperty* uproperty, FString parentCategory = FString(), uint8* StructPtr = nullptr, MZStructProperty* parentProperty = nullptr);
	

	FObjectProperty* objectprop;
	virtual void SetPropValue_Internal(void* val, size_t size, uint8* customContainer = nullptr) override;
	virtual std::vector<uint8> UpdatePinValue(uint8* customContainer = nullptr) override;

};

class MZStructProperty : public MZProperty
{
public:
	MZStructProperty(UObject* container, FStructProperty* uproperty, FString parentCategory = FString(), uint8* StructPtr = nullptr, MZStructProperty* parentProperty = nullptr);

	FStructProperty* structprop;
	virtual void SetPropValue_Internal(void* val, size_t size, uint8* customContainer = nullptr) override;
	virtual std::vector<uint8> UpdatePinValue(uint8* customContainer = nullptr) override { return std::vector<uint8>(); }
};

template<typename T, mz::tmp::StrLiteral LitType>
class MZCustomStructProperty : public MZProperty 
{
public:
	MZCustomStructProperty(UObject* container, FStructProperty* uproperty, FString parentCategory = FString(), uint8* StructPtr = nullptr, MZStructProperty* parentProperty = nullptr)
		: MZProperty(container, uproperty, parentCategory, StructPtr, parentProperty), structprop(uproperty)
	{
		data = std::vector<uint8_t>(sizeof(T), 0);
		TypeName = LitType.val;
	}

	FStructProperty* structprop;
protected:
	virtual void SetProperty_InCont(void* container, void* val) override 
	{
		structprop->CopyCompleteValue(structprop->ContainerPtrToValuePtr<void>(container), (T*)val);
	}
};

using MZVec2Property = MZCustomStructProperty<FVector2D, "mz.fb.vec2d">;
using MZVec3Property = MZCustomStructProperty<FVector, "mz.fb.vec3d">;
using MZVec4Property = MZCustomStructProperty < FVector4, "mz.fb.vec4d">;
using MZVec4FProperty = MZCustomStructProperty < FVector4f, "mz.fb.vec4">;

class MZRotatorProperty : public MZProperty
{
public:
	MZRotatorProperty(UObject* container, FStructProperty* uproperty, FString parentCategory = FString(), uint8* StructPtr = nullptr, MZStructProperty* parentProperty = nullptr)
		: MZProperty(container, uproperty, parentCategory, StructPtr, parentProperty), structprop(uproperty)
	{
		data = std::vector<uint8_t>(sizeof(FVector), 0);
		TypeName = "mz.fb.vec3d";
	}
	virtual std::vector<uint8> UpdatePinValue(uint8* customContainer = nullptr) override;

	FStructProperty* structprop;
protected:
	virtual void SetProperty_InCont(void* container, void* val) override;
};



class MZTrackProperty : public MZProperty
{
public:
	MZTrackProperty(UObject* container, FStructProperty* uproperty, FString parentCategory = FString(), uint8* StructPtr = nullptr, MZStructProperty* parentProperty = nullptr)
		: MZProperty(container, uproperty, parentCategory, StructPtr, parentProperty), structprop(uproperty)
	{
		
		data = std::vector<uint8_t>(1, 0);
		TypeName = "mz.fb.Track";
	}
	virtual std::vector<uint8> UpdatePinValue(uint8* customContainer = nullptr) override;

	//virtual flatbuffers::Offset<mz::fb::Pin> Serialize(flatbuffers::FlatBufferBuilder& fbb) override;
	virtual void SetPropValue_Internal(void* val, size_t size, uint8* customContainer = nullptr) override;

	FStructProperty* structprop;
protected:
	virtual void SetProperty_InCont(void* container, void* val) override;
};

class  MZSCENETREEMANAGER_API  MZPropertyFactory
{
public:
	static TSharedPtr<MZProperty> CreateProperty(UObject* Container, 
		FProperty* UProperty, 
		TMap<FGuid, TSharedPtr<MZProperty>>* RegisteredProperties = nullptr,
		TMap<FProperty*, TSharedPtr<MZProperty>>* PropertiesMap = nullptr,
		FString ParentCategory = FString(), 
		uint8* StructPtr = nullptr, 
		MZStructProperty* ParentProperty = nullptr);
};
