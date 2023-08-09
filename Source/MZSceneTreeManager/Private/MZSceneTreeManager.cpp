// Copyright MediaZ AS. All Rights Reserved.

//mediaz plugin includes
#include "MZSceneTreeManager.h"
#include "MZClient.h"
#include "MZTextureShareManager.h"
#include "MZAssetManager.h"
#include "MZViewportManager.h"

//unreal engine includes
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet2/ComponentEditorUtils.h"
#include "Kismet/GameplayStatics.h"
#include "EditorCategoryUtils.h"
#include "ObjectEditorUtils.h"
#include "HardwareInfo.h"
#include "LevelSequence.h"
#include "PacketHandler.h"

DEFINE_LOG_CATEGORY(LogMZSceneTreeManager);
#define LOG(x) UE_LOG(LogMZSceneTreeManager, Display, TEXT(x))
#define LOGF(x, y) UE_LOG(LogMZSceneTreeManager, Display, TEXT(x), y)


IMPLEMENT_MODULE(FMZSceneTreeManager, MZSceneTreeManager)

UWorld* FMZSceneTreeManager::daWorld = nullptr;

#define MZ_POPULATE_UNREAL_FUNCTIONS //uncomment if you want to see functions 

TMap<FGuid, std::vector<uint8>> ParsePins(mz::fb::Node const& archive)
{
	TMap<FGuid, std::vector<uint8>> re;
	if (!flatbuffers::IsFieldPresent(&archive, mz::fb::Node::VT_PINS))
	{
		return re;
	}

	for (auto pin : *archive.pins())
	{
		if(!pin->data() || pin->data()->size() <= 0 )
		{
			continue;
		}
		std::vector<uint8> data(pin->data()->size(), 0);
		memcpy(data.data(), pin->data()->data(), data.size());
		re.Add(*(FGuid*)pin->id()->bytes()->Data(), data);
	}
	return re;
}

TMap<FGuid, const mz::fb::Pin*> ParsePins(const mz::fb::Node* archive)
{
	TMap<FGuid, const mz::fb::Pin*> re;
	if (!flatbuffers::IsFieldPresent(archive, mz::fb::Node::VT_PINS))
	{
		return re;
	}
	for (auto pin : *(archive->pins()))
	{
		re.Add(*(FGuid*)pin->id()->bytes()->Data(), pin);
	}
	return re;
}


FMZSceneTreeManager::FMZSceneTreeManager()
{

}
void FMZSceneTreeManager::OnMapChange(uint32 MapFlags)
{
	FString WorldName = GEditor->GetEditorWorldContext().World()->GetMapName();
	LOGF("OnMapChange with editor world contexts world %s", *WorldName);
	FMZSceneTreeManager::daWorld = GEditor ? GEditor->GetEditorWorldContext().World() : GEngine->GetCurrentPlayWorld();
}

void FMZSceneTreeManager::OnNewCurrentLevel()
{
	FString WorldName = GEditor->GetEditorWorldContext().World()->GetMapName();
	LOGF("OnNewCurrentLevel with editor world contexts world %s", *WorldName);
	//todo we may need to fill these according to the level system
}

void FMZSceneTreeManager::AddCustomFunction(MZCustomFunction* CustomFunction)
{
	CustomFunctions.Add(CustomFunction->Id, CustomFunction);
	SendEngineFunctionUpdate();
}

void FMZSceneTreeManager::OnBeginFrame()
{
	if(ToggleExecutionState)
	{
		ToggleExecutionState = false;
		ExecutionState = ExecutionState == mz::app::ExecutionState::IDLE ? mz::app::ExecutionState::SYNCED : mz::app::ExecutionState::IDLE;
		bool SemaphoresRenewed = false;
		MZTextureShareManager::GetInstance()->ExecutionStateChanged(ExecutionState, SemaphoresRenewed);
		if(SemaphoresRenewed)
		{
			SendSyncSemaphores(false);
		}
	}
	
	MZPropertyManager.OnBeginFrame();
	MZTextureShareManager::GetInstance()->OnBeginFrame();
}

void FMZSceneTreeManager::OnEndFrame()
{
	 MZPropertyManager.OnEndFrame();
	MZTextureShareManager::GetInstance()->OnEndFrame();
}

void FMZSceneTreeManager::StartupModule()
{
	if (!FApp::HasProjectName())
	{
		return;
	}

	auto hwinfo = FHardwareInfo::GetHardwareInfo(NAME_RHI);
	if ("D3D12" != hwinfo)
	{
		FMessageDialog::Debugf(FText::FromString("MediaZ plugin supports DirectX12 only!"), 0);
		return;
	}
	
	bIsModuleFunctional = true; 

	MZClient = &FModuleManager::LoadModuleChecked<FMZClient>("MZClient");
	MZAssetManager = &FModuleManager::LoadModuleChecked<FMZAssetManager>("MZAssetManager");
	MZViewportManager = &FModuleManager::LoadModuleChecked<FMZViewportManager>("MZViewportManager");

	MZPropertyManager.MZClient = MZClient;

	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FMZSceneTreeManager::Tick));
	MZActorManager = new FMZActorManager(SceneTree);
	//Bind to MediaZ events
	MZClient->OnMZNodeSelected.AddRaw(this, &FMZSceneTreeManager::OnMZNodeSelected);
	MZClient->OnMZConnected.AddRaw(this, &FMZSceneTreeManager::OnMZConnected);
	MZClient->OnMZNodeUpdated.AddRaw(this, &FMZSceneTreeManager::OnMZNodeUpdated);
	MZClient->OnMZConnectionClosed.AddRaw(this, &FMZSceneTreeManager::OnMZConnectionClosed);
	MZClient->OnMZPinValueChanged.AddRaw(this, &FMZSceneTreeManager::OnMZPinValueChanged);
	MZClient->OnMZPinShowAsChanged.AddRaw(this, &FMZSceneTreeManager::OnMZPinShowAsChanged);
	MZClient->OnMZFunctionCalled.AddRaw(this, &FMZSceneTreeManager::OnMZFunctionCalled);
	MZClient->OnMZExecutedApp.AddRaw(this, &FMZSceneTreeManager::OnMZExecutedApp);
	MZClient->OnMZContextMenuRequested.AddRaw(this, &FMZSceneTreeManager::OnMZContextMenuRequested);
	MZClient->OnMZContextMenuCommandFired.AddRaw(this, &FMZSceneTreeManager::OnMZContextMenuCommandFired);
	MZClient->OnMZNodeImported.AddRaw(this, &FMZSceneTreeManager::OnMZNodeImported);
	MZClient->OnMZNodeRemoved.AddRaw(this, &FMZSceneTreeManager::OnMZNodeRemoved);
	MZClient->OnMZStateChanged.AddRaw(this, &FMZSceneTreeManager::OnMZStateChanged);

	FCoreDelegates::OnBeginFrame.AddRaw(this, &FMZSceneTreeManager::OnBeginFrame);
	FCoreDelegates::OnEndFrame.AddRaw(this, &FMZSceneTreeManager::OnEndFrame);

	
	FEditorDelegates::PostPIEStarted.AddRaw(this, &FMZSceneTreeManager::HandleBeginPIE);
	FEditorDelegates::EndPIE.AddRaw(this, &FMZSceneTreeManager::HandleEndPIE);
	FEditorDelegates::NewCurrentLevel.AddRaw(this, &FMZSceneTreeManager::OnNewCurrentLevel);
	FEditorDelegates::MapChange.AddRaw(this, &FMZSceneTreeManager::OnMapChange);

	FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &FMZSceneTreeManager::OnPropertyChanged);

	FWorldDelegates::OnPostWorldInitialization.AddRaw(this, &FMZSceneTreeManager::OnPostWorldInit);
	FWorldDelegates::OnPreWorldFinishDestroy.AddRaw(this, &FMZSceneTreeManager::OnPreWorldFinishDestroy);
	
#ifdef VIEWPORT_TEXTURE
	UMZViewportClient::MZViewportDestroyedDelegate.AddRaw(this, &FMZSceneTreeManager::DisconnectViewportTexture);
	//custom pins
	{
		auto mzprop = MZPropertyManager.CreateProperty(nullptr, UMZViewportClient::StaticClass()->FindPropertyByName("ViewportTexture"));
		if(mzprop) {
			MZPropertyManager.CreatePortal(mzprop->Id, mz::fb::ShowAs::INPUT_PIN);
			mzprop->PinShowAs = mz::fb::ShowAs::INPUT_PIN;
			CustomProperties.Add(mzprop->Id, mzprop);
			ViewportTextureProperty = mzprop.Get();
		}
	}
#endif

	//custom functions 
	{
		MZCustomFunction* mzcf = new MZCustomFunction;
		mzcf->Id = FGuid::NewGuid();
		FGuid actorPinId = FGuid::NewGuid();
		mzcf->Params.Add(actorPinId, "Spawn Actor");
		mzcf->Serialize = [funcid = mzcf->Id, actorPinId](flatbuffers::FlatBufferBuilder& fbb)->flatbuffers::Offset<mz::fb::Node>
		{
			std::string empty = "None";
			auto data = std::vector<uint8_t>(empty.begin(), empty.end());
			data.push_back(0);
			std::vector<flatbuffers::Offset<mz::fb::Pin>> spawnPins = {
				mz::fb::CreatePinDirect(fbb, (mz::fb::UUID*)&actorPinId, TCHAR_TO_ANSI(TEXT("Actor List")), TCHAR_TO_ANSI(TEXT("string")), mz::fb::ShowAs::PROPERTY, mz::fb::CanShowAs::PROPERTY_ONLY, "UE PROPERTY", mz::fb::CreateVisualizerDirect(fbb, mz::fb::VisualizerType::COMBO_BOX, "UE5_ACTOR_LIST"), &data, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  mz::fb::PinContents::JobPin),
			};
			return mz::fb::CreateNodeDirect(fbb, (mz::fb::UUID*)&funcid, "Spawn Actor", "UE5.UE5", false, true, &spawnPins, 0, mz::fb::NodeContents::Job, mz::fb::CreateJob(fbb, mz::fb::JobType::CPU).Union(), TCHAR_TO_ANSI(*FMZClient::AppKey), 0, "Control");
		};
		mzcf->Function = [this, actorPinId](TMap<FGuid, std::vector<uint8>> properties)
		{
			FString SpawnTag((char*)properties.FindRef(actorPinId).data());
			if(SpawnTag.IsEmpty())
			{
				return;
			}
			AActor* SpawnedActor = MZActorManager->SpawnActor(SpawnTag);
			LOGF("Actor with tag %s is spawned", *SpawnTag);
		};
		CustomFunctions.Add(mzcf->Id, mzcf);
	}

	LOG("MZSceneTreeManager module successfully loaded.");
}

void FMZSceneTreeManager::ShutdownModule()
{
	LOG("MZSceneTreeManager module successfully shut down.");
}

bool FMZSceneTreeManager::Tick(float dt)
{
	//TODO check after merge
	//if (MZClient)
	//{
	//	MZTextureShareManager::GetInstance()->EnqueueCommands(MZClient->AppServiceClient);
	//}

	return true;
}

void FMZSceneTreeManager::OnMZConnected(mz::fb::Node const* appNode)
{
	if(!appNode)
	{
		return;
	}
		
	SceneTree.Root->Id = *(FGuid*)appNode->id();
	//add executable path
	if(appNode->pins() && appNode->pins()->size() > 0)
	{
		std::vector<flatbuffers::Offset<mz::PartialPinUpdate>> PinUpdates;
		flatbuffers::FlatBufferBuilder fb1;
		for (auto pin : *appNode->pins())
		{
			PinUpdates.push_back(mz::CreatePartialPinUpdate(fb1, pin->id(), 0, mz::fb::CreateOrphanStateDirect(fb1, true, "Binding in progress")));
		}
		auto offset = mz::CreatePartialNodeUpdateDirect(fb1, (mz::fb::UUID*)&FMZClient::NodeId, mz::ClearFlags::NONE, 0, 0, 0, 0, 0, 0, 0, &PinUpdates);
		fb1.Finish(offset);
		auto buf = fb1.Release();
		auto root = flatbuffers::GetRoot<mz::PartialNodeUpdate>(buf.data());
		MZClient->AppServiceClient->SendPartialNodeUpdate(*root);
	}
	RescanScene();
	SendNodeUpdate(FMZClient::NodeId, false);
	if((appNode->pins() && appNode->pins()->size() > 0 )|| (appNode->contents_as_Graph()->nodes() && appNode->contents_as_Graph()->nodes()->size() > 0))
	{
		LOG("Node import request recieved on connection");
		OnMZNodeImported(*appNode);
	}
	//else
		//SendSyncSemaphores(true);
}

void FMZSceneTreeManager::OnMZNodeUpdated(mz::fb::Node const& appNode)
{
	FString NodeName(appNode.name()->c_str());
	LOGF("On MZ Node updated for %s", *NodeName);
	
	if (FMZClient::NodeId != SceneTree.Root->Id)
	{
		SceneTree.Root->Id = *(FGuid*)appNode.id();
		RescanScene();
		SendNodeUpdate(FMZClient::NodeId);
		//SendSyncSemaphores(true);
	}
	auto texman = MZTextureShareManager::GetInstance();
	for (auto& [id, pin] : ParsePins(&appNode))
	{
		if (texman->PendingCopyQueue.Contains(id))
		{
			auto mzprop = texman->PendingCopyQueue.FindRef(id);
			auto ShowAs = mzprop->PinShowAs;
			if(MZPropertyManager.PropertyToPortalPin.Contains(mzprop->Id))
			{
				auto PortalId = MZPropertyManager.PropertyToPortalPin.FindRef(mzprop->Id); 
				if(MZPropertyManager.PortalPinsById.Contains(PortalId))
				{
					auto Portal = MZPropertyManager.PortalPinsById.FindRef(PortalId);
					ShowAs = Portal.ShowAs;
				}
			}
			texman->UpdatePinShowAs(mzprop, ShowAs);
		}
	}
}

void FMZSceneTreeManager::OnMZNodeSelected(mz::fb::UUID const& nodeId)
{
	FGuid id = *(FGuid*)&nodeId;
	if (PopulateNode(id))
	{
		SendNodeUpdate(id);
	}
}

bool IsActorDisplayable(const AActor* Actor)
{
	static const FName SequencerActorTag(TEXT("SequencerActor"));

	return Actor &&
		Actor->IsEditable() &&																	// Only show actors that are allowed to be selected and drawn in editor
		Actor->IsListedInSceneOutliner() &&
		(((Actor->GetWorld() && Actor->GetWorld()->IsPlayInEditor()) || !Actor->HasAnyFlags(RF_Transient)) ||
			(Actor->ActorHasTag(SequencerActorTag))) &&
		!Actor->IsTemplate() &&																	// Should never happen, but we never want CDOs displayed
		!Actor->IsA(AWorldSettings::StaticClass()) &&											// Don't show the WorldSettings actor, even though it is technically editable
		IsValidChecked(Actor);// &&																// We don't want to show actors that are about to go away
		//!Actor->IsHidden();
}

void FMZSceneTreeManager::OnMZConnectionClosed()
{
	MZActorManager->ClearActors();
	if(ExecutionState == mz::app::ExecutionState::SYNCED)
	{
		ExecutionState = mz::app::ExecutionState::IDLE;
		bool discard;
		MZTextureShareManager::GetInstance()->ExecutionStateChanged(ExecutionState, discard);
	}
}

void FMZSceneTreeManager::OnMZPinValueChanged(mz::fb::UUID const& pinId, uint8_t const* data, size_t size, bool reset)
{
	FGuid Id = *(FGuid*)&pinId;
	if (CustomProperties.Contains(Id))
	{
		auto mzprop = CustomProperties.FindRef(Id);
		std::vector<uint8_t> copy(size, 0);
		memcpy(copy.data(), data, size);

		mzprop->SetPropValue((void*)copy.data(), size);
		return;
	}
	SetPropertyValue(Id, (void*)data, size);
}

void FMZSceneTreeManager::OnMZPinShowAsChanged(mz::fb::UUID const& Id, mz::fb::ShowAs newShowAs)
{
	FGuid pinId = *(FGuid*)&Id;
	if (CustomProperties.Contains(pinId))
	{
		UE_LOG(LogTemp, Warning, TEXT("Custom Property ShowAs changed."));
	}
	else if (MZPropertyManager.PropertiesById.Contains(pinId) && !MZPropertyManager.PropertyToPortalPin.Contains(pinId))
	{
		MZPropertyManager.CreatePortal(pinId, newShowAs);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Property with given id is not found."));
	}
	if(MZPropertyManager.PropertyToPortalPin.Contains(pinId))
	{
		auto PortalId = MZPropertyManager.PropertyToPortalPin.FindRef(pinId);
		if(MZPropertyManager.PortalPinsById.Contains(PortalId))
		{
			auto Portal = MZPropertyManager.PortalPinsById.FindRef(PortalId);
			if(MZPropertyManager.PropertiesById.Contains(Portal.SourceId))
			{
				auto MzProperty = MZPropertyManager.PropertiesById.FindRef(Portal.SourceId);
				MZClient->AppServiceClient->SendPinShowAsChange(reinterpret_cast<mz::fb::UUID&>(PortalId), newShowAs);
				MZTextureShareManager::GetInstance()->UpdatePinShowAs(MzProperty.Get(), newShowAs);
			}
		}
	}
		
	if(MZPropertyManager.PortalPinsById.Contains(pinId))
	{
		auto Portal = MZPropertyManager.PortalPinsById.Find(pinId);
		Portal->ShowAs = newShowAs;
		if(MZPropertyManager.PropertiesById.Contains(Portal->SourceId))
		{
			auto MzProperty = MZPropertyManager.PropertiesById.FindRef(Portal->SourceId);
			flatbuffers::FlatBufferBuilder mb;
			auto offset = mz::CreateAppEventOffset(mb ,mz::CreatePinShowAsChanged(mb, (mz::fb::UUID*)&Portal->SourceId, newShowAs));
			mb.Finish(offset);
			auto buf = mb.Release();
			auto root = flatbuffers::GetRoot<mz::app::AppEvent>(buf.data());
			MZClient->AppServiceClient->Send(*root);
			MZTextureShareManager::GetInstance()->UpdatePinShowAs(MzProperty.Get(), newShowAs);
		}
	}
}

void FMZSceneTreeManager::OnMZFunctionCalled(mz::fb::UUID const& nodeId, mz::fb::Node const& function)
{
	FGuid funcId = *(FGuid*)function.id();
	TMap<FGuid, std::vector<uint8>> properties = ParsePins(function);

	if (CustomFunctions.Contains(funcId))
	{
		auto mzcf = CustomFunctions.FindRef(funcId);
		mzcf->Function(properties);
	}
	else if (RegisteredFunctions.Contains(funcId))
	{
		auto mzfunc = RegisteredFunctions.FindRef(funcId);
		uint8* Parms = (uint8*)FMemory_Alloca_Aligned(mzfunc->Function->ParmsSize, mzfunc->Function->GetMinAlignment());
		mzfunc->Parameters = Parms;
		FMemory::Memzero(Parms, mzfunc->Function->ParmsSize);

		for (TFieldIterator<FProperty> It(mzfunc->Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			FProperty* LocalProp = *It;
			checkSlow(LocalProp);
			if (!LocalProp->HasAnyPropertyFlags(CPF_ZeroConstructor))
			{
				LocalProp->InitializeValue_InContainer(Parms);
			}
		}

		for (auto [id, val] : properties)
		{
			if (MZPropertyManager.PropertiesById.Contains(id))
			{
				auto mzprop = MZPropertyManager.PropertiesById.FindRef(id);
				mzprop->SetPropValue((void*)val.data(), val.size(), Parms);
			}
		}

		mzfunc->Invoke();
		
		for (auto mzprop : mzfunc->OutProperties)
		{
			SendPinValueChanged(mzprop->Id, mzprop->UpdatePinValue(Parms));
		}
		mzfunc->Parameters = nullptr;
		LOG("Unreal Engine function executed.");
	}
}

void FMZSceneTreeManager::OnMZExecutedApp(mz::app::AppExecute const& appExecute)
{
	if (!flatbuffers::IsFieldPresent(&appExecute, mz::app::AppExecute::VT_LATEST_PIN_DATA))
	{
		return;
	}
	for (auto* update : *appExecute.latest_pin_data())
	{
		auto id = *(FGuid*)update->pin_id()->bytes()->Data();

		if (MZPropertyManager.PropertiesById.Contains(id))
		{
			auto mzprop = MZPropertyManager.PropertiesById.FindRef(id);
			mzprop->SetPropValue((void*)update->value()->data(), update->value()->size());
			if(mzprop->TypeName == "mz.fb.Texture")
			{
				auto texman = MZTextureShareManager::GetInstance();
				auto ShowAs = mzprop->PinShowAs;
				if(MZPropertyManager.PropertyToPortalPin.Contains(mzprop->Id))
				{
					auto PortalId = MZPropertyManager.PropertyToPortalPin.FindRef(mzprop->Id); 
					if(MZPropertyManager.PortalPinsById.Contains(PortalId))
					{
						auto Portal = MZPropertyManager.PortalPinsById.FindRef(PortalId);
						ShowAs = Portal.ShowAs;
					}
				}
				texman->UpdatePinShowAs(mzprop.Get(), ShowAs);
				
			}
		}
		
	}
}

void FMZSceneTreeManager::OnMZContextMenuRequested(mz::ContextMenuRequest const& request)
{
	FVector2D pos(request.pos()->x(), request.pos()->y());
	FGuid itemId = *(FGuid*)request.item_id();
	auto instigator = request.instigator();

	if (SceneTree.NodeMap.Contains(itemId))
	{
		if (auto actorNode = SceneTree.NodeMap.FindRef(itemId)->GetAsActorNode())
		{
			if (!MZClient->IsConnected())
			{
				return;
			}
			flatbuffers::FlatBufferBuilder mb;
			std::vector<flatbuffers::Offset<mz::ContextMenuItem>> actions = menuActions.SerializeActorMenuItems(mb);
			auto posx = mz::fb::vec2(pos.X, pos.Y);
			auto offset = mz::CreateContextMenuUpdateDirect(mb, (mz::fb::UUID*)&itemId, &posx, instigator, &actions);
			mb.Finish(offset);
			auto buf = mb.Release();
			auto root = flatbuffers::GetRoot<mz::ContextMenuUpdate>(buf.data());
			MZClient->AppServiceClient->SendContextMenuUpdate(*root);
		}
	}
	else if(MZPropertyManager.PortalPinsById.Contains(itemId))
	{
		auto MzProperty = MZPropertyManager.PortalPinsById.FindRef(itemId);
		
		flatbuffers::FlatBufferBuilder mb;
		std::vector<flatbuffers::Offset<mz::ContextMenuItem>> actions = menuActions.SerializePortalPropertyMenuItems(mb);
		auto posx = mz::fb::vec2(pos.X, pos.Y);
		auto offset = mz::CreateContextMenuUpdateDirect(mb, (mz::fb::UUID*)&itemId, &posx, instigator, &actions);
		mb.Finish(offset);
		auto buf = mb.Release();
		auto root = flatbuffers::GetRoot<mz::ContextMenuUpdate>(buf.data());
		MZClient->AppServiceClient->SendContextMenuUpdate(*root);
	}
}

void FMZSceneTreeManager::OnMZContextMenuCommandFired(mz::ContextMenuAction const& action)
{
	FGuid itemId = *(FGuid*)action.item_id();
	uint32 actionId = action.command();
	if (SceneTree.NodeMap.Contains(itemId))
	{
		if (auto actorNode = SceneTree.NodeMap.FindRef(itemId)->GetAsActorNode())
		{
			auto actor = actorNode->actor.Get();
			if (!actor)
			{
				return;
			}
			menuActions.ExecuteActorAction(actionId, actor);
		}
	}
	else if(MZPropertyManager.PortalPinsById.Contains(itemId))
	{
		menuActions.ExecutePortalPropertyAction(actionId, this, itemId);
	}
}

void FMZSceneTreeManager::OnMZNodeRemoved()
{
	MZActorManager->ClearActors();
}

void FMZSceneTreeManager::OnMZStateChanged(mz::app::ExecutionState newState)
{
	if(ExecutionState != newState)
	{
		ToggleExecutionState = true; 
	}
}

void FMZSceneTreeManager::OnPostWorldInit(UWorld* World, const UWorld::InitializationValues InitValues)
{
	auto WorldContext = GEngine->GetWorldContextFromGameViewport(GEngine->GameViewport);
	if (World != WorldContext->World())
	{
		return;
	}
	FOnActorSpawned::FDelegate ActorSpawnedDelegate = FOnActorSpawned::FDelegate::CreateRaw(this, &FMZSceneTreeManager::OnActorSpawned);
	FOnActorDestroyed::FDelegate ActorDestroyedDelegate = FOnActorDestroyed::FDelegate::CreateRaw(this, &FMZSceneTreeManager::OnActorDestroyed);
	World->AddOnActorSpawnedHandler(ActorSpawnedDelegate);
	World->AddOnActorDestroyedHandler(ActorDestroyedDelegate);
	FMZSceneTreeManager::daWorld = GEditor ? GEditor->GetEditorWorldContext().World() : GEngine->GetCurrentPlayWorld();
	RescanScene();
	SendNodeUpdate(FMZClient::NodeId);
}


void FMZSceneTreeManager::OnPreWorldFinishDestroy(UWorld* World)
{
	//TODO check if we actually need this function
	return;
#if 0
	SceneTree.Clear();
	RegisteredProperties = Pins;
	PropertiesMap.Empty();
	MZActorManager->ReAddActorsToSceneTree();
	RescanScene(false);
	SendNodeUpdate(FMZClient::NodeId, false);
#endif
}

struct PropUpdate
{
	FGuid actorId;
	FGuid pinId;
	FString displayName;
	FString componentName;
	FString PropertyPath;
	FString ContainerPath;
	void* newVal;
	size_t newValSize;
	void* defVal;
	size_t defValSize;
	mz::fb::ShowAs pinShowAs;
	bool IsPortal;
};

void GetNodesSpawnedByMediaz(const mz::fb::Node* node, TMap<FGuid, FString>& spawnedByMediaz)
{
	if (flatbuffers::IsFieldPresent(node, mz::fb::Node::VT_META_DATA_MAP))
	{
		if (auto entry = node->meta_data_map()->LookupByKey("spawnTag"))
		{
			spawnedByMediaz.Add(*(FGuid*)node->id(), FString(entry->value()->c_str()));
		}
	}
	if (flatbuffers::IsFieldPresent(node->contents_as_Graph(), mz::fb::Graph::VT_NODES))
	{
		for (auto child : *node->contents_as_Graph()->nodes())
		{
			GetNodesSpawnedByMediaz(child, spawnedByMediaz);
		}
	}
}

void GetUMGsByMediaz(const mz::fb::Node* node, TMap<FGuid, FString>& UMGsByMediaz)
{
	if (flatbuffers::IsFieldPresent(node, mz::fb::Node::VT_META_DATA_MAP))
	{
		if (auto entry = node->meta_data_map()->LookupByKey("umgTag"))
		{
			UMGsByMediaz.Add(*(FGuid*)node->id(), FString(entry->value()->c_str()));
		}
	}
	if (flatbuffers::IsFieldPresent(node->contents_as_Graph(), mz::fb::Graph::VT_NODES))
	{
		for (auto child : *node->contents_as_Graph()->nodes())
		{
			GetUMGsByMediaz(child, UMGsByMediaz);
		}
	}
}

void GetNodesWithProperty(const mz::fb::Node* node, std::vector<const mz::fb::Node*>& out)
{
	if (flatbuffers::IsFieldPresent(node, mz::fb::Node::VT_PINS) && node->pins()->size() > 0)
	{
		out.push_back(node);
	}

	if (flatbuffers::IsFieldPresent(node->contents_as_Graph(), mz::fb::Graph::VT_NODES))
	{
		for (auto child : *node->contents_as_Graph()->nodes())
		{
			GetNodesWithProperty(child, out);
		}
	}
	

}

void FMZSceneTreeManager::OnPropertyChanged(UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent)
{
	if (!PropertyChangedEvent.MemberProperty || !PropertyChangedEvent.Property)
	{
		return;
	}
	if (!ObjectBeingModified->IsA(PropertyChangedEvent.MemberProperty->GetOwner<UClass>()))
	{
		return;
	}
	//not sure whether we need this check
	//if (PropertyChangedEvent.Property && !ObjectBeingModified->IsA(PropertyChangedEvent.Property->GetOwner<UClass>()))
	//{
	//	return;
	//}
	if (!PropertyChangedEvent.Property->IsValidLowLevel())
	{
		return;
	}
	if (MZPropertyManager.PropertiesByPropertyAndContainer.Contains({PropertyChangedEvent.Property, ObjectBeingModified}))
	{
		auto mzprop = MZPropertyManager.PropertiesByPropertyAndContainer.FindRef({PropertyChangedEvent.Property, ObjectBeingModified});
		if(mzprop->TypeName != "mz.fb.Void")
		{
			mzprop->UpdatePinValue();
			SendPinValueChanged(mzprop->Id, mzprop->data);
		}
		return;
	}
	if (MZPropertyManager.PropertiesByPropertyAndContainer.Contains({PropertyChangedEvent.MemberProperty, ObjectBeingModified}))
	{
		auto mzprop = MZPropertyManager.PropertiesByPropertyAndContainer.FindRef({PropertyChangedEvent.MemberProperty, ObjectBeingModified});
		if(mzprop->TypeName != "mz.fb.Void")
		{
			mzprop->UpdatePinValue();
			SendPinValueChanged(mzprop->Id, mzprop->data);
		}
		return;
	}
}

void FMZSceneTreeManager::OnActorSpawned(AActor* InActor)
{
	if (IsActorDisplayable(InActor))
	{
		LOGF("%s is spawned", *(InActor->GetFName().ToString()));
		if (SceneTree.NodeMap.Contains(InActor->GetActorGuid()))
		{
			return;
		}
		SendActorAdded(InActor);
	}
}

void FMZSceneTreeManager::OnActorDestroyed(AActor* InActor)
{
	LOGF("%s is destroyed.", *(InActor->GetFName().ToString()));
	auto id = InActor->GetActorGuid();
	TSet<UObject*> RemovedItems;
	RemovedItems.Add(InActor);
	auto Components = InActor->GetComponents();
	for (auto comp : Components)
	{
		RemovedItems.Add(comp);
	}
	SendActorDeleted(id, RemovedItems);
}

void FMZSceneTreeManager::OnMZNodeImported(mz::fb::Node const& appNode)
{
	//MZActorManager->ClearActors();
	FMZClient::NodeId = *(FGuid*)appNode.id();
	SceneTree.Root->Id = FMZClient::NodeId;

	auto node = &appNode;

	std::vector<flatbuffers::Offset<mz::PartialPinUpdate>> PinUpdates;
	flatbuffers::FlatBufferBuilder fb1;
	if(node->pins() && node->pins()->size() > 0)
	{
		for (auto pin : *node->pins())
		{
			PinUpdates.push_back(mz::CreatePartialPinUpdate(fb1, pin->id(), 0, mz::fb::CreateOrphanStateDirect(fb1, true, "Object not found in the scene")));
		}
	}
	auto offset = mz::CreatePartialNodeUpdateDirect(fb1, (mz::fb::UUID*)&FMZClient::NodeId, mz::ClearFlags::NONE, 0, 0, 0, 0, 0, 0, 0, &PinUpdates);
	fb1.Finish(offset);
	auto buf = fb1.Release();
	auto root = flatbuffers::GetRoot<mz::PartialNodeUpdate>(buf.data());
	MZClient->AppServiceClient->SendPartialNodeUpdate(*root);

	flatbuffers::FlatBufferBuilder fb3;
	auto offset2 = mz::CreatePartialNodeUpdateDirect(fb3, (mz::fb::UUID*)&FMZClient::NodeId, mz::ClearFlags::CLEAR_FUNCTIONS | mz::ClearFlags::CLEAR_NODES);
	fb3.Finish(offset2);
	auto buf2 = fb3.Release();
	auto root2 = flatbuffers::GetRoot<mz::PartialNodeUpdate>(buf2.data());
	MZClient->AppServiceClient->SendPartialNodeUpdate(*root);


	std::vector<const mz::fb::Node*> nodesWithProperty;
	GetNodesWithProperty(node, nodesWithProperty);
	std::vector<PropUpdate> updates;
	for (auto nodeW : nodesWithProperty)
	{
		FGuid id = *(FGuid*)(nodeW->id());
		for (auto prop : *nodeW->pins())
		{
			
			if (flatbuffers::IsFieldPresent(prop, mz::fb::Pin::VT_META_DATA_MAP))
			{
				FString componentName;
				FString displayName;
				FString PropertyPath;
				FString ContainerPath;
				char* valcopy = nullptr;
				char* defcopy = nullptr;
				size_t valsize = 0;
				size_t defsize = 0;
				if (flatbuffers::IsFieldPresent(prop, mz::fb::Pin::VT_DATA))
				{
					valcopy = new char[prop->data()->size()];
					valsize = prop->data()->size();
					memcpy(valcopy, prop->data()->data(), prop->data()->size());
				}
				if (flatbuffers::IsFieldPresent(prop, mz::fb::Pin::VT_DEF))
				{
					defcopy = new char[prop->def()->size()];
					defsize = prop->def()->size();
					memcpy(defcopy, prop->def()->data(), prop->def()->size());
				}

				if (auto entry = prop->meta_data_map()->LookupByKey("PropertyPath"))
				{
					PropertyPath = FString(entry->value()->c_str());
				}
				if (auto entry = prop->meta_data_map()->LookupByKey("ContainerPath"))
				{
					ContainerPath = FString(entry->value()->c_str());
				}
				if (auto entry = prop->meta_data_map()->LookupByKey("component"))
				{
					componentName = FString(entry->value()->c_str());
				}
				if (auto entry = prop->meta_data_map()->LookupByKey("actorId"))
				{
					FString actorIdString = FString(entry->value()->c_str());
					FGuid actorId;
					if (FGuid::Parse(actorIdString, actorId))
					{
						id = actorId;
					}
				}

				if (flatbuffers::IsFieldPresent(prop, mz::fb::Pin::VT_NAME))
				{
					displayName = FString(prop->name()->c_str());
				}

				bool IsPortal = prop->contents_type() == mz::fb::PinContents::PortalPin;
				
				updates.push_back({ id, *(FGuid*)prop->id(),displayName, componentName, PropertyPath, ContainerPath,valcopy, valsize, defcopy, defsize, prop->show_as(), IsPortal});
			}

		}
	}

	TMap<FGuid, FString> spawnedByMediaz; //old guid (imported) x spawn tag
	GetNodesSpawnedByMediaz(node, spawnedByMediaz);

	TMap<FGuid, FString> UMGsByMediaz; //old guid (imported) x spawn tag
	GetUMGsByMediaz(node, UMGsByMediaz);
	
	UWorld* World = GEngine->GetWorldContextFromGameViewport(GEngine->GameViewport)->World();

	TMap<FGuid, AActor*> sceneActorMap;
	if (World)
	{
		for (TActorIterator< AActor > ActorItr(World); ActorItr; ++ActorItr)
		{
			sceneActorMap.Add(ActorItr->GetActorGuid(), *ActorItr);
		}
	}

	FGuid OldParentTransformId = {};
	for (auto [oldGuid, spawnTag] : spawnedByMediaz)
	{
		if(spawnTag == "RealityParentTransform")
		{
			AActor* spawnedActor = MZActorManager->SpawnActor(spawnTag);
			sceneActorMap.Add(oldGuid, spawnedActor); //this will map the old id with spawned actor in order to match the old properties (imported from disk)
			MZActorManager->ParentTransformActor = MZActorReference(spawnedActor);
			MZActorManager->ParentTransformActor->GetRootComponent()->SetMobility(EComponentMobility::Static);
			OldParentTransformId = oldGuid;
		}
	}
	if(OldParentTransformId.IsValid())
	{
		spawnedByMediaz.Remove(OldParentTransformId);
	}
		
	for (auto [oldGuid, spawnTag] : spawnedByMediaz)
	{
		if (!sceneActorMap.Contains(oldGuid))
		{
			///spawn
			AActor* spawnedActor = MZActorManager->SpawnActor(spawnTag);
			if (spawnedActor)
			{
				sceneActorMap.Add(oldGuid, spawnedActor); //this will map the old id with spawned actor in order to match the old properties (imported from disk)
			}
		}
	}

	for (auto [oldGuid, umgTag] : UMGsByMediaz)
	{
		if (!sceneActorMap.Contains(oldGuid))
		{
			////
			UUserWidget* newWidget = MZAssetManager->CreateUMGFromTag(umgTag);
			AActor* spawnedActor = MZActorManager->SpawnUMGRenderManager(umgTag, newWidget);
			if (spawnedActor)
			{
				sceneActorMap.Add(oldGuid, spawnedActor); //this will map the old id with spawned actor in order to match the old properties (imported from disk)
			}
		}
	}

	for (auto const& update : updates)
	{
		FGuid ActorId = update.actorId;
		
		UObject* Container = nullptr;
		if (sceneActorMap.Contains(ActorId))
		{
			Container = sceneActorMap.FindRef(ActorId);
		}
		if (!update.componentName.IsEmpty())
		{
			Container = FindObject<USceneComponent>(Container, *update.componentName);
		}
		if(!Container)
		{
			continue;
		}
		TSharedPtr<MZProperty> mzprop = nullptr;

		FProperty* PropertyToUpdate = FindFProperty<FProperty>(*update.PropertyPath);
		if(!PropertyToUpdate->IsValidLowLevel())
		{
			continue;
		}
		if(!update.ContainerPath.IsEmpty())
		{
			bool IsResultUObject;
			void* UnknownContainer = FindContainerFromContainerPath(Container, update.ContainerPath, IsResultUObject);
			if(!UnknownContainer)
			{
				LOGF("No container is found from the saved properties path : %s", *update.ContainerPath)
				continue;
			}
			if(IsResultUObject)
			{
				mzprop = MZPropertyFactory::CreateProperty((UObject*) UnknownContainer, PropertyToUpdate);
			}
			else
			{
				mzprop = MZPropertyFactory::CreateProperty(nullptr, PropertyToUpdate, nullptr, nullptr, FString(), (uint8*)UnknownContainer);
			}
		}
		else
		{
			mzprop = MZPropertyFactory::CreateProperty(Container, PropertyToUpdate);
		}
		if(!mzprop)
		{
			continue;
		}
		
		if (update.newValSize > 0)
		{
			mzprop->SetPropValue(update.newVal, update.newValSize);
		}
		if (!update.displayName.IsEmpty())
		{
			mzprop->DisplayName = update.displayName;
		}
		mzprop->UpdatePinValue();
		mzprop->PinShowAs = update.pinShowAs;
		if (mzprop && update.defValSize > 0)
		{
			mzprop->default_val = std::vector<uint8>(update.defValSize, 0);
			memcpy(mzprop->default_val.data(), update.defVal, update.defValSize);
		}
	}

	RescanScene(true);
	SendNodeUpdate(FMZClient::NodeId, false);

	PinUpdates.clear();
	flatbuffers::FlatBufferBuilder fb2;
	std::vector<MZPortal> NewPortals;
	for (auto const& update : updates)
	{
		FGuid ActorId = update.actorId;
		
		if (update.IsPortal)
		{
			UObject* Container = nullptr;
			if (sceneActorMap.Contains(ActorId))
			{
				Container = sceneActorMap.FindRef(ActorId);
				PopulateAllChilds(Cast<AActor>(Container));
			}
			else
			{
				continue;
			}
			if (!update.componentName.IsEmpty())
			{
				Container = FindObject<USceneComponent>(Container, *update.componentName);
			}
			if(!Container)
			{
				continue;
			}
			
			FProperty* PropertyToUpdate = FindFProperty<FProperty>(*update.PropertyPath);
			if(!PropertyToUpdate)
			{
				continue;
			}
			void* UnknownContainer = Container;
			if(!update.ContainerPath.IsEmpty())
			{
				bool discard;
				UnknownContainer = FindContainerFromContainerPath(Container, update.ContainerPath, discard);
			}
			if(!UnknownContainer)
			{
				continue;
			}
			if (MZPropertyManager.PropertiesByPropertyAndContainer.Contains({PropertyToUpdate, UnknownContainer}))
			{
				auto MzProperty = MZPropertyManager.PropertiesByPropertyAndContainer.FindRef({PropertyToUpdate, UnknownContainer});
				PinUpdates.push_back(mz::CreatePartialPinUpdate(fb2, (mz::fb::UUID*)&update.pinId,  (mz::fb::UUID*)&MzProperty->Id, mz::fb::CreateOrphanStateDirect(fb2, false)));
				MZPortal NewPortal{update.pinId ,MzProperty->Id};
				NewPortal.DisplayName = FString("");
				UObject* parent = MzProperty->GetRawObjectContainer();
				while (parent)
				{
					NewPortal.DisplayName = parent->GetFName().ToString() + FString(".") + NewPortal.DisplayName;
					parent = parent->GetTypedOuter<AActor>();
				}

				NewPortal.DisplayName += MzProperty->DisplayName;
				NewPortal.TypeName = FString(MzProperty->TypeName.c_str());
				NewPortal.CategoryName = MzProperty->CategoryName;
				NewPortal.ShowAs = update.pinShowAs;

				MZPropertyManager.PortalPinsById.Add(NewPortal.Id, NewPortal);
				MZPropertyManager.PropertyToPortalPin.Add(MzProperty->Id, NewPortal.Id);
				NewPortals.push_back(NewPortal);
				MZTextureShareManager::GetInstance()->UpdatePinShowAs(MzProperty.Get(), update.pinShowAs);
				MZClient->AppServiceClient->SendPinShowAsChange((mz::fb::UUID&)MzProperty->Id, update.pinShowAs);
			}
			
		}

	}
	for (auto const& update : updates)
	{
		if(update.defVal)
		{
			delete[] update.defVal;
		}
		if(update.newVal)
		{
			delete[] update.newVal;
		}
	}
	if (!PinUpdates.empty())
	{
		auto offset3 = mz::CreatePartialNodeUpdateDirect(fb2, (mz::fb::UUID*)&FMZClient::NodeId, mz::ClearFlags::NONE, 0, 0, 0, 0, 0, 0, 0, &PinUpdates);
		fb2.Finish(offset3);
		auto buf3 = fb2.Release();
		auto root3 = flatbuffers::GetRoot<mz::PartialNodeUpdate>(buf3.data());
		MZClient->AppServiceClient->SendPartialNodeUpdate(*root3);
	}
	for (auto Portal : NewPortals)
	{
		if(MZPropertyManager.PropertiesById.Contains(Portal.SourceId))
		{
			flatbuffers::FlatBufferBuilder fb4;
			auto SourceProperty = MZPropertyManager.PropertiesById.FindRef(Portal.SourceId);
			auto UpdatedMetadata = SourceProperty->SerializeMetaData(fb4);
			auto offset4 = mz::CreateAppEventOffset(fb4, mz::app::CreatePinMetadataUpdateDirect(fb4, (mz::fb::UUID*)&Portal.Id, &UpdatedMetadata  ,true));
			fb4.Finish(offset4);
			auto buf4 = fb4.Release();
			auto root4 = flatbuffers::GetRoot<mz::app::AppEvent>(buf4.data());
			MZClient->AppServiceClient->Send(*root4);
		}
	}
	//SendSyncSemaphores(true);
	LOG("Node from MediaZ successfully imported");
}

void FMZSceneTreeManager::SetPropertyValue(FGuid pinId, void* newval, size_t size)
{
	if (!MZPropertyManager.PropertiesById.Contains(pinId))
	{
		UE_LOG(LogTemp, Warning, TEXT("The property with given id is not found."));
		return;
	}

	auto mzprop = MZPropertyManager.PropertiesById.FindRef(pinId);
	if(!mzprop->GetRawContainer())
	{
		return;
	}
	if (!MZPropertyManager.PropertyToPortalPin.Contains(pinId))
	{
		MZPropertyManager.CreatePortal(pinId, mz::fb::ShowAs::PROPERTY);
	}	
	
}

#ifdef VIEWPORT_TEXTURE
void FMZSceneTreeManager::ConnectViewportTexture()
{
	auto viewport = Cast<UMZViewportClient>(GEngine->GameViewport);
	if (IsValid(viewport) && ViewportTextureProperty)
	{
		auto mzprop = ViewportTextureProperty;
		mzprop->ObjectPtr = viewport;

		auto tex = MZTextureShareManager::GetInstance()->AddTexturePin(mzprop);
		mzprop->data = mz::Buffer::From(tex);
	}
}

void FMZSceneTreeManager::DisconnectViewportTexture()
{
	if (ViewportTextureProperty) {
		MZTextureShareManager::GetInstance()->TextureDestroyed(ViewportTextureProperty);
		ViewportTextureProperty->ObjectPtr = nullptr;
		auto tex = MZTextureShareManager::GetInstance()->AddTexturePin(ViewportTextureProperty);
		ViewportTextureProperty->data = mz::Buffer::From
		(tex);
		MZTextureShareManager::GetInstance()->TextureDestroyed(ViewportTextureProperty);
	}
}
#endif

void FMZSceneTreeManager::RescanScene(bool reset)
{
	if (reset)
	{
		Reset();
	}

	UWorld* World = FMZSceneTreeManager::daWorld;

	flatbuffers::FlatBufferBuilder fbb;
	std::vector<flatbuffers::Offset<mz::fb::Node>> actorNodes;
	TArray<AActor*> ActorsInScene;
	if (IsValid(World))
	{
		
		for (TActorIterator< AActor > ActorItr(World); ActorItr; ++ActorItr)
		{
			if (!IsActorDisplayable(*ActorItr) || ActorItr->GetParentActor())
			{
				continue;
			}
			AActor* parent = ActorItr->GetSceneOutlinerParent();

			if (parent)
			{
				if (SceneTree.ChildMap.Contains(parent->GetActorGuid()))
				{
					SceneTree.ChildMap.Find(parent->GetActorGuid())->Add(*ActorItr);
				}
				else
				{
					SceneTree.ChildMap.FindOrAdd(parent->GetActorGuid()).Add(*ActorItr);
				}
				continue;
			}

			ActorsInScene.Add(*ActorItr);
			auto newNode = SceneTree.AddActor(ActorItr->GetFolder().GetPath().ToString(), *ActorItr);
			if (newNode)
			{
				newNode->actor = MZActorReference(*ActorItr);
			}
		}
#ifdef VIEWPORT_TEXTURE
		ConnectViewportTexture();
#endif
	}
	LOG("SceneTree is constructed.");
}

bool PropertyVisible(FProperty* ueproperty)
{
	return !ueproperty->HasAllPropertyFlags(CPF_DisableEditOnInstance) &&
		!ueproperty->HasAllPropertyFlags(CPF_Deprecated) &&
		//!ueproperty->HasAllPropertyFlags(CPF_EditorOnly) && //? dont know what this flag does but it hides more than necessary
		ueproperty->HasAllPropertyFlags(CPF_Edit) &&
		//ueproperty->HasAllPropertyFlags(CPF_BlueprintVisible) && //? dont know what this flag does but it hides more than necessary
		ueproperty->HasAllFlags(RF_Public);
}

uint32_t SwapEndian(uint32_t num)
{
	return ((num >> 24) & 0xff) | // move byte 3 to byte 0
		((num << 8) & 0xff0000) | // move byte 1 to byte 2
		((num >> 8) & 0xff00) | // move byte 2 to byte 1
		((num << 24) & 0xff000000); // byte 0 to byte 3
}

FString UEIdToMZIDString(FGuid Guid)
{

	uint32 A = SwapEndian(Guid.A);
	uint32 B = SwapEndian(Guid.B);
	uint32 C = SwapEndian(Guid.C);
	uint32 D = SwapEndian(Guid.D);
	FString result = "";
	result.Appendf(TEXT("%08x-%04x-%04x-%04x-%04x%08x"), A, B >> 16, B & 0xFFFF, C >> 16, C & 0xFFFF, D);
	return result;
}

bool FMZSceneTreeManager::PopulateNode(FGuid nodeId)
{
	auto val = SceneTree.NodeMap.Find(nodeId);
	TSharedPtr<TreeNode> treeNode = val ? *val : nullptr;

	if (!treeNode || !treeNode->NeedsReload)
	{
		return false;
	}
	
	LOGF("Populating node with id %s", *nodeId.ToString());
	if (treeNode->GetAsActorNode())
	{
		auto actorNode = StaticCastSharedPtr<ActorNode>(treeNode);
		if (!IsValid(actorNode->actor.Get()))
		{
			return false;
		}
		bool ColoredChilds = false;
		if(actorNode->mzMetaData.Contains("NodeColor"))
		{
			ColoredChilds = true;
		}
		auto ActorClass = actorNode->actor->GetClass();

		//ITERATE PROPERTIES BEGIN
		class FProperty* AProperty = ActorClass->PropertyLink;
		while (AProperty != nullptr)
		{
			FName CategoryName = FObjectEditorUtils::GetCategoryFName(AProperty);

			UClass* Class = ActorClass;

			if (FEditorCategoryUtils::IsCategoryHiddenFromClass(Class, CategoryName.ToString()) || !PropertyVisible(AProperty))
			{
				AProperty = AProperty->PropertyLinkNext;
				continue;
			}
			auto mzprop = MZPropertyManager.CreateProperty(actorNode->actor.Get(), AProperty);
			if (!mzprop)
			{
				AProperty = AProperty->PropertyLinkNext;
				continue;
			}
			//RegisteredProperties.Add(mzprop->Id, mzprop);
			actorNode->Properties.push_back(mzprop);

			for (auto it : mzprop->childProperties)
			{
				//RegisteredProperties.Add(it->Id, it);
				actorNode->Properties.push_back(it);
			}

			AProperty = AProperty->PropertyLinkNext;
		}

		auto Components = actorNode->actor->GetComponents();

		for(auto MzProp : actorNode->Properties)
		{
			if(MzProp->EditConditionProperty)
			{
				for(auto prop : actorNode->Properties)
				{
					if(prop->Property == MzProp->EditConditionProperty)
					{
						
						MzProp->mzMetaDataMap.Add("EditConditionPropertyId", UEIdToMZIDString(prop->Id));
						
						UE_LOG(LogMZSceneTreeManager, Warning, TEXT("%s has edit condition named %s with pind id %s"), *MzProp->DisplayName, *prop->DisplayName, *MzProp->mzMetaDataMap["EditConditionPropertyId"]);
					}
				}
			}
		}

		
		//ITERATE PROPERTIES END

#ifdef MZ_POPULATE_UNREAL_FUNCTIONS 
		//ITERATE FUNCTIONS BEGIN
		auto ActorComponent = actorNode->actor->GetRootComponent();
		for (TFieldIterator<UFunction> FuncIt(ActorClass, EFieldIteratorFlags::IncludeSuper); FuncIt; ++FuncIt)
		{
			UFunction* UEFunction = *FuncIt;
			// LOGF("function with name %s is a function, indeed!", *UEFunction->GetFName().ToString());
			if (UEFunction->HasAllFunctionFlags(FUNC_BlueprintCallable /*| FUNC_Public*/) /*&&
				!UEFunction->HasAllFunctionFlags(FUNC_Event)*/) //commented out because custom events are seems to public? and not has FUNC_Event flags?
			{
				auto UEFunctionName = UEFunction->GetFName().ToString();

				if (UEFunctionName.StartsWith("OnChanged_") || UEFunctionName.StartsWith("OnLengthChanged_"))
				{
					continue; // do not export user's changed handler functions
				}

				auto OwnerClass = UEFunction->GetOwnerClass();
				if (!OwnerClass || !Cast<UBlueprint>(OwnerClass->ClassGeneratedBy))
				{
					continue; // export only BP functions //? what we will show in mediaz
				}

				TSharedPtr<MZFunction> mzfunc(new MZFunction(actorNode->actor.Get(), UEFunction));

				// Parse all function parameters.
				bool bNotSupported = false;
				for (TFieldIterator<FProperty> PropIt(UEFunction); PropIt && PropIt->HasAnyPropertyFlags(CPF_Parm); ++PropIt)
				{
					if (auto mzprop = MZPropertyManager.CreateProperty(nullptr, *PropIt))
					{
						mzfunc->Properties.push_back(mzprop);
						//RegisteredProperties.Add(mzprop->Id, mzprop);			
						if (PropIt->HasAnyPropertyFlags(CPF_OutParm))
						{
							mzfunc->OutProperties.push_back(mzprop);
						}
					}
					else
					{
						bNotSupported = true;
						break;
					}
				}
				if(bNotSupported)
				{
					continue;
				}

				actorNode->Functions.push_back(mzfunc);
				RegisteredFunctions.Add(mzfunc->Id, mzfunc);
			}
		}
		//ITERATE FUNCTIONS END
#endif
		//ITERATE CHILD COMPONENTS TO SHOW BEGIN
		actorNode->Children.clear();

		auto unattachedChildsPtr = SceneTree.ChildMap.Find(actorNode->Id);
		TSet<AActor*> unattachedChilds = unattachedChildsPtr ? *unattachedChildsPtr : TSet<AActor*>();
		for (auto child : unattachedChilds)
		{
			SceneTree.AddActor(actorNode, child);
		}

		AActor* ActorContext = actorNode->actor.Get();
		TSet<UActorComponent*> ComponentsToAdd(ActorContext->GetComponents());

		const bool bHideConstructionScriptComponentsInDetailsView = false; //GetDefault<UBlueprintEditorSettings>()->bHideConstructionScriptComponentsInDetailsView;
		auto ShouldAddInstancedActorComponent = [bHideConstructionScriptComponentsInDetailsView](UActorComponent* ActorComp, USceneComponent* ParentSceneComp)
		{
			// Exclude nested DSOs attached to BP-constructed instances, which are not mutable.
			return (ActorComp != nullptr
				&& (!ActorComp->IsVisualizationComponent())
				&& (ActorComp->CreationMethod != EComponentCreationMethod::UserConstructionScript || !bHideConstructionScriptComponentsInDetailsView)
				&& (ParentSceneComp == nullptr || !ParentSceneComp->IsCreatedByConstructionScript() || !ActorComp->HasAnyFlags(RF_DefaultSubObject)))
				&& (ActorComp->CreationMethod != EComponentCreationMethod::Native || FComponentEditorUtils::GetPropertyForEditableNativeComponent(ActorComp));
		};

		// Filter the components by their visibility
		for (TSet<UActorComponent*>::TIterator It(ComponentsToAdd.CreateIterator()); It; ++It)
		{
			UActorComponent* ActorComp = *It;
			USceneComponent* SceneComp = Cast<USceneComponent>(ActorComp);
			USceneComponent* ParentSceneComp = SceneComp != nullptr ? SceneComp->GetAttachParent() : nullptr;
			if (!ShouldAddInstancedActorComponent(ActorComp, ParentSceneComp))
			{
				It.RemoveCurrent();
			}
		}

		TArray<TSharedPtr<SceneComponentNode>> OutArray;

		TFunction<void(USceneComponent*, TSharedPtr<TreeNode>)> AddInstancedComponentsRecursive = [&, this](USceneComponent* Component, TSharedPtr<TreeNode> ParentHandle)
		{
			if (Component != nullptr)
			{
				for (USceneComponent* ChildComponent : Component->GetAttachChildren())
				{
					if (ComponentsToAdd.Contains(ChildComponent) && ChildComponent->GetOwner() == Component->GetOwner())
					{
						ComponentsToAdd.Remove(ChildComponent);
						TSharedPtr<SceneComponentNode> NewParentHandle = nullptr;
						if (ParentHandle->GetAsActorNode())
						{
							// TODO: TSharedFromThis
							auto ParentAsActorNode = StaticCastSharedPtr<ActorNode>(ParentHandle);
							NewParentHandle = this->SceneTree.AddSceneComponent(ParentAsActorNode, ChildComponent);
						}
						else if (ParentHandle->GetAsSceneComponentNode())
						{
							auto ParentAsSceneComponentNode = StaticCastSharedPtr<SceneComponentNode>(ParentHandle);
							NewParentHandle = this->SceneTree.AddSceneComponent(ParentAsSceneComponentNode, ChildComponent);
						}


						if (!NewParentHandle)
						{
							LOG("A Child node other than actor or component is present!");
							continue;
						}
						if(ColoredChilds)
						{
							NewParentHandle->mzMetaData.Add("NodeColor", HEXCOLOR_Reality_Node);
						}
						NewParentHandle->Children.clear();
						OutArray.Add(NewParentHandle);

						AddInstancedComponentsRecursive(ChildComponent, NewParentHandle);
					}
				}
			}
		};

		USceneComponent* RootComponent = ActorContext->GetRootComponent();

		// Add the root component first
		if (RootComponent != nullptr)
		{
			// We want this to be first every time, so remove it from the set of components that will be added later
			ComponentsToAdd.Remove(RootComponent);

			// Add the root component first
			auto RootHandle = SceneTree.AddSceneComponent(actorNode, RootComponent);
			if(RootHandle)
			{
				if(ColoredChilds)
				{
					RootHandle->mzMetaData.Add("NodeColor", HEXCOLOR_Reality_Node);
				}
				// Clear the loading child
				RootHandle->Children.clear();
				OutArray.Add(RootHandle);
			}
			// Recursively add
			AddInstancedComponentsRecursive(RootComponent, RootHandle);
		}

		// Sort components by type (always put scene components first in the tree)
		ComponentsToAdd.Sort([](const UActorComponent& A, const UActorComponent& /* B */)
			{
				return A.IsA<USceneComponent>();
			});

		// Now add any remaining instanced owned components not already added above. This will first add any
		// unattached scene components followed by any instanced non-scene components owned by the Actor instance.
		for (UActorComponent* ActorComp : ComponentsToAdd)
		{
			// Create new subobject data with the original data as their parent.
			//OutArray.Add(SceneTree.AddSceneComponent(componentNode, ActorComp)); //TODO scene tree add actor components
		}
		//ITERATE CHILD COMPONENTS TO SHOW END

		treeNode->NeedsReload = false;
		return true;
	}
	else if (treeNode->GetAsSceneComponentNode())
	{
		auto Component = treeNode->GetAsSceneComponentNode()->sceneComponent;
		auto Actor = Component->GetOwner();
		auto ComponentNode = treeNode->GetAsSceneComponentNode();
		auto ComponentClass = Component->GetClass();

		for (FProperty* Property = ComponentClass->PropertyLink; Property; Property = Property->PropertyLinkNext)
		{

			FName CategoryName = FObjectEditorUtils::GetCategoryFName(Property);
			UClass* Class = Actor->StaticClass();

			if (FEditorCategoryUtils::IsCategoryHiddenFromClass(Class, CategoryName.ToString()) || !PropertyVisible(Property))
			{
				continue;
			}

			auto mzprop = MZPropertyManager.CreateProperty(Component.Get(), Property);
			if (mzprop)
			{
				//RegisteredProperties.Add(mzprop->Id, mzprop);
				ComponentNode->Properties.push_back(mzprop);

				for (auto it : mzprop->childProperties)
				{
					//RegisteredProperties.Add(it->Id, it);
					ComponentNode->Properties.push_back(it);
				}

			}
		}
		
		for(auto MzProp : ComponentNode->Properties)
		{
			if(MzProp->EditConditionProperty)
			{
				for(auto prop : ComponentNode->Properties)
				{
					if(prop->Property == MzProp->EditConditionProperty)
					{
						MzProp->mzMetaDataMap.Add("EditConditionPropertyId", UEIdToMZIDString(prop->Id));
						UE_LOG(LogMZSceneTreeManager, Warning, TEXT("%s has edit condition named %s with pind id %s"), *MzProp->DisplayName, *prop->DisplayName, *MzProp->mzMetaDataMap["EditConditionPropertyId"]);
					}
				}
			}
		}
		treeNode->NeedsReload = false;
		return true;
	}
	return false;
}


void FMZSceneTreeManager::SendNodeUpdate(FGuid nodeId, bool bResetRootPins)
{
	LOGF("Sending node update to MediaZ with id %s", *nodeId.ToString());
	if (!MZClient->IsConnected())
	{
		return;
	}

	if (nodeId == SceneTree.Root->Id)
	{
		if (!bResetRootPins)
		{
			flatbuffers::FlatBufferBuilder mb;
			std::vector<flatbuffers::Offset<mz::fb::Node>> graphNodes = SceneTree.Root->SerializeChildren(mb);
			std::vector<flatbuffers::Offset<mz::fb::Node>> graphFunctions;
			for (auto& [_, cfunc] : CustomFunctions)
			{
				graphFunctions.push_back(cfunc->Serialize(mb));
			}
		
			std::vector<flatbuffers::Offset<mz::fb::MetaDataEntry>> metadata = SceneTree.Root->SerializeMetaData(mb);
			auto offset = mz::CreatePartialNodeUpdateDirect(mb, (mz::fb::UUID*)&nodeId, mz::ClearFlags::CLEAR_FUNCTIONS | mz::ClearFlags::CLEAR_NODES, 0, 0, 0, &graphFunctions, 0, &graphNodes, 0, 0, &metadata);
			mb.Finish(offset);
			auto buf = mb.Release();
			auto root = flatbuffers::GetRoot<mz::PartialNodeUpdate>(buf.data());
			MZClient->AppServiceClient->SendPartialNodeUpdate(*root);

			return;
		}

		flatbuffers::FlatBufferBuilder mb = flatbuffers::FlatBufferBuilder();
		std::vector<flatbuffers::Offset<mz::fb::Node>> graphNodes = SceneTree.Root->SerializeChildren(mb);
		std::vector<flatbuffers::Offset<mz::fb::Pin>> graphPins;
		for (auto& [_, property] : CustomProperties)
		{
			graphPins.push_back(property->Serialize(mb));
		}
		for (auto& [_, pin] : Pins)
		{
			graphPins.push_back(pin->Serialize(mb));
		}
		std::vector<flatbuffers::Offset<mz::fb::Node>> graphFunctions;
		for (auto& [_, cfunc] : CustomFunctions)
		{
			graphFunctions.push_back(cfunc->Serialize(mb));

		}
		
		std::vector<flatbuffers::Offset<mz::fb::MetaDataEntry>> metadata = SceneTree.Root->SerializeMetaData(mb);
		auto offset =  mz::CreatePartialNodeUpdateDirect(mb, (mz::fb::UUID*)(&nodeId), mz::ClearFlags::ANY & ~mz::ClearFlags::CLEAR_METADATA, 0, &graphPins, 0, &graphFunctions, 0, &graphNodes, 0, 0, &metadata);
		mb.Finish(offset);
		auto buf = mb.Release();
		auto root = flatbuffers::GetRoot<mz::PartialNodeUpdate>(buf.data());
		MZClient->AppServiceClient->SendPartialNodeUpdate(*root);

		return;
	}
	auto val = SceneTree.NodeMap.Find(nodeId);
	TSharedPtr<TreeNode> treeNode = val ? *val : nullptr;
	if (!(treeNode))
	{
		return;
	}
	flatbuffers::FlatBufferBuilder mb;
	std::vector<flatbuffers::Offset<mz::fb::Node>> graphNodes = treeNode->SerializeChildren(mb);
	std::vector<flatbuffers::Offset<mz::fb::Pin>> graphPins;
	if (treeNode->GetAsActorNode())
	{
		graphPins = treeNode->GetAsActorNode()->SerializePins(mb);
	}
	else if (treeNode->GetAsSceneComponentNode())
	{
		graphPins = treeNode->GetAsSceneComponentNode()->SerializePins(mb);
	}
	std::vector<flatbuffers::Offset<mz::fb::Node>> graphFunctions;
	if (treeNode->GetAsActorNode())
	{
		for (auto mzfunc : treeNode->GetAsActorNode()->Functions)
		{
			graphFunctions.push_back(mzfunc->Serialize(mb));
		}
	}
	auto metadata = treeNode->SerializeMetaData(mb);
	auto offset = mz::CreatePartialNodeUpdateDirect(mb, (mz::fb::UUID*)&nodeId, mz::ClearFlags::CLEAR_PINS | mz::ClearFlags::CLEAR_FUNCTIONS | mz::ClearFlags::CLEAR_NODES | mz::ClearFlags::CLEAR_METADATA, 0, &graphPins, 0, &graphFunctions, 0, &graphNodes, 0, 0, &metadata);
	mb.Finish(offset);
	auto buf = mb.Release();
	auto root = flatbuffers::GetRoot<mz::PartialNodeUpdate>(buf.data());
	MZClient->AppServiceClient->SendPartialNodeUpdate(*root);
}

void FMZSceneTreeManager::SendEngineFunctionUpdate()
{
	if (!MZClient || !MZClient->IsConnected())
	{
		return;
	}
	flatbuffers::FlatBufferBuilder mb = flatbuffers::FlatBufferBuilder();
	std::vector<flatbuffers::Offset<mz::fb::Node>> graphFunctions;
	for (auto& [_, cfunc] : CustomFunctions)
	{
		graphFunctions.push_back(cfunc->Serialize(mb));

	}
	std::vector<flatbuffers::Offset<mz::fb::MetaDataEntry>> metadata = SceneTree.Root->SerializeMetaData(mb);
	auto offset =  mz::CreatePartialNodeUpdateDirect(mb, (mz::fb::UUID*)(&FMZClient::NodeId), mz::ClearFlags::CLEAR_FUNCTIONS, 0, 0, 0, &graphFunctions, 0, 0, 0, 0, &metadata);
	mb.Finish(offset);
	auto buf = mb.Release();
	auto root = flatbuffers::GetRoot<mz::PartialNodeUpdate>(buf.data());
	MZClient->AppServiceClient->SendPartialNodeUpdate(*root);
}

void FMZSceneTreeManager::SendPinValueChanged(FGuid propertyId, std::vector<uint8> data)
{
	if (!MZClient->IsConnected() || data.empty())
	{
		return;
	}

	flatbuffers::FlatBufferBuilder mb;
	auto offset = mz::CreatePinValueChangedDirect(mb, (mz::fb::UUID*)&propertyId, &data);
	mb.Finish(offset);
	auto buf = mb.Release();
	auto root = flatbuffers::GetRoot<mz::PinValueChanged>(buf.data());
	MZClient->AppServiceClient->NotifyPinValueChanged(*root);
}

void FMZSceneTreeManager::SendPinUpdate()
{
	if (!MZClient->IsConnected())
	{
		return;
	}

	auto nodeId = FMZClient::NodeId;

	flatbuffers::FlatBufferBuilder mb;
	std::vector<flatbuffers::Offset<mz::fb::Pin>> graphPins;
	for (auto& [_, pin] : CustomProperties)
	{
		graphPins.push_back(pin->Serialize(mb));
	}
	for (auto& [_, pin] : Pins)
	{
		graphPins.push_back(pin->Serialize(mb));
	}
	auto offset = mz::CreatePartialNodeUpdateDirect(mb, (mz::fb::UUID*)&nodeId, mz::ClearFlags::CLEAR_PINS, 0, &graphPins, 0, 0, 0, 0);
	mb.Finish(offset);
	auto buf = mb.Release();
	auto root = flatbuffers::GetRoot<mz::PartialNodeUpdate>(buf.data());
	MZClient->AppServiceClient->SendPartialNodeUpdate(*root);

}

void FMZSceneTreeManager::RemovePortal(FGuid PortalId)
{
	LOG("Portal is removed.");
	
	if(!MZPropertyManager.PortalPinsById.Contains(PortalId))
	{
		return;
	}
	auto Portal = MZPropertyManager.PortalPinsById.FindRef(PortalId);
	MZPropertyManager.PortalPinsById.Remove(Portal.Id);
	MZPropertyManager.PropertyToPortalPin.Remove(Portal.SourceId);

	if(!MZClient->IsConnected())
	{
		return;
	}
	flatbuffers::FlatBufferBuilder mb;
	std::vector<mz::fb::UUID> pinsToDelete;
	pinsToDelete.push_back(*(mz::fb::UUID*)&Portal.Id);

	auto offset = mz::CreatePartialNodeUpdateDirect(mb, (mz::fb::UUID*)&FMZClient::NodeId, mz::ClearFlags::NONE, &pinsToDelete, 0, 0, 0, 0, 0);
	mb.Finish(offset);
	auto buf = mb.Release();
	auto root = flatbuffers::GetRoot<mz::PartialNodeUpdate>(buf.data());
	MZClient->AppServiceClient->SendPartialNodeUpdate(*root);
}

void FMZSceneTreeManager::SendPinAdded(FGuid NodeId, TSharedPtr<MZProperty> const& mzprop)
{
	if (!MZClient->IsConnected())
	{
		return;
	}
	flatbuffers::FlatBufferBuilder mb;
	std::vector<flatbuffers::Offset<mz::fb::Pin>> graphPins = { mzprop->Serialize(mb) };
	auto offset = mz::CreatePartialNodeUpdateDirect(mb, (mz::fb::UUID*)&NodeId, mz::ClearFlags::NONE, 0, &graphPins, 0, 0, 0, 0);
	mb.Finish(offset);
	auto buf = mb.Release();
	auto root = flatbuffers::GetRoot<mz::PartialNodeUpdate>(buf.data());
	MZClient->AppServiceClient->SendPartialNodeUpdate(*root);

	return;
}

void FMZSceneTreeManager::SendActorAdded(AActor* actor, FString spawnTag)
{
	TSharedPtr<ActorNode> newNode = nullptr;
	if (auto sceneParent = actor->GetSceneOutlinerParent())
	{
		if (SceneTree.NodeMap.Contains(sceneParent->GetActorGuid()))
		{
			auto parentNode = SceneTree.NodeMap.FindRef(sceneParent->GetActorGuid());
			newNode = SceneTree.AddActor(parentNode, actor);
			if (!newNode)
			{
				return;
			}
			if (!spawnTag.IsEmpty())
			{
				newNode->mzMetaData.Add("spawnTag", spawnTag);
			}
			if (!MZClient->IsConnected())
			{
				return;
			}
			flatbuffers::FlatBufferBuilder mb;
			std::vector<flatbuffers::Offset<mz::fb::Node>> graphNodes = { newNode->Serialize(mb) };
			auto offset = mz::CreatePartialNodeUpdateDirect(mb, (mz::fb::UUID*)&parentNode->Id, mz::ClearFlags::NONE, 0, 0, 0, 0, 0, &graphNodes);
			mb.Finish(offset);
			auto buf = mb.Release();
			auto root = flatbuffers::GetRoot<mz::PartialNodeUpdate>(buf.data());
			MZClient->AppServiceClient->SendPartialNodeUpdate(*root);

		}
	}
	else
	{
		TSharedPtr<TreeNode> mostRecentParent;
		newNode = SceneTree.AddActor(actor->GetFolder().GetPath().ToString(), actor, mostRecentParent);
		if (!newNode)
		{
			return;
		}
		if (!spawnTag.IsEmpty())
		{
			newNode->mzMetaData.Add("spawnTag", spawnTag);
		}
		if (!MZClient->IsConnected())
		{
			return;
		}

		flatbuffers::FlatBufferBuilder mb;
		std::vector<flatbuffers::Offset<mz::fb::Node>> graphNodes = { mostRecentParent->Serialize(mb) };
		auto offset = mz::CreatePartialNodeUpdateDirect(mb, (mz::fb::UUID*)&mostRecentParent->Parent->Id, mz::ClearFlags::NONE, 0, 0, 0, 0, 0, &graphNodes);
		mb.Finish(offset);
		auto buf = mb.Release();
		auto root = flatbuffers::GetRoot<mz::PartialNodeUpdate>(buf.data());
		MZClient->AppServiceClient->SendPartialNodeUpdate(*root);

	}

	return;
}

void FMZSceneTreeManager::RemoveProperties(TSharedPtr<TreeNode> Node,
	TSet<TSharedPtr<MZProperty>>& PropertiesToRemove)
{
	if (auto componentNode = Node->GetAsSceneComponentNode())
	{
		for (auto& prop : componentNode->Properties)
		{
			PropertiesToRemove.Add(prop);
			MZPropertyManager.PropertiesById.Remove(prop->Id);
			MZPropertyManager.PropertiesByPropertyAndContainer.Remove({prop->Property, prop->GetRawObjectContainer()});
		}
	}
	else if (auto actorNode = Node->GetAsActorNode())
	{
		for (auto& prop : actorNode->Properties)
		{
			PropertiesToRemove.Add(prop);
			MZPropertyManager.PropertiesById.Remove(prop->Id);
			MZPropertyManager.PropertiesByPropertyAndContainer.Remove({prop->Property, prop->GetRawObjectContainer()});
		}
	}
	for (auto& child : Node->Children)
	{
		RemoveProperties(child, PropertiesToRemove);
	}
}

void FMZSceneTreeManager::CheckPins(TSet<UObject*>& RemovedObjects,
	TSet<TSharedPtr<MZProperty>>& PinsToRemove,
	TSet<TSharedPtr<MZProperty>>& PropertiesToRemove)
{
	for (auto& [id, pin] : Pins)
	{
		UObject* container = pin->GetRawObjectContainer();
		if (!container)
		{
			continue;
		}
		if (RemovedObjects.Contains(container))
		{
			PinsToRemove.Add(pin);
		}
	}
}

void FMZSceneTreeManager::Reset()
{
	MZTextureShareManager::GetInstance()->Reset();
	SceneTree.Clear();
	Pins.Empty();
	MZPropertyManager.Reset();
	MZActorManager->ReAddActorsToSceneTree();
}

void FMZSceneTreeManager::SendActorDeleted(FGuid Id, TSet<UObject*>& RemovedObjects)
{
	if (SceneTree.NodeMap.Contains(Id))
	{
		auto node = SceneTree.NodeMap.FindRef(Id);
		//delete properties
		// can be optimized by using raw pointers
		TSet<TSharedPtr<MZProperty>> propertiesToRemove;
		RemoveProperties(node, propertiesToRemove);
		TSet<FGuid> PropertiesWithPortals;
		TSet<FGuid> PortalsToRemove;
		auto texman = MZTextureShareManager::GetInstance();
		for (auto prop : propertiesToRemove)
		{
			if(prop->TypeName == "mz.fb.Texture")
			{
				texman->TextureDestroyed(prop.Get());
			}
			if (!MZPropertyManager.PropertyToPortalPin.Contains(prop->Id))
			{
				continue;
			}
			auto portalId = MZPropertyManager.PropertyToPortalPin.FindRef(prop->Id);
			PropertiesWithPortals.Add(prop->Id);
			if (!MZPropertyManager.PortalPinsById.Contains(portalId))
			{
				continue;
			}
			auto portal = MZPropertyManager.PortalPinsById.FindRef(portalId);
			PortalsToRemove.Add(portal.Id);
		}
		for (auto PropertyId : PropertiesWithPortals)
		{
			MZPropertyManager.PropertyToPortalPin.Remove(PropertyId);
		}
		for (auto PortalId : PortalsToRemove)
		{
			MZPropertyManager.PortalPinsById.Remove(PortalId);
		}

		MZPropertyManager.ActorsPropertyIds.Remove(Id);

		//delete from parent
		FGuid parentId = FMZClient::NodeId;
		if (auto parent = node->Parent)
		{
			parentId = parent->Id;
			auto v = parent->Children;
			auto it = std::find(v.begin(), v.end(), node);
			if (it != v.end())
				v.erase(it);
		}
		//delete from map
		SceneTree.NodeMap.Remove(node->Id);

		if (!MZClient->IsConnected())
		{
			return;
		}

		if (!PortalsToRemove.IsEmpty())
		{
			std::vector<mz::fb::UUID> pinsToDelete;
			for (auto portalId : PortalsToRemove)
			{
				pinsToDelete.push_back(*(mz::fb::UUID*)&portalId);
			}
			flatbuffers::FlatBufferBuilder mb;
			auto offset = mz::CreatePartialNodeUpdateDirect(mb, (mz::fb::UUID*)&FMZClient::NodeId, mz::ClearFlags::NONE, &pinsToDelete, 0, 0, 0, 0, 0);
			mb.Finish(offset);
			auto buf = mb.Release();
			auto root = flatbuffers::GetRoot<mz::PartialNodeUpdate>(buf.data());
			MZClient->AppServiceClient->SendPartialNodeUpdate(*root);
		}

		flatbuffers::FlatBufferBuilder mb2;
		std::vector<mz::fb::UUID> graphNodes = { *(mz::fb::UUID*)&node->Id };
		auto offset = mz::CreatePartialNodeUpdateDirect(mb2, (mz::fb::UUID*)&parentId, mz::ClearFlags::NONE, 0, 0, 0, 0, &graphNodes, 0);
		mb2.Finish(offset);
		auto buf = mb2.Release();
		auto root = flatbuffers::GetRoot<mz::PartialNodeUpdate>(buf.data());
		MZClient->AppServiceClient->SendPartialNodeUpdate(*root);
	}
}

void FMZSceneTreeManager::PopulateAllChilds(AActor* actor)
{
	LOGF("Populating all childs of %s", *actor->GetFName().ToString());
	FGuid ActorId = actor->GetActorGuid();
	PopulateAllChilds(ActorId);
}
void FMZSceneTreeManager::PopulateAllChilds(FGuid ActorId)
{
	LOGF("Populating all childs of actor with id %s", *ActorId.ToString());
	if (PopulateNode(ActorId))
	{
		SendNodeUpdate(ActorId);
	}

	if(SceneTree.NodeMap.Contains(ActorId))
	{
		auto ActorNode = SceneTree.NodeMap.FindRef(ActorId);
		
		for (auto ChildNode : ActorNode->Children)
		{
			if (ChildNode->GetAsActorNode())
			{
				PopulateAllChilds(ChildNode->GetAsActorNode()->actor.Get());
			}
			else if (ChildNode->GetAsSceneComponentNode())
			{
				PopulateAllChildsOfSceneComponentNode(ChildNode->GetAsSceneComponentNode());
			}
		}

	}
}

void FMZSceneTreeManager::PopulateAllChildsOfSceneComponentNode(SceneComponentNode* SceneComponentNode)
{
	if (!SceneComponentNode)
	{
		return;
	}

	if (PopulateNode(SceneComponentNode->Id))
	{
		SendNodeUpdate(SceneComponentNode->Id);
	}

	for (auto ChildNode : SceneComponentNode->Children)
	{
		if (ChildNode->GetAsActorNode())
		{
			PopulateAllChilds(ChildNode->GetAsActorNode()->actor.Get());
		}
		else if (ChildNode->GetAsSceneComponentNode())
		{
			PopulateAllChildsOfSceneComponentNode(ChildNode->GetAsSceneComponentNode());
		}
	}
}

void FMZSceneTreeManager::SendSyncSemaphores(bool RenewSemaphores)
{
	if(!FMZClient::NodeId.IsValid())
	{
		UE_LOG(LogMZSceneTreeManager, Error, TEXT("Sending sync semaphores with non-valid node Id, a deadlock might happen!"));
	}
	auto TextureShareManager = MZTextureShareManager::GetInstance();
	if(RenewSemaphores)
	{
		TextureShareManager->RenewSemaphores();
	}

	uint64_t inputSemaphore = (uint64_t)TextureShareManager->SyncSemaphoresExportHandles.InputSemaphore;
	uint64_t outputSemaphore = (uint64_t)TextureShareManager->SyncSemaphoresExportHandles.OutputSemaphore;

	flatbuffers::FlatBufferBuilder mb;
	auto offset = mz::CreateAppEventOffset(mb, mz::app::CreateSetSyncSemaphores(mb, (mz::fb::UUID*)&FMZClient::NodeId, FPlatformProcess::GetCurrentProcessId(), inputSemaphore, outputSemaphore));
	mb.Finish(offset);
	auto buf = mb.Release();
	auto root = flatbuffers::GetRoot<mz::app::AppEvent>(buf.data());
	MZClient->AppServiceClient->Send(*root);
}

struct PortalSourceContainerInfo
{
	FGuid ActorId;
	FString ComponentName;
	FString PropertyPath;
	FString ContainerPath;
	FProperty* Property;
};

void FMZSceneTreeManager::HandleWorldChange()
{
	LOG("Handling world change.");
	SceneTree.Clear();
	MZTextureShareManager::GetInstance()->Reset();

	TArray<TTuple<PortalSourceContainerInfo, MZPortal>> Portals;
	TSet<FGuid> ActorsToRescan;

	flatbuffers::FlatBufferBuilder mb;
	std::vector<mz::fb::UUID> graphPins;// = { *(mz::fb::UUID*)&node->Id };
	std::vector<flatbuffers::Offset<mz::PartialPinUpdate>> PinUpdates;

	for (auto [id, portal] : MZPropertyManager.PortalPinsById)
	{
		if (!MZPropertyManager.PropertiesById.Contains(portal.SourceId))
		{
			continue;
		}
		auto MzProperty = MZPropertyManager.PropertiesById.FindRef(portal.SourceId);

		
		PortalSourceContainerInfo ContainerInfo; //= { .ComponentName = "", .PropertyPath =  PropertyPath, .Property = MzProperty->Property};
		ContainerInfo.Property = MzProperty->Property;
		if(MzProperty->mzMetaDataMap.Contains("PropertyPath"))
		{
			ContainerInfo.PropertyPath = MzProperty->mzMetaDataMap.FindRef("PropertyPath");
		}
		
		if(MzProperty->mzMetaDataMap.Contains("ContainerPath"))
		{
			ContainerInfo.ContainerPath = MzProperty->mzMetaDataMap.FindRef("ContainerPath");
		}
		
		if(MzProperty->mzMetaDataMap.Contains("actorId"))
		{
			FString ActorIdString = MzProperty->mzMetaDataMap.FindRef("actorId");
			FGuid ActorId;
			FGuid::Parse(ActorIdString, ActorId);
			ContainerInfo.ActorId = ActorId;
			ActorsToRescan.Add(ActorId);
		}
		
		if(MzProperty->mzMetaDataMap.Contains("component"))
		{
			FString ComponentName = MzProperty->mzMetaDataMap.FindRef("component");
			ContainerInfo.ComponentName = ComponentName;
		}
		
		Portals.Add({ContainerInfo, portal});
		graphPins.push_back(*(mz::fb::UUID*)&portal.Id);
		PinUpdates.push_back(mz::CreatePartialPinUpdate(mb, (mz::fb::UUID*)&portal.Id, 0, mz::fb::CreateOrphanStateDirect(mb, true, "Object not found in the world")));
	}

	if (!MZClient->IsConnected())
	{
		return;
	}
	auto offset = mz::CreatePartialNodeUpdateDirect(mb, (mz::fb::UUID*)&FMZClient::NodeId, mz::ClearFlags::NONE, 0, 0, 0, 0, 0, 0, 0, &PinUpdates);
	mb.Finish(offset);
	auto buf = mb.Release();
	auto root = flatbuffers::GetRoot<mz::PartialNodeUpdate>(buf.data());
	MZClient->AppServiceClient->SendPartialNodeUpdate(*root);
	PinUpdates.clear();

	MZPropertyManager.Reset(false);
	MZActorManager->ReAddActorsToSceneTree();
	RescanScene(false);
	SendNodeUpdate(FMZClient::NodeId, false);


	for (auto ActorId : ActorsToRescan)
	{
		PopulateAllChilds(ActorId);
	}

	flatbuffers::FlatBufferBuilder mbb;
	std::vector<mz::fb::UUID> PinsToRemove;
	for (auto& [containerInfo, portal] : Portals)
	{
		UObject* ObjectContainer = FindContainer(containerInfo.ActorId, containerInfo.ComponentName);
		bool discard;
		void* UnknownContainer = FindContainerFromContainerPath(ObjectContainer, containerInfo.ContainerPath, discard);
		UnknownContainer = UnknownContainer ? UnknownContainer : ObjectContainer;
		if (MZPropertyManager.PropertiesByPropertyAndContainer.Contains({containerInfo.Property,UnknownContainer}))
		{
			auto MzProperty = MZPropertyManager.PropertiesByPropertyAndContainer.FindRef({containerInfo.Property,UnknownContainer});
			bool notOrphan = false;
			if (MZPropertyManager.PortalPinsById.Contains(portal.Id))
			{
				auto pPortal = MZPropertyManager.PortalPinsById.Find(portal.Id);
				pPortal->SourceId = MzProperty->Id;
			}
			portal.SourceId = MzProperty->Id;
			MZPropertyManager.PropertyToPortalPin.Add(MzProperty->Id, portal.Id);
			PinUpdates.push_back(mz::CreatePartialPinUpdate(mbb, (mz::fb::UUID*)&portal.Id, (mz::fb::UUID*)&MzProperty->Id, mz::fb::CreateOrphanStateDirect(mbb, notOrphan, notOrphan ? "" : "Object not found in the world")));
		}
		else
		{
			PinsToRemove.push_back(*(mz::fb::UUID*)&portal.Id);
		}

	}
	
	if (!PinUpdates.empty())
	{
		auto offset1 = 	mz::CreatePartialNodeUpdateDirect(mbb, (mz::fb::UUID*)&FMZClient::NodeId, mz::ClearFlags::NONE, 0, 0, 0, 0, 0, 0, 0, &PinUpdates);
		mbb.Finish(offset1);
		auto buf1 = mbb.Release();
		auto root1 = flatbuffers::GetRoot<mz::PartialNodeUpdate>(buf1.data());
		MZClient->AppServiceClient->SendPartialNodeUpdate(*root1);
	}
	if(!PinsToRemove.empty())
	{
		flatbuffers::FlatBufferBuilder mb2;
		auto offset2 = mz::CreatePartialNodeUpdateDirect(mb2, (mz::fb::UUID*)&FMZClient::NodeId, mz::ClearFlags::NONE, &PinsToRemove);
		mb2.Finish(offset2);
		auto buf2 = mb2.Release();
		auto root2 = flatbuffers::GetRoot<mz::PartialNodeUpdate>(buf2.data());
		MZClient->AppServiceClient->SendPartialNodeUpdate(*root2);
	}

	LOG("World change handled");
}

UObject* FMZSceneTreeManager::FindContainer(FGuid ActorId, FString ComponentName)
{
	UObject* Container = nullptr;
	UWorld* World = FMZSceneTreeManager::daWorld;
	for (TActorIterator< AActor > ActorItr(World); ActorItr; ++ActorItr)
	{
		if(ActorItr->GetActorGuid() == ActorId)
		{
			Container = *ActorItr;
		}
	}
	
	if (!ComponentName.IsEmpty())
	{
		Container = FindObject<USceneComponent>(Container, *ComponentName);
	}

	return Container;	
}

void* FMZSceneTreeManager::FindContainerFromContainerPath(UObject* BaseContainer, FString ContainerPath, bool& IsResultUObject)
{
	if(!BaseContainer)
	{
		IsResultUObject = false;
		return nullptr;
	}
	TArray<FString> ChildContainerNames;
	ContainerPath.ParseIntoArray(ChildContainerNames, TEXT("/"));
	FProperty* Property = nullptr; 
	UClass* ContainerClass = BaseContainer->GetClass();
	void* Container = BaseContainer;
	IsResultUObject = true;
	for(auto ChildContainerName : ChildContainerNames)
	{
		if(!Container)
		{
			return nullptr;
		}
		Property = FindFProperty<FProperty>(ContainerClass, *ChildContainerName);
		if(FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
		{
			UObject* Object = ObjectProperty->GetObjectPropertyValue(ObjectProperty->ContainerPtrToValuePtr<UObject>(Container));
			ContainerClass = Object->GetClass();
			Container = Object;
			IsResultUObject = true;
		}
		else if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			uint8* StructInstance = StructProperty->ContainerPtrToValuePtr<uint8>(Container);
			ContainerClass = StructProperty->Struct->GetClass();
			Container = StructInstance;
			IsResultUObject = false;
		}
		else
		{
			return nullptr;
		}
	}
	return Container;
}

void FMZSceneTreeManager::HandleBeginPIE(bool bIsSimulating)
{
	FString WorldName = GEditor->GetEditorWorldContext().World()->GetMapName();
	WorldName = GEngine->GetWorldContextFromGameViewport(GEngine->GameViewport)->World()->GetMapName();
	FMZSceneTreeManager::daWorld = GEngine->GetWorldContextFromGameViewport(GEngine->GameViewport)->World();
	HandleWorldChange();
}

void FMZSceneTreeManager::HandleEndPIE(bool bIsSimulating)
{
	FString WorldName = GEditor->GetEditorWorldContext().World()->GetMapName();
	FMZSceneTreeManager::daWorld = GEditor ? GEditor->GetEditorWorldContext().World() : GEngine->GetCurrentPlayWorld();
	HandleWorldChange();
}


AActor* FMZActorManager::GetParentTransformActor()
{
	if(!ParentTransformActor.Get())
	{
		ParentTransformActor = MZActorReference(SpawnActor("RealityParentTransform"));
		ParentTransformActor->GetRootComponent()->SetMobility(EComponentMobility::Static);
	}

	return ParentTransformActor.Get();
}

AActor* FMZActorManager::SpawnActor(FString SpawnTag)
{
	if (!MZAssetManager)
	{
		return nullptr;
	}

	AActor* SpawnedActor = MZAssetManager->SpawnFromTag(SpawnTag);
	if (!SpawnedActor)
	{
		return nullptr;
	}
	bool bIsSpawningParentTransform = (SpawnTag == "RealityParentTransform");
	if(!bIsSpawningParentTransform)
	{
		SpawnedActor->AttachToComponent(GetParentTransformActor()->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);	
	}

	ActorIds.Add(SpawnedActor->GetActorGuid());
	TMap<FString, FString> savedMetadata;
	savedMetadata.Add({"spawnTag", SpawnTag});
	savedMetadata.Add({"NodeColor", HEXCOLOR_Reality_Node});
	Actors.Add({ MZActorReference(SpawnedActor),savedMetadata });
	TSharedPtr<TreeNode> mostRecentParent;
	TSharedPtr<ActorNode> ActorNode = SceneTree.AddActor(NAME_Reality_FolderName.ToString(), SpawnedActor, mostRecentParent);
	ActorNode->mzMetaData.Add("spawnTag", SpawnTag);
	ActorNode->mzMetaData.Add("NodeColor", HEXCOLOR_Reality_Node);	
	
	if (!MZClient->IsConnected())
	{
		return SpawnedActor;
	}

	flatbuffers::FlatBufferBuilder mb;
	std::vector<flatbuffers::Offset<mz::fb::Node>> graphNodes = { mostRecentParent->Serialize(mb) };
	auto offset = mz::CreatePartialNodeUpdateDirect(mb, (mz::fb::UUID*)&mostRecentParent->Parent->Id, mz::ClearFlags::NONE, 0, 0, 0, 0, 0, &graphNodes);
	mb.Finish(offset);
	auto buf = mb.Release();
	auto root = flatbuffers::GetRoot<mz::PartialNodeUpdate>(buf.data());
	MZClient->AppServiceClient->SendPartialNodeUpdate(*root);

	return SpawnedActor;
}

AActor* FMZActorManager::SpawnUMGRenderManager(FString umgTag, UUserWidget* widget)
{

	if (!MZAssetManager)
	{
		return nullptr;
	}

	FString SpawnTag("CustomUMGRenderManager");
	AActor* UMGManager = MZAssetManager->SpawnFromTag(SpawnTag);
	if (!UMGManager)
	{
		return nullptr;
	}
	UMGManager->AttachToComponent(GetParentTransformActor()->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);	
	UMGManager->Rename(*MakeUniqueObjectName(nullptr, AActor::StaticClass(), FName(umgTag)).ToString());

//	Cast<AMZUMGRenderManager>(UMGManager)->Widget = widget;
	FObjectProperty* WidgetProperty = FindFProperty<FObjectProperty>(UMGManager->GetClass(), "Widget");
	if (WidgetProperty != nullptr)
		WidgetProperty->SetObjectPropertyValue_InContainer(UMGManager, widget);

	ActorIds.Add(UMGManager->GetActorGuid());
	TMap<FString, FString> savedMetadata;
	savedMetadata.Add({"umgTag", umgTag});
	savedMetadata.Add({"NodeColor", HEXCOLOR_Reality_Node});
	Actors.Add({ MZActorReference(UMGManager), savedMetadata});
	TSharedPtr<TreeNode> mostRecentParent;
	TSharedPtr<ActorNode> ActorNode = SceneTree.AddActor(NAME_Reality_FolderName.ToString(), UMGManager, mostRecentParent);
	ActorNode->mzMetaData.Add("umgTag", umgTag);
	ActorNode->mzMetaData.Add("NodeColor", HEXCOLOR_Reality_Node);	


	if (!MZClient->IsConnected())
	{
		return UMGManager;
	}

	flatbuffers::FlatBufferBuilder mb;
	std::vector<flatbuffers::Offset<mz::fb::Node>> graphNodes = { mostRecentParent->Serialize(mb) };
	auto offset = mz::CreatePartialNodeUpdateDirect(mb, (mz::fb::UUID*)&mostRecentParent->Parent->Id, mz::ClearFlags::NONE, 0, 0, 0, 0, 0, &graphNodes);
	mb.Finish(offset);
	auto buf = mb.Release();
	auto root = flatbuffers::GetRoot<mz::PartialNodeUpdate>(buf.data());
	MZClient->AppServiceClient->SendPartialNodeUpdate(*root);

	return UMGManager;
}

AActor* FMZActorManager::SpawnActor(UClass* ClassToSpawn)
{
	if (!MZAssetManager)
	{
		return nullptr;
	}

	FActorSpawnParameters sp;
	sp.bHideFromSceneOutliner = MZAssetManager->HideFromOutliner();
	AActor* SpawnedActor = GEngine->GetWorldContextFromGameViewport(GEngine->GameViewport)->World()->SpawnActor(ClassToSpawn, 0, sp);
	if (!SpawnedActor)
	{
		return nullptr;
	}
	SpawnedActor->AttachToComponent(GetParentTransformActor()->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);	

	ActorIds.Add(SpawnedActor->GetActorGuid());
	TMap<FString, FString> savedMetadata;
	savedMetadata.Add({"spawnPath", ClassToSpawn->GetClassPathName().ToString()});
	Actors.Add({MZActorReference(SpawnedActor), savedMetadata});
	TSharedPtr<TreeNode> mostRecentParent;
	TSharedPtr<ActorNode> ActorNode = SceneTree.AddActor(NAME_Reality_FolderName.ToString(), SpawnedActor, mostRecentParent);
	ActorNode->mzMetaData.Add("NodeColor", HEXCOLOR_Reality_Node);
	
	if (!MZClient->IsConnected())
	{
		return SpawnedActor;
	}

	flatbuffers::FlatBufferBuilder mb;
	std::vector<flatbuffers::Offset<mz::fb::Node>> graphNodes = { mostRecentParent->Serialize(mb) };
	auto offset = mz::CreatePartialNodeUpdateDirect(mb, (mz::fb::UUID*)&mostRecentParent->Parent->Id, mz::ClearFlags::NONE, 0, 0, 0, 0, 0, &graphNodes);
	mb.Finish(offset);
	auto buf = mb.Release();
	auto root = flatbuffers::GetRoot<mz::PartialNodeUpdate>(buf.data());
	MZClient->AppServiceClient->SendPartialNodeUpdate(*root);

	return SpawnedActor;
}

void FMZActorManager::ClearActors()
{
	LOG("Clearing all actors");
	//
	// Remove/destroy actors from Editor and PIE worlds.
	//
	// Remove from current (Editor or PIE) world.
	if(ParentTransformActor)
	{
		ParentTransformActor->Destroy(false, false);
	}
	for (auto& [Actor, spawnTag] : Actors)
	{
		AActor* actor = Actor.Get();
		if (actor)
			actor->Destroy(false, false);
	}
	// When Play starts then actors are duplicated from Editor world into newly created PIE world.
	if (FMZSceneTreeManager::daWorld)
	{
		EWorldType::Type CurrentWorldType = FMZSceneTreeManager::daWorld->WorldType.GetValue();
		if (CurrentWorldType == EWorldType::PIE)
		{
			// Actor was removed from PIE world. Remove him also from Editor world.
			UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
			ParentTransformActor.UpdateActorPointer(EditorWorld);
			if(ParentTransformActor)
			{
				ParentTransformActor->Destroy();
			}
			for (auto& [Actor, spawnTag] : Actors)
			{
				Actor.UpdateActorPointer(EditorWorld);
				AActor* actor = Actor.Get();
				if (actor)
					actor->Destroy(false, false);
			}
		}
	}
	
	// Clear local structures.
	ActorIds.Reset();
	Actors.Reset();
	SceneTree.Clear();
}

void FMZActorManager::ReAddActorsToSceneTree()
{
	for (auto& [Actor, SavedMetaData] : Actors)
	{
		if (Actor.UpdateActualActorPointer())
		{
			AActor* actor = Actor.Get();
			if (!actor)
			{
				continue;
			}

			auto ActorNode = SceneTree.AddActor(NAME_Reality_FolderName.ToString(), actor);
			for(auto [key, value] : SavedMetaData)
			{
				ActorNode->mzMetaData.Add(key, value);
			}
		}
		else
		{
			Actor = MZActorReference();
		}
	}
	Actors = Actors.FilterByPredicate([](const TPair<MZActorReference, TMap<FString, FString>>& Actor)
		{
			return Actor.Key;
		});
}

void FMZActorManager::RegisterDelegates()
{
	FEditorDelegates::PreSaveWorldWithContext.AddRaw(this, &FMZActorManager::PreSave);
	FEditorDelegates::PostSaveWorldWithContext.AddRaw(this, &FMZActorManager::PostSave);
}

void FMZActorManager::PreSave(UWorld* World, FObjectPreSaveContext Context)
{
	for (auto [Actor, spawnTag] : Actors)
	{
		AActor* actor = Actor.Get();
		if (!actor)
		{
			continue;
		}

		actor->SetFlags(actor->GetFlags() | RF_Transient);
	}
}

void FMZActorManager::PostSave(UWorld* World, FObjectPostSaveContext Context)
{
	for (auto [Actor, spawnTag] : Actors)
	{
		AActor* actor = Actor.Get();
		if (!actor)
		{
			continue;
		}

		actor->SetFlags(actor->GetFlags() & ~RF_Transient);
	}
}

FMZPropertyManager::FMZPropertyManager()
{
}

void FMZPropertyManager::CreatePortal(FGuid PropertyId, mz::fb::ShowAs ShowAs)
{
	if (!PropertiesById.Contains(PropertyId))
	{
		return;
	}
	auto MZProperty = PropertiesById.FindRef(PropertyId);

	if(!CheckPinShowAs(MZProperty->PinCanShowAs, ShowAs))
	{
		LOG("Pin can't be shown as the wanted type!");
		return;
	}
	MZTextureShareManager::GetInstance()->UpdatePinShowAs(MZProperty.Get(), ShowAs);
	MZClient->AppServiceClient->SendPinShowAsChange((mz::fb::UUID&)MZProperty->Id, ShowAs);
	
	MZPortal NewPortal{FGuid::NewGuid() ,PropertyId};
	NewPortal.DisplayName = FString("");
	UObject* parent = MZProperty->GetRawObjectContainer();
	FString parentName = "";
	while (parent)
	{
		parentName = parent->GetFName().ToString();
		if(auto actor = Cast<AActor>(parent))
			parentName = actor->GetActorLabel();
		if(auto component = Cast<USceneComponent>(parent))
			parentName = component->GetName();
		parentName += ".";
		parent = parent->GetTypedOuter<AActor>();
	}
	NewPortal.DisplayName =  parentName + NewPortal.DisplayName;

	NewPortal.DisplayName += MZProperty->DisplayName;
	NewPortal.TypeName = FString(MZProperty->TypeName.c_str());
	NewPortal.CategoryName = MZProperty->CategoryName;
	NewPortal.ShowAs = ShowAs;

	PortalPinsById.Add(NewPortal.Id, NewPortal);
	PropertyToPortalPin.Add(PropertyId, NewPortal.Id);

	if (!MZClient->IsConnected())
	{
		return;
	}
	flatbuffers::FlatBufferBuilder mb;
	std::vector<flatbuffers::Offset<mz::fb::Pin>> graphPins = { SerializePortal(mb, NewPortal, MZProperty.Get()) };
	auto offset = mz::CreatePartialNodeUpdateDirect(mb, (mz::fb::UUID*)&FMZClient::NodeId, mz::ClearFlags::NONE, 0, &graphPins, 0, 0, 0, 0);
	mb.Finish(offset);
	auto buf = mb.Release();
	auto root = flatbuffers::GetRoot<mz::PartialNodeUpdate>(buf.data());
	MZClient->AppServiceClient->SendPartialNodeUpdate(*root);
}

void FMZPropertyManager::CreatePortal(FProperty* uproperty, UObject* Container, mz::fb::ShowAs ShowAs)
{
	if (PropertiesByPropertyAndContainer.Contains({uproperty, Container}))
	{
		auto MzProperty =PropertiesByPropertyAndContainer.FindRef({uproperty, Container});
		if(!CheckPinShowAs(MzProperty->PinCanShowAs, ShowAs))
		{
			LOG("Pin can't be shown as the wanted type!");
			return;
		}
		CreatePortal(MzProperty->Id, ShowAs);
	}
}

TSharedPtr<MZProperty> FMZPropertyManager::CreateProperty(UObject* container, FProperty* uproperty, FString parentCategory)
{
	TSharedPtr<MZProperty> MzProperty = MZPropertyFactory::CreateProperty(container, uproperty, 0, 0, parentCategory);
	if (!MzProperty)
	{
		return nullptr;
	}

	PropertiesById.Add(MzProperty->Id, MzProperty);
	PropertiesByPropertyAndContainer.Add({MzProperty->Property, container}, MzProperty);

	if (MzProperty->ActorContainer)
	{
		ActorsPropertyIds.FindOrAdd(MzProperty->ActorContainer.Get()->GetActorGuid()).Add(MzProperty->Id);
	}
	else if (MzProperty->ComponentContainer)
	{
		ActorsPropertyIds.FindOrAdd(MzProperty->ComponentContainer.Actor.Get()->GetActorGuid()).Add(MzProperty->Id);
	}

	for (auto Child : MzProperty->childProperties)
	{
		PropertiesById.Add(Child->Id, Child);
		PropertiesByPropertyAndContainer.Add({Child->Property, Child->GetRawContainer()}, Child);
		
		if (Child->ActorContainer)
		{
			ActorsPropertyIds.FindOrAdd(Child->ActorContainer.Get()->GetActorGuid()).Add(Child->Id);
		}
		else if (Child->ComponentContainer)
		{
			ActorsPropertyIds.FindOrAdd(Child->ComponentContainer.Actor.Get()->GetActorGuid()).Add(Child->Id);
		}
	}

	return MzProperty;
}

void FMZPropertyManager::SetPropertyValue()
{
}

bool FMZPropertyManager::CheckPinShowAs(mz::fb::CanShowAs CanShowAs, mz::fb::ShowAs ShowAs)
{
	if(ShowAs == mz::fb::ShowAs::INPUT_PIN)
	{
		return (CanShowAs == mz::fb::CanShowAs::INPUT_OUTPUT) || 
				(CanShowAs == mz::fb::CanShowAs::INPUT_PIN_OR_PROPERTY) || 
				(CanShowAs == mz::fb::CanShowAs::INPUT_OUTPUT_PROPERTY) || 
				(CanShowAs == mz::fb::CanShowAs::INPUT_PIN_ONLY); 
	}
	if(ShowAs == mz::fb::ShowAs::OUTPUT_PIN)
	{
		return (CanShowAs == mz::fb::CanShowAs::INPUT_OUTPUT) || 
				(CanShowAs == mz::fb::CanShowAs::OUTPUT_PIN_OR_PROPERTY) || 
				(CanShowAs == mz::fb::CanShowAs::INPUT_OUTPUT_PROPERTY) || 
				(CanShowAs == mz::fb::CanShowAs::OUTPUT_PIN_ONLY); 
	}
	if(ShowAs == mz::fb::ShowAs::PROPERTY)
	{
		// we show every pin as property to begin with, may change in the future
		return true;
	}
	return true;
}

void FMZPropertyManager::ActorDeleted(FGuid DeletedActorId)
{
}

flatbuffers::Offset<mz::fb::Pin> FMZPropertyManager::SerializePortal(flatbuffers::FlatBufferBuilder& fbb, MZPortal Portal, MZProperty* SourceProperty)
{
	auto SerializedMetadata = SourceProperty->SerializeMetaData(fbb);
	bool bLive = (Portal.ShowAs == mz::fb::ShowAs::OUTPUT_PIN); 
	return mz::fb::CreatePinDirect(fbb, (mz::fb::UUID*)&Portal.Id, TCHAR_TO_UTF8(*Portal.DisplayName), TCHAR_TO_UTF8(*Portal.TypeName), Portal.ShowAs, SourceProperty->PinCanShowAs, TCHAR_TO_UTF8(*Portal.CategoryName), SourceProperty->SerializeVisualizer(fbb), 0, 0, 0, 0, 0, 0, 0, 0, false, &SerializedMetadata, bLive, mz::fb::PinContents::PortalPin, mz::fb::CreatePortalPin(fbb, (mz::fb::UUID*)&Portal.SourceId).Union(), 0, false, mz::fb::PinValueDisconnectBehavior::KEEP_LAST_VALUE, TCHAR_TO_UTF8(*SourceProperty->ToolTipText), TCHAR_TO_UTF8(*Portal.DisplayName));
}

void FMZPropertyManager::Reset(bool ResetPortals)
{
	if (ResetPortals)
	{
		PropertyToPortalPin.Empty();
		PortalPinsById.Empty();
	}

	PropertiesById.Empty();
	PropertiesByPointer.Empty();
	ActorsPropertyIds.Empty();
	PropertiesByPropertyAndContainer.Empty();
}

void FMZPropertyManager::OnBeginFrame()
{
	for (auto [id, portal] : PortalPinsById)
	{
		if (portal.ShowAs == mz::fb::ShowAs::OUTPUT_PIN || 
		    !PropertiesById.Contains(portal.SourceId))
		{
			continue;
		}
		
		auto MzProperty = PropertiesById.FindRef(portal.SourceId);

		if (portal.TypeName == "mz.fb.Texture")
		{
			MZTextureShareManager::GetInstance()->UpdateTexturePin(MzProperty.Get(), portal.ShowAs);
			continue;
		}

		auto shouldWait = portal.ShowAs == mz::fb::ShowAs::INPUT_PIN && portal.TypeName == "mz.fb.Track";
		auto buffer = MZClient->EventDelegates->Pop(*((mz::fb::UUID*)&MzProperty->Id), shouldWait);
		if (!buffer.IsEmpty())
		{
			MzProperty->SetPropValue(buffer.data(), buffer.size());
		}
	}
}

void FMZPropertyManager::OnEndFrame()
{
	// TODO: copy and dirty CPU out pins
}

std::vector<flatbuffers::Offset<mz::ContextMenuItem>> ContextMenuActions::SerializeActorMenuItems(flatbuffers::FlatBufferBuilder& fbb)
{
	std::vector<flatbuffers::Offset<mz::ContextMenuItem>> result;
	int command = 0;
	for (auto item : ActorMenu)
	{
		result.push_back(mz::CreateContextMenuItemDirect(fbb, TCHAR_TO_UTF8(*item.Key), command++, 0));
	}
	return result;
}

std::vector<flatbuffers::Offset<mz::ContextMenuItem>> ContextMenuActions::SerializePortalPropertyMenuItems(flatbuffers::FlatBufferBuilder& fbb)
{
	std::vector<flatbuffers::Offset<mz::ContextMenuItem>> result;
	int command = 0;
	for (auto item : PortalPropertyMenu)
	{
		result.push_back(mz::CreateContextMenuItemDirect(fbb, TCHAR_TO_UTF8(*item.Key), command++, 0));
	}
	return result;
}

ContextMenuActions::ContextMenuActions()
{
	TPair<FString, std::function<void(AActor*)> > deleteAction(FString("Delete Actor"), [](AActor* actor)
		{
			//actor->Destroy();
			actor->GetWorld()->EditorDestroyActor(actor, false);
		});
	ActorMenu.Add(deleteAction);
	TPair<FString, std::function<void(class FMZSceneTreeManager*, FGuid)>> PortalDeleteAction(FString("Delete Bookmark"), [](class FMZSceneTreeManager* MZSceneTreeManager,FGuid id)
		{
			MZSceneTreeManager->RemovePortal(id);	
		});
	PortalPropertyMenu.Add(PortalDeleteAction);
}

void ContextMenuActions::ExecuteActorAction(uint32 command, AActor* actor)
{
	if (ActorMenu.IsValidIndex(command))
	{
		ActorMenu[command].Value(actor);
	}
}
void ContextMenuActions::ExecutePortalPropertyAction(uint32 command, class FMZSceneTreeManager* MZSceneTreeManager, FGuid PortalId)
{
	if (PortalPropertyMenu.IsValidIndex(command))
	{
		PortalPropertyMenu[command].Value(MZSceneTreeManager, PortalId);
	}
}
