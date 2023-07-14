/*
 * Copyright MediaZ AS. All Rights Reserved.
 */

#pragma once
#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "MediaZ/AppAPI.h"
#include "AppEvents_generated.h"
#include <mzFlatBuffersCommon.h>
#include "MZSceneTree.h"
#include "MZClient.h"
#include "MZViewportClient.h"
#include "MZAssetManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMZSceneTreeManager, Log, All);

struct MZPortal
{
	FGuid Id;
	FGuid SourceId;

	FString DisplayName;
	FString TypeName;
	FString CategoryName;
	mz::fb::ShowAs ShowAs;
};
//This class holds the list of all properties and pins 
class MZSCENETREEMANAGER_API FMZPropertyManager
{
public:
	FMZPropertyManager();

	TSharedPtr<MZProperty> CreateProperty(UObject* container,
		FProperty* uproperty,
		FString parentCategory = FString(""));

	void SetPropertyValue();
	void CreatePortal(FGuid PropertyId, mz::fb::ShowAs ShowAs);
	void CreatePortal(FProperty* uproperty, UObject* Container, mz::fb::ShowAs ShowAs);
	void ActorDeleted(FGuid DeletedActorId);
	flatbuffers::Offset<mz::fb::Pin> SerializePortal(flatbuffers::FlatBufferBuilder& fbb, MZPortal Portal, MZProperty* SourceProperty);
	
	FMZClient* MZClient;

	TMap<FGuid, TSharedPtr<MZProperty>> customProperties;
	TMap<FGuid, FGuid> PropertyToPortalPin;
	TMap<FGuid, MZPortal> PortalPinsById;
	TMap<FGuid, TSharedPtr<MZProperty>> PropertiesById;
	TMap<FProperty*, TSharedPtr<MZProperty>> PropertiesByPointer;
	TMap<FGuid, TSet<FGuid>> ActorsPropertyIds; //actor guid x actor mzproperties guid

	TMap<TPair<FProperty*, void*>, TSharedPtr<MZProperty>> PropertiesByPropertyAndContainer;
	void Reset(bool ResetPortals = true);

	void OnBeginFrame();
	void OnEndFrame();
};

class MZSCENETREEMANAGER_API FMZActorManager
{
public:
	FMZActorManager(MZSceneTree& SceneTree) : SceneTree(SceneTree)
	{
		MZAssetManager = &FModuleManager::LoadModuleChecked<FMZAssetManager>("MZAssetManager");
		MZClient = &FModuleManager::LoadModuleChecked<FMZClient>("MZClient");
		RegisterDelegates();
	};

	AActor* GetParentTransformActor();
	AActor* SpawnActor(FString SpawnTag);
	AActor* SpawnUMGRenderManager(FString umgTag,UUserWidget* widget);
	AActor* SpawnActor(UClass* ClassToSpawn);
	void ClearActors();
	
	void ReAddActorsToSceneTree();

	void RegisterDelegates();
	void PreSave(UWorld* World, FObjectPreSaveContext Context);
	void PostSave(UWorld* World, FObjectPostSaveContext Context);

	MZActorReference ParentTransformActor;

	MZSceneTree& SceneTree;
	class FMZAssetManager* MZAssetManager;
	class FMZClient* MZClient;
	
	TSet<FGuid> ActorIds;
	TArray< TPair<MZActorReference,TMap<FString,FString>> > Actors;
};


class ContextMenuActions
{
public:
	TArray<TPair<FString, std::function<void(AActor*)>>>  ActorMenu;
	TArray<TPair<FString, Task>>  FunctionMenu;
	TArray<TPair<FString, std::function<void(class FMZSceneTreeManager*, FGuid)>>>   PortalPropertyMenu;
	ContextMenuActions();
	std::vector<flatbuffers::Offset<mz::ContextMenuItem>> SerializeActorMenuItems(flatbuffers::FlatBufferBuilder& fbb);
	std::vector<flatbuffers::Offset<mz::ContextMenuItem>> SerializePortalPropertyMenuItems(flatbuffers::FlatBufferBuilder& fbb);
	void ExecuteActorAction(uint32 command, AActor* actor);
	void ExecutePortalPropertyAction(uint32 command, class FMZSceneTreeManager* MZSceneTreeManager, FGuid PortalId);
};


class MZSCENETREEMANAGER_API FMZSceneTreeManager : public IModuleInterface {

public:
	//Empty constructor
	FMZSceneTreeManager();

	//Called on startup of the module on Unreal Engine start
	virtual void StartupModule() override;

	//Called on shutdown of the module on Unreal Engine exit
	virtual void ShutdownModule() override;

	bool Tick(float dt);

	void OnBeginFrame();
	void OnEndFrame();

	void OnMZConnected(mz::fb::Node const& appNode);

	void OnMZNodeUpdated(mz::fb::Node const& appNode);

	//every function of this class runs in game thread
	void OnMZNodeSelected(mz::fb::UUID const& nodeId);

	//called when connection is ended with mediaz
	void OnMZConnectionClosed();

	//called when a pin value changed from mediaz
	void OnMZPinValueChanged(mz::fb::UUID const& pinId, uint8_t const* data, size_t size, bool reset);

	//called when a pins show as changed
	void OnMZPinShowAsChanged(mz::fb::UUID const& pinId, mz::fb::ShowAs newShowAs);

	//called when a function is called from mediaz
	void OnMZFunctionCalled(mz::fb::UUID const& nodeId, mz::fb::Node const& function);

	//called when the app(unreal engine) is executed from mediaz
	void OnMZExecutedApp(mz::app::AppExecute const& appExecute);

	//called when a context menu is requested on some node on mediaz
	void OnMZContextMenuRequested(mz::ContextMenuRequest const& request);

	//called when a action is selected from context menu
	void OnMZContextMenuCommandFired(mz::ContextMenuAction const& action);

	void OnMZNodeRemoved();

	void OnMZStateChanged(mz::app::ExecutionState);
	//END OF MediaZ DELEGATES
	 
	void PopulateAllChilds(AActor* actor);

	void PopulateAllChilds(FGuid ActorId);

	void PopulateAllChildsOfSceneComponentNode(SceneComponentNode* SceneComponentNode);

	void SendSyncSemaphores(bool RenewSemaphores);
	
	//Called when the level is initiated
	void OnPostWorldInit(UWorld* World, const UWorld::InitializationValues InitValues);

	//Called when the level destruction began
	void OnPreWorldFinishDestroy(UWorld* World);

	//delegate called when a property is changed from unreal engine editor
	//it updates thecorresponding property in mediaz
	void OnPropertyChanged(UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent);

	//Called when an actor is spawned into the world
	void OnActorSpawned(AActor* InActor);

	//Called when an actor is destroyed from the world
	void OnActorDestroyed(AActor* InActor);

	//called when unreal engine node is imported from mediaZ
	void OnMZNodeImported(mz::fb::Node const& appNode);

	//Set a properties value
	void SetPropertyValue(FGuid pinId, void* newval, size_t size);

#ifdef VIEWPORT_TEXTURE
	//Set viewport texture pin's container to current viewport client's texture on play
	void ConnectViewportTexture();

	//Set viewport texture pin's container to null
	void DisconnectViewportTexture();
#endif

	//Rescans the current viewports world scene to get the current state of the scene outliner
	void RescanScene(bool reset = true);

	//Populates node with child actors/components, functions and properties
	bool PopulateNode(FGuid id);

	//Sends node updates to the MediaZ
	void SendNodeUpdate(FGuid NodeId, bool bResetRootPins = true);

	void SendEngineFunctionUpdate();

	//Sends pin value changed event to MediaZ
	void SendPinValueChanged(FGuid propertyId, std::vector<uint8> data);

	//Sends pin updates to the root node 
	void SendPinUpdate();
	
	void RemovePortal(FGuid PortalId);
	
	//Sends pin to add to a node
	void SendPinAdded(FGuid NodeId, TSharedPtr<MZProperty> const& mzprop);

	//Adds the node to scene tree and sends it to mediaZ
	void SendActorAdded(AActor* actor, FString spawnTag = FString());

	//Deletes the node from scene tree and sends it to mediaZ
	void SendActorDeleted(FGuid Id, TSet<UObject*>& RemovedObjects);

	//Called when pie is started
	void HandleBeginPIE(bool bIsSimulating);

	//Called when pie is ending
	void HandleEndPIE(bool bIsSimulating);

	void HandleWorldChange();

	UObject* FindContainer(FGuid ActorId, FString ComponentName);

	void* FindContainerFromContainerPath(UObject* BaseContainer, FString ContainerPath);

	// UObject* FMZSceneTreeManager::FindObjectContainerFromContainerPath(UObject* BaseContainer, FString ContainerPath);
	//Remove properties of tree node from registered properties and pins
	void RemoveProperties(TSharedPtr<TreeNode> Node,
		TSet<TSharedPtr<MZProperty>>& PropertiesToRemove);

	void CheckPins(TSet<UObject*>& RemovedObjects,
		TSet<TSharedPtr<MZProperty>>& PinsToRemove,
		TSet<TSharedPtr<MZProperty>>& PropertiesToRemove);

	void Reset();

	void OnMapChange(uint32 MapFlags);
	void OnNewCurrentLevel();

	void AddCustomFunction(MZCustomFunction* CustomFunction);
	
	//the world we interested in
	static UWorld* daWorld;

	//all the properties registered 
	TMap<FGuid, TSharedPtr<MZProperty>> RegisteredProperties;

	//all the properties registered mapped with property pointers
	TMap<FProperty*, TSharedPtr<MZProperty>> PropertiesMap;

	//all the functions registered
	TMap<FGuid, TSharedPtr<MZFunction>> RegisteredFunctions;

	//in/out pins of the mediaz node
	TMap<FGuid, TSharedPtr<MZProperty>> Pins;

	//custom properties like viewport texture
	TMap<FGuid, TSharedPtr<MZProperty>> CustomProperties;

#ifdef VIEWPORT_TEXTURE
	MZProperty* ViewportTextureProperty;
#endif

	//custom functions like spawn actor
	TMap<FGuid, MZCustomFunction*> CustomFunctions;

	//handles context menus and their actions
	friend class ContextMenuActions;
	class ContextMenuActions menuActions;

	//Scene tree holds the information to mimic the outliner in mediaz
	class MZSceneTree SceneTree;
	
	//Class communicates with MediaZ
	class FMZClient* MZClient;

	class FMZAssetManager* MZAssetManager;

	class FMZViewportManager* MZViewportManager;
	
	FMZActorManager* MZActorManager;

	FMZPropertyManager MZPropertyManager;

	bool bIsModuleFunctional = false;

	mz::app::ExecutionState ExecutionState = mz::app::ExecutionState::IDLE;

	bool ToggleExecutionState = false;
};

