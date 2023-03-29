#pragma once
#include "CoreMinimal.h"
#include <vector>
#pragma warning (disable : 4800)
#pragma warning (disable : 4668)
#include "AppEvents_generated.h"
#include "RealityTrack.h"
#include "MZClient.h"
/*
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

	virtual void SetPropValue(void* val, size_t size, uint8* customContainer = nullptr);
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
	FString UIMaxString;
	FString UIMinString;
	bool IsAdvanced = false;
	bool ReadOnly = false;
	std::string TypeName;
	FGuid Id;
	std::vector<uint8_t> data; //wrt mediaZ standarts
	std::vector<uint8_t> default_val; //wrt mediaZ standarts
	std::vector<uint8_t> min_val; //wrt mediaZ standarts
	std::vector<uint8_t> max_val; //wrt mediaZ standarts
	mz::fb::ShowAs PinShowAs = mz::fb::ShowAs::PROPERTY;
	std::vector<TSharedPtr<MZProperty>> childProperties;
	TMap<FString, FString> mzMetaDataMap;
	bool transient = true;
	bool IsChanged = false;

	virtual ~MZProperty() {}
protected:
	virtual void SetProperty_InCont(void* container, void* val);

};

class MZBoolProperty : public MZProperty
{
public:
	MZBoolProperty(UObject* container, FBoolProperty* uproperty, FString parentCategory = FString(), uint8* StructPtr = nullptr, MZStructProperty* parentProperty = nullptr)
		: MZProperty(container, uproperty, parentCategory, StructPtr, parentProperty), boolprop(uproperty) 
	{
		data = std::vector<uint8_t>(1, 0);
		TypeName = "bool";
	}

	FBoolProperty* boolprop;
	virtual std::vector<uint8> UpdatePinValue(uint8* customContainer = nullptr) override;

protected:
	virtual void SetProperty_InCont(void* container, void* val) override;

};

class MZFloatProperty : public MZProperty
{
public:
	MZFloatProperty(UObject* container, FFloatProperty* uproperty, FString parentCategory = FString(), uint8* StructPtr = nullptr, MZStructProperty* parentProperty = nullptr)
		: MZProperty(container, uproperty, parentCategory, StructPtr, parentProperty), floatprop(uproperty) 
	{
		data = std::vector<uint8_t>(4, 0);
		TypeName = "float";
	}
	FFloatProperty* floatprop;

protected:
	virtual void SetProperty_InCont(void* container, void* val) override;
	
};

class MZDoubleProperty : public MZProperty
{
public:
	MZDoubleProperty(UObject* container, FDoubleProperty* uproperty, FString parentCategory = FString(), uint8* StructPtr = nullptr, MZStructProperty* parentProperty = nullptr)
		: MZProperty(container, uproperty, parentCategory, StructPtr, parentProperty), doubleprop(uproperty) 
	{
		data = std::vector<uint8_t>(8, 0);
		TypeName = "double";
	}
	FDoubleProperty* doubleprop;

protected:
	virtual void SetProperty_InCont(void* container, void* val) override;
};

class MZInt8Property : public MZProperty
{
public:
	MZInt8Property(UObject* container, FInt8Property* uproperty, FString parentCategory = FString(), uint8* StructPtr = nullptr, MZStructProperty* parentProperty = nullptr)
		: MZProperty(container, uproperty, parentCategory, StructPtr, parentProperty), int8prop(uproperty) 
	{
		data = std::vector<uint8_t>(1, 0);
		TypeName = "byte";
	}

	FInt8Property* int8prop;

protected:
	virtual void SetProperty_InCont(void* container, void* val) override;
};

class MZInt16Property : public MZProperty
{
public:
	MZInt16Property(UObject* container, FInt16Property* uproperty, FString parentCategory = FString(), uint8* StructPtr = nullptr, MZStructProperty* parentProperty = nullptr)
		: MZProperty(container, uproperty, parentCategory, StructPtr, parentProperty), int16prop(uproperty) 
	{
		data = std::vector<uint8_t>(2, 0);
		TypeName = "short";
	}

	FInt16Property* int16prop;

protected:
	virtual void SetProperty_InCont(void* container, void* val) override;
};

class MZIntProperty : public MZProperty
{
public:
	MZIntProperty(UObject* container, FIntProperty* uproperty, FString parentCategory = FString(), uint8* StructPtr = nullptr, MZStructProperty* parentProperty = nullptr)
		: MZProperty(container, uproperty, parentCategory, StructPtr, parentProperty), intprop(uproperty) 
	{
		data = std::vector<uint8_t>(4, 0);
		TypeName = "int";
	}

	FIntProperty* intprop;

protected:
	virtual void SetProperty_InCont(void* container, void* val) override;
};

class MZInt64Property : public MZProperty
{
public:
	MZInt64Property(UObject* container, FInt64Property* uproperty, FString parentCategory = FString(), uint8* StructPtr = nullptr, MZStructProperty* parentProperty = nullptr)
		: MZProperty(container, uproperty, parentCategory, StructPtr, parentProperty), int64prop(uproperty) 
	{
		data = std::vector<uint8_t>(8, 0);
		TypeName = "long";
	}

	FInt64Property* int64prop;

protected:
	virtual void SetProperty_InCont(void* container, void* val) override;
};

class MZByteProperty : public MZProperty
{
public:
	MZByteProperty(UObject* container, FByteProperty* uproperty, FString parentCategory = FString(), uint8* StructPtr = nullptr, MZStructProperty* parentProperty = nullptr)
		: MZProperty(container, uproperty, parentCategory, StructPtr, parentProperty), byteprop(uproperty) 
	{
		data = std::vector<uint8_t>(1, 0);
		TypeName = "ubyte";
	}

	FByteProperty* byteprop;

protected:
	virtual void SetProperty_InCont(void* container, void* val) override;
};

class MZUInt16Property : public MZProperty
{
public:
	MZUInt16Property(UObject* container, FUInt16Property* uproperty, FString parentCategory = FString(), uint8* StructPtr = nullptr, MZStructProperty* parentProperty = nullptr)
		: MZProperty(container, uproperty, parentCategory, StructPtr, parentProperty), uint16prop(uproperty) 
	{
		data = std::vector<uint8_t>(2, 0);
		TypeName = "ushort";
	}

	FUInt16Property* uint16prop;

protected:
	virtual void SetProperty_InCont(void* container, void* val) override;
};

class MZUInt32Property : public MZProperty
{
public:
	MZUInt32Property(UObject* container, FUInt32Property* uproperty, FString parentCategory = FString(), uint8* StructPtr = nullptr, MZStructProperty* parentProperty = nullptr)
		: MZProperty(container, uproperty, parentCategory, StructPtr, parentProperty), uint32prop(uproperty) 
	{
		data = std::vector<uint8_t>(4, 0);
		TypeName = "uint";
	}

	FUInt32Property* uint32prop;

protected:
	virtual void SetProperty_InCont(void* container, void* val) override;
};

class MZUInt64Property : public MZProperty
{
public:
	MZUInt64Property(UObject* container, FUInt64Property* uproperty, FString parentCategory = FString(), uint8* StructPtr = nullptr, MZStructProperty* parentProperty = nullptr)
		: MZProperty(container, uproperty, parentCategory, StructPtr, parentProperty), uint64prop(uproperty) 
	{
		data = std::vector<uint8_t>(8, 0);
		TypeName = "ulong";
	}

	FUInt64Property* uint64prop;

protected:
	virtual void SetProperty_InCont(void* container, void* val) override;
};

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
		std::vector<mz::fb::String256> NameList;
		for (auto [name, _]: NameMap)
		{
			mz::fb::String256 str256;
			auto val = str256.mutable_val();
			auto size = name.Len() < 256 ? name.Len() : 256;
			memcpy(val->data(), TCHAR_TO_UTF8(*name), size);
			NameList.push_back(str256);
		}
		mz::fb::String256 listName;
		strcat((char*)listName.mutable_val()->data(), TCHAR_TO_UTF8(*Enum->GetFName().ToString()));
		auto offset = mz::app::CreateUpdateStringList(mb, mz::fb::CreateString256ListDirect(mb, &listName, &NameList));
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
	virtual void SetPropValue(void* val, size_t size, uint8* customContainer = nullptr) override;
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
	virtual void SetPropValue(void* val, size_t size, uint8* customContainer = nullptr) override;
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
	virtual void SetPropValue(void* val, size_t size, uint8* customContainer = nullptr) override;
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
	virtual void SetPropValue(void* val, size_t size, uint8* customContainer = nullptr) override;
	virtual std::vector<uint8> UpdatePinValue(uint8* customContainer = nullptr) override;

};

class MZObjectProperty : public MZProperty
{
public:
	MZObjectProperty(UObject* container, FObjectProperty* uproperty, FString parentCategory = FString(), uint8* StructPtr = nullptr, MZStructProperty* parentProperty = nullptr);
	

	FObjectProperty* objectprop;
	virtual void SetPropValue(void* val, size_t size, uint8* customContainer = nullptr) override;
	virtual std::vector<uint8> UpdatePinValue(uint8* customContainer = nullptr) override { return std::vector<uint8>(); }

};

class MZStructProperty : public MZProperty
{
public:
	MZStructProperty(UObject* container, FStructProperty* uproperty, FString parentCategory = FString(), uint8* StructPtr = nullptr, MZStructProperty* parentProperty = nullptr);

	FStructProperty* structprop;
	virtual void SetPropValue(void* val, size_t size, uint8* customContainer = nullptr) override;
	virtual std::vector<uint8> UpdatePinValue(uint8* customContainer = nullptr) override { return std::vector<uint8>(); }
};

class MZVec2Property : public MZProperty
{
public:
	MZVec2Property(UObject* container, FStructProperty* uproperty, FString parentCategory = FString(), uint8* StructPtr = nullptr, MZStructProperty* parentProperty = nullptr)
		: MZProperty(container, uproperty, parentCategory, StructPtr, parentProperty), structprop(uproperty)
	{
		data = std::vector<uint8_t>(sizeof(FVector2D), 0);
		TypeName = "mz.fb.vec2d";
	}

	FStructProperty* structprop;
protected:
	virtual void SetProperty_InCont(void* container, void* val) override;
};

class MZVec3Property : public MZProperty
{
public:
	MZVec3Property(UObject* container, FStructProperty* uproperty, FString parentCategory = FString(), uint8* StructPtr = nullptr, MZStructProperty* parentProperty = nullptr)
		: MZProperty(container, uproperty, parentCategory, StructPtr, parentProperty), structprop(uproperty)
	{
		data = std::vector<uint8_t>(sizeof(FVector), 0);
		TypeName = "mz.fb.vec3d";
	}

	FStructProperty* structprop;
protected:
	virtual void SetProperty_InCont(void* container, void* val) override;
};

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

class MZVec4Property : public MZProperty
{
public:
	MZVec4Property(UObject* container, FStructProperty* uproperty, FString parentCategory = FString(), uint8* StructPtr = nullptr, MZStructProperty* parentProperty = nullptr)
		: MZProperty(container, uproperty, parentCategory, StructPtr, parentProperty), structprop(uproperty)
	{
		data = std::vector<uint8_t>(sizeof(FVector4), 0);
		TypeName = "mz.fb.vec4d";
	}

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
	virtual void SetPropValue(void* val, size_t size, uint8* customContainer = nullptr) override;

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
*/