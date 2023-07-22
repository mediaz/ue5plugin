// Copyright MediaZ AS. All Rights Reserved.

#include "MZClient.h"
#include "MZCustomTimeStep.h"
// std
#include <cstdio>
#include <string>
#include <filesystem>

// UE
#include "HardwareInfo.h"
#include "LevelEditor.h"
#include "LevelEditorActions.h"
#include "Runtime/Engine/Classes/Kismet/GameplayStatics.h"
#include "Engine/EngineCustomTimeStep.h"
#include "Misc/MessageDialog.h"

//mediaz
#include "mzFlatBuffersCommon.h"

#define LOCTEXT_NAMESPACE "FMZClient"
#pragma optimize("", off)

DEFINE_LOG_CATEGORY(LogMZClient);
#define LOG(x) UE_LOG(LogMZClient, Display, TEXT(x))
#define LOGF(x, y) UE_LOG(LogMZClient, Display, TEXT(x), y)

FGuid FMZClient::NodeId = {};
FString FMZClient::AppKey = "";

void* FMediaZ::LibHandle = nullptr;
PFN_MakeAppServiceClient FMediaZ::MakeAppServiceClient = nullptr;
PFN_ShutdownClient FMediaZ::ShutdownClient = nullptr;
PFN_mzGetD3D12Resources FMediaZ::GetD3D12Resources = nullptr;

bool FMediaZ::Initialize()
{
	FString SdkPath = FPlatformMisc::GetEnvironmentVariable(TEXT("MZ_SDK_DIR"));
	FString SdkBinPath = FPaths::Combine(SdkPath, TEXT("bin"));
	FPlatformProcess::PushDllDirectory(*SdkBinPath);
	FString SdkDllPath = FPaths::Combine(SdkBinPath, "mzAppSDK.dll");

	if (!FPaths::FileExists(SdkDllPath))
	{
		UE_LOG(LogMZClient, Error, TEXT("Failed to find the mzAppSDK.dll at %s. Plugin will not be functional."), *SdkPath);
		return false;
	}

	LibHandle = FPlatformProcess::GetDllHandle(*SdkDllPath);

	if (LibHandle == nullptr)
	{
		UE_LOG(LogMZClient, Error, TEXT("Failed to load required library %s. Plugin will not be functional."), *SdkDllPath);
		return false;
	}

	MakeAppServiceClient = (PFN_MakeAppServiceClient)FPlatformProcess::GetDllExport(LibHandle, TEXT("MakeAppServiceClient"));
	ShutdownClient = (PFN_ShutdownClient)FPlatformProcess::GetDllExport(LibHandle, TEXT("ShutdownClient"));
	GetD3D12Resources = (PFN_mzGetD3D12Resources)FPlatformProcess::GetDllExport(LibHandle, TEXT("mzGetD3D12Resources"));
	
	if (!MakeAppServiceClient || !ShutdownClient || !GetD3D12Resources)
	{
		UE_LOG(LogMZClient, Error, TEXT("Failed to load some of the functions in MediaZ SDK. The plugin uses a different version of the SDK (%s) that what is available in your system."), *SdkDllPath)
		return false;
	}

	return true;
}

void FMediaZ::Shutdown()
{
	if (LibHandle)
	{
		FPlatformProcess::FreeDllHandle(LibHandle);
		LibHandle = nullptr;
		MakeAppServiceClient = nullptr;
		ShutdownClient = nullptr;
		GetD3D12Resources = nullptr;
		LOG("Unloaded MediaZ SDK dll successfully.");
	}
}

TMap<FGuid, std::vector<uint8>> ParsePins(mz::fb::Node const& archive)
{
	TMap<FGuid, std::vector<uint8>> re;
	if (!flatbuffers::IsFieldPresent(&archive, mz::fb::Node::VT_PINS))
	{
		return re;
	}
	for (auto pin : *archive.pins())
	{
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

void MZEventDelegates::OnAppConnected(mz::fb::Node const& appNode)
{
	FMZClient::NodeId = *(FGuid*)appNode.id();
	if (!PluginClient)
	{
		return;
	}

	LOG("Connected to mzEngine");
	PluginClient->Connected();

	mz::fb::TNode copy;
	appNode.UnPackTo(&copy);
	PluginClient->TaskQueue.Enqueue([MZClient = PluginClient, copy]()
		{
			flatbuffers::FlatBufferBuilder fbb;
			auto offset = mz::fb::CreateNode(fbb, &copy);
			fbb.Finish(offset);
			auto buf = fbb.Release();
			MZClient->OnMZConnected.Broadcast(*flatbuffers::GetRoot<mz::fb::Node>(buf.data()));
		});

    
}

void MZEventDelegates::OnNodeUpdated(mz::fb::Node const& appNode)
{
	LOG("Node update from mediaz");

	if (!PluginClient)
	{
		return;
	}

	if (!FMZClient::NodeId.IsValid())
	{
		FMZClient::NodeId = *(FGuid*)appNode.id();
		PluginClient->Connected();
	}

	mz::fb::TNode copy;
	appNode.UnPackTo(&copy);
	PluginClient->TaskQueue.Enqueue([MZClient = PluginClient, copy]()
		{
			flatbuffers::FlatBufferBuilder fbb;
			auto offset = mz::fb::CreateNode(fbb, &copy);
			fbb.Finish(offset);
			auto buf = fbb.Release();
			MZClient->OnMZNodeUpdated.Broadcast(*flatbuffers::GetRoot<mz::fb::Node>(buf.data()));
		});
}

void MZEventDelegates::OnConnectionClosed()
{
	LOG("Connection with mediaz is finished.");
	FMZClient::NodeId = {};
	if (!PluginClient)
	{
		return;
	}
	PluginClient->Disconnected();
	
	PluginClient->TaskQueue.Enqueue([MZClient = PluginClient]()
		{
			MZClient->OnMZConnectionClosed.Broadcast();
		});
}

void MZEventDelegates::OnStateChanged(mz::app::ExecutionState newState)
{
	LOGF("Execution state is changed from mediaz to %s", *FString(newState == mz::app::ExecutionState::SYNCED ? "synced" : "idle"));
	if (!PluginClient)
	{
		return;
	}
	PluginClient->TaskQueue.Enqueue([MZClient = PluginClient, newState]()
		{
			MZClient->OnMZStateChanged.Broadcast(newState);
		});
	
}

void MZEventDelegates::OnNodeRemoved()
{
	LOG("Plugin node removed from mediaz");
	if (!PluginClient)
	{
		return;
	}

	if (PluginClient->MZTimeStep.IsValid())
	{
		PluginClient->MZTimeStep->Step(1.f / 50.f);
	}

	PluginClient->TaskQueue.Enqueue([MZClient = PluginClient]()
		{
			FMZClient::NodeId = {};
			MZClient->OnMZNodeRemoved.Broadcast();
		});
}

void MZEventDelegates::OnPinValueChanged(mz::fb::UUID const& pinId, uint8_t const* data, size_t size, bool reset)
{
	if (!PluginClient)
	{
		return;
	}
	
	std::vector<uint8_t> copy(size, 0);
	memcpy(copy.data(), data, size);
	FGuid id = *(FGuid*)&pinId;

	PluginClient->TaskQueue.Enqueue([MZClient = PluginClient, copy, size, id, reset]()
		{
			MZClient->OnMZPinValueChanged.Broadcast(*(mz::fb::UUID*)&id, copy.data(), size, reset);
		});

}

void MZEventDelegates::OnPinShowAsChanged(mz::fb::UUID const& pinId, mz::fb::ShowAs newShowAs)
{
	LOG("Pin show as changed from mediaz");
	if (!PluginClient)
	{
		return;
	}
	

	FGuid id = *(FGuid*)&pinId;
	PluginClient->TaskQueue.Enqueue([MZClient = PluginClient, id, newShowAs]()
		{
			MZClient->OnMZPinShowAsChanged.Broadcast(*(mz::fb::UUID*)&id, newShowAs);
		});
}

void MZEventDelegates::OnFunctionCall(mz::fb::UUID const& nodeId, mz::fb::Node const& function)
{
	LOG("Function called from mediaz");
	if (!PluginClient)
	{
		return;
	}


	mz::fb::TNode copy;
	function.UnPackTo(&copy);
	FGuid id = *(FGuid*)&nodeId;

	PluginClient->TaskQueue.Enqueue([MZClient = PluginClient, copy, id]()
		{
			flatbuffers::FlatBufferBuilder fbb;
			auto offset = mz::fb::CreateNode(fbb, &copy);
			fbb.Finish(offset);
			auto buf = fbb.Release();
			MZClient->OnMZFunctionCalled.Broadcast(*(mz::fb::UUID*)&id, *flatbuffers::GetRoot<mz::fb::Node>(buf.data()));
		});
}

void MZEventDelegates::OnExecuteApp(mz::app::AppExecute const& appExecute)
{
	if (!PluginClient)
	{
		return;
	}
	
	PluginClient->OnUpdatedNodeExecuted(appExecute.delta_seconds());

	mz::app::TAppExecute appExecuteCopy;
	appExecute.UnPackTo(&appExecuteCopy);
	PluginClient->ExecuteQueue.Enqueue(appExecuteCopy);
	// PluginClient->TaskQueue.Enqueue([MZClient = PluginClient, appExecuteCopy]()
	// 	{
	// 		flatbuffers::FlatBufferBuilder fbb;
	// 		auto offset = mz::app::CreateAppExecute(fbb, &appExecuteCopy);
	// 		fbb.Finish(offset);
	// 		auto buf = fbb.Release();
	// 		MZClient->OnMZExecutedApp.Broadcast(*flatbuffers::GetRoot<mz::app::AppExecute>(buf.data()));
	// 	});
}

void MZEventDelegates::OnNodeSelected(mz::fb::UUID const& nodeId)
{
	LOG("Node selected from mediaz");
	if (!PluginClient)
	{
		return;
	}
	FGuid id = *(FGuid*)&nodeId;
	PluginClient->TaskQueue.Enqueue([MZClient = PluginClient, id]()
		{
			MZClient->OnMZNodeSelected.Broadcast(*(mz::fb::UUID*)&id);
		});
}

void MZEventDelegates::OnContextMenuRequested(mz::ContextMenuRequest const& request)
{
	LOG("Context menu fired from MediaZ");
	if (!PluginClient)
	{
		return;
	}

	
	mz::TContextMenuRequest copy;
	request.UnPackTo(&copy);
	PluginClient->TaskQueue.Enqueue([MZClient = PluginClient, copy]()
		{
			flatbuffers::FlatBufferBuilder fbb;
			auto offset = mz::CreateContextMenuRequest(fbb, &copy);
			fbb.Finish(offset);
			auto buf = fbb.Release();
			MZClient->OnMZContextMenuRequested.Broadcast(*flatbuffers::GetRoot<mz::ContextMenuRequest>(buf.data()));
		});
}

void MZEventDelegates::OnContextMenuCommandFired(mz::ContextMenuAction const& action)
{
	LOG("Context menu command fired from MediaZ");
	if (!PluginClient)
	{
		return;
	}

	mz::TContextMenuAction copy;
	action.UnPackTo(&copy);
	PluginClient->TaskQueue.Enqueue([MZClient = PluginClient, copy]()
		{
			flatbuffers::FlatBufferBuilder fbb;
			auto offset = mz::CreateContextMenuAction(fbb, &copy);
			fbb.Finish(offset);
			auto buf = fbb.Release();
			MZClient->OnMZContextMenuCommandFired.Broadcast(*flatbuffers::GetRoot<mz::ContextMenuAction>(buf.data()));
		});
}

void MZEventDelegates::OnNodeImported(mz::fb::Node const& appNode)
{
	LOG("Node imported from MediaZ");
	if (!PluginClient)
	{
		return;
	}


	mz::fb::TNode copy;
	appNode.UnPackTo(&copy);
	PluginClient->TaskQueue.Enqueue([MZClient = PluginClient, copy]()
		{
			flatbuffers::FlatBufferBuilder fbb;
			auto offset = mz::fb::CreateNode(fbb, &copy);
			fbb.Finish(offset);
			auto buf = fbb.Release();
			FMZClient::NodeId = *(FGuid*)&copy.id;
			MZClient->OnMZNodeImported.Broadcast(*flatbuffers::GetRoot<mz::fb::Node>(buf.data()));

			auto WorldContext = GEngine->GetWorldContextFromGameViewport(GEngine->GameViewport);
			if (WorldContext->World())
			{
				mz::fb::TNodeStatusMessage MapNameStatus;
				MapNameStatus.text = TCHAR_TO_UTF8(*WorldContext->World()->GetMapName());
				MapNameStatus.type = mz::fb::NodeStatusMessageType::INFO;
				MZClient->UENodeStatusHandler.Add("map_name", MapNameStatus);
			}
		});
}

FMZClient::FMZClient() {}

bool FMZClient::IsConnected()
{
	return AppServiceClient && AppServiceClient->IsConnected();
}

void FMZClient::Connected()
{
	TaskQueue.Enqueue([&]()
		{
			LOG("Sent map information to MediaZ");
			auto WorldContext = GEngine->GetWorldContextFromGameViewport(GEngine->GameViewport);
			if (WorldContext->World())
			{
				mz::fb::TNodeStatusMessage MapNameStatus;
				MapNameStatus.text = TCHAR_TO_UTF8(*WorldContext->World()->GetMapName());
				MapNameStatus.type = mz::fb::NodeStatusMessageType::INFO;
				UENodeStatusHandler.Add("map_name", MapNameStatus);
			}
		});
}

void FMZClient::Disconnected()
{
	if (MZTimeStep.IsValid())
	{
		MZTimeStep->Step(1.f / 50.f);
	}
}

void FMZClient::TryConnect()
{
	if (!IsWorldInitialized)
	{
		return;
	}

	if (!AppServiceClient)
	{
		FString CmdAppKey;
		if (FParse::Value(FCommandLine::Get(), TEXT("mzname"), CmdAppKey))
		{
			LOGF("MediaZ app key is provided: %s", *CmdAppKey);
			FMZClient::AppKey = CmdAppKey;
		}
		else
		{
			FMZClient::AppKey = "UE5";
		}
		auto ProjectPath = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
		auto ExePath = FString(FPlatformProcess::ExecutablePath());
		auto LaunchCommand = "\"\"" + ExePath + "\"" + " \"" + ProjectPath + "\" -game\"";
		AppServiceClient = FMediaZ::MakeAppServiceClient("localhost:50053", TCHAR_TO_ANSI(*FMZClient::AppKey), "UE5", TCHAR_TO_ANSI(*LaunchCommand));
		EventDelegates = TSharedPtr<MZEventDelegates>(new MZEventDelegates());
		EventDelegates->PluginClient = this;
		UENodeStatusHandler.SetClient(this);
		AppServiceClient->RegisterEventDelegates(EventDelegates.Get());
		LOG("AppClient instance is created");
	}

	if (AppServiceClient)
	{
		AppServiceClient->TryConnect();
	}

	// (Samil) TODO: This connection logic should be provided by the SDK itself. 
	// App developers should not be required implement 'always connect' behaviour.
	//if (!Client->IsChannelReady)
	//{
	//	Client->IsChannelReady = (GRPC_CHANNEL_READY == Client->Connect());
	//}

	 if (!CustomTimeStepBound && IsConnected())
	 {
	 	MZTimeStep = NewObject<UMZCustomTimeStep>();
	 	MZTimeStep->PluginClient = this;
	 	if (GEngine->SetCustomTimeStep(MZTimeStep.Get()))
	 	{
	 		CustomTimeStepBound = true;
	 	}
	 }
	return;
}

void FMZClient::OnBeginFrame()
{

}

void FMZClient::OnPostWorldInit(UWorld* World, const UWorld::InitializationValues initValues)
{
	TaskQueue.Enqueue([World, this]()
		{
			auto WorldContext = GEngine->GetWorldContextFromGameViewport(GEngine->GameViewport);
			if (World != WorldContext->World())
			{
				return;
			}
			if (!GEngine->GameViewport || !GEngine->GameViewport->IsStatEnabled("FPS"))
			{ 
				GEngine->Exec(World, TEXT("Stat FPS"));
			}

			IsWorldInitialized = true;

			mz::fb::TNodeStatusMessage MapNameStatus;
			MapNameStatus.text = TCHAR_TO_UTF8(*World->GetMapName());
			MapNameStatus.type = mz::fb::NodeStatusMessageType::INFO;
			UENodeStatusHandler.Add("map_name", MapNameStatus);
		});
}

void FMZClient::OnPreWorldFinishDestroy(UWorld* World)
{
	LOG("World is destroyed");

	if (!GEngine)
	{
		return;
	}
	auto WorldContext = GEngine->GetWorldContextFromGameViewport(GEngine->GameViewport);
	if (WorldContext->World())
	{
		mz::fb::TNodeStatusMessage MapNameStatus;
		MapNameStatus.text = TCHAR_TO_UTF8(*WorldContext->World()->GetMapName());
		MapNameStatus.type = mz::fb::NodeStatusMessageType::INFO;
		UENodeStatusHandler.Add("map_name", MapNameStatus);
	}
	if (World != WorldContext->World())
	{
		return;
	}
	
}

void FMZClient::StartupModule() {

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

	if (!FMediaZ::Initialize())
	{
		return;
	}

	//Add Delegates
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FMZClient::Tick));
	FWorldDelegates::OnPostWorldInitialization.AddRaw(this, &FMZClient::OnPostWorldInit);
	FWorldDelegates::OnPreWorldFinishDestroy.AddRaw(this, &FMZClient::OnPreWorldFinishDestroy);
}

void FMZClient::ShutdownModule()
{
	// AppServiceClient-/*>*/
	MZTimeStep = nullptr;
	if(FMediaZ::ShutdownClient)
	{
		FMediaZ::ShutdownClient(AppServiceClient);
	}
	FMediaZ::Shutdown();
}

bool FMZClient::Tick(float dt)
{
    TryConnect();
	while (!TaskQueue.IsEmpty()) {
		Task task;
		TaskQueue.Dequeue(task);
		task();
	}
	UENodeStatusHandler.Update();
	return true;
}

void FMZClient::OnUpdatedNodeExecuted(float deltaTime)
{
	if (MZTimeStep.IsValid())
	{
		MZTimeStep->Step(deltaTime);
	}
}

void UENodeStatusHandler::SetClient(FMZClient* _PluginClient)
{
	this->PluginClient = _PluginClient;
}

void UENodeStatusHandler::Add(std::string const& Id, mz::fb::TNodeStatusMessage const& Status)
{
	StatusMessages[Id] = Status;
	Dirty = true;
}

void UENodeStatusHandler::Remove(std::string const& Id)
{
	auto it = StatusMessages.find(Id);
	if (it != StatusMessages.end())
	{
		StatusMessages.erase(it);
		Dirty = true;
	}
}

void UENodeStatusHandler::Update()
{
	if (Dirty)
	{
		SendStatus();
	}
}

void UENodeStatusHandler::SendStatus()
{
	if (!PluginClient || !PluginClient->IsConnected())
		return;
	flatbuffers::FlatBufferBuilder Builder;
	mz::TPartialNodeUpdate UpdateRequest;
	UpdateRequest.node_id = *reinterpret_cast<mz::fb::UUID*>(&FMZClient::NodeId);
	for (auto& [_, StatusMsg] : StatusMessages)
	{
		UpdateRequest.status_messages.push_back(std::make_unique<mz::fb::TNodeStatusMessage>(StatusMsg));
	}
	auto offset = mz::CreatePartialNodeUpdate(Builder, &UpdateRequest);
	Builder.Finish(offset);
	auto buf = Builder.Release();
	auto root = flatbuffers::GetRoot<mz::PartialNodeUpdate>(buf.data());
	PluginClient->AppServiceClient->SendPartialNodeUpdate(*root);
	Dirty = false;
}

bool FPSCounter::Update(float dt)
{
	++FrameCount;
	DeltaTimeAccum += dt;
	if (DeltaTimeAccum >= 1.f)
	{
		auto Prev = FramesPerSecond;
		FramesPerSecond = 1.f / (DeltaTimeAccum / float(FrameCount));
		FramesPerSecond = std::roundf(FramesPerSecond  * 100.f) / 100.f;
		DeltaTimeAccum = 0;
		FrameCount = 0;
		return Prev != FramesPerSecond;
	}
	return false;
}

mz::fb::TNodeStatusMessage FPSCounter::GetNodeStatusMessage() const
{
	mz::fb::TNodeStatusMessage FpsStatusMessage;

	FpsStatusMessage.text.resize(32);
	::snprintf(FpsStatusMessage.text.data(), 32, "%.2f FPS", FramesPerSecond);
	FpsStatusMessage.text.shrink_to_fit();

	FpsStatusMessage.type = mz::fb::NodeStatusMessageType::INFO;
	return FpsStatusMessage;
}

#pragma optimize("", on)
#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMZClient, ClientImpl)

