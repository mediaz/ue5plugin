#pragma once

#include "Engine/EngineTypes.h"

#include "CoreMinimal.h"
#include <numeric>
#include "Logging/LogMacros.h"

#pragma warning (disable : 4800)
#pragma warning (disable : 4668)

#include "MediaZ/AppInterface.h"
#include "MediaZ/MediaZ.h"
#include "AppEvents_generated.h"
#include <mzFlatBuffersCommon.h>
#include <functional> 

typedef std::function<void()> Task;

DECLARE_LOG_CATEGORY_EXTERN(LogMediaZ, Log, All);

//events coming from mediaz
DECLARE_EVENT_OneParam(FMZClient, FMZNodeConnected, mz::fb::Node const&);
DECLARE_EVENT_OneParam(FMZClient, FMZNodeUpdated, mz::fb::Node const&);
DECLARE_EVENT_OneParam(FMZClient, FMZContextMenuRequested, mz::ContextMenuRequest const&);
DECLARE_EVENT_OneParam(FMZClient, FMZContextMenuCommandFired, mz::ContextMenuAction const&);
DECLARE_EVENT(FMZClient, FMZNodeRemoved);
DECLARE_EVENT_ThreeParams(FMZClient, FMZPinValueChanged, mz::fb::UUID const&, uint8_t const*, size_t);
DECLARE_EVENT_TwoParams(FMZClient, FMZPinShowAsChanged, mz::fb::UUID const&, mz::fb::ShowAs);
DECLARE_EVENT_OneParam(FMZClient, FMZExecutedApp, mz::app::AppExecute const&);
DECLARE_EVENT_TwoParams(FMZClient, FMZFunctionCalled, mz::fb::UUID const&, mz::fb::Node const&);
DECLARE_EVENT_OneParam(FMZClient, FMZNodeSelected, mz::fb::UUID const&);
DECLARE_EVENT_OneParam(FMZClient, FMZNodeImported, mz::fb::Node const&);
DECLARE_EVENT(FMZClient, FMZConnectionClosed);



/**
 * Implements communication with the MediaZ Engine
 */
class FMZClient;

class MZCLIENT_API MZEventDelegates : public mz::app::IEventDelegates
{
public:
	virtual void OnAppConnected(mz::fb::Node const& appNode) override;
	virtual void OnNodeUpdated(mz::fb::Node const& appNode) override;
	virtual void OnContextMenuRequested(mz::ContextMenuRequest const& request) override;
	virtual void OnContextMenuCommandFired(mz::ContextMenuAction const& action) override;
	virtual void OnNodeRemoved() override;
	virtual void OnPinValueChanged(mz::fb::UUID const& pinId, uint8_t const* data, size_t size) override;
	virtual void OnPinShowAsChanged(mz::fb::UUID const& pinId, mz::fb::ShowAs newShowAs) override;
	virtual void OnExecuteApp(mz::app::AppExecute const& appExecute) override; 
	virtual void OnFunctionCall(mz::fb::UUID const& nodeId, mz::fb::Node const& function) override;
	virtual void OnNodeSelected(mz::fb::UUID const& nodeId) override;
	virtual void OnNodeImported(mz::fb::Node const& appNode) override;
	virtual void OnConnectionClosed() override;


	FMZClient* PluginClient;
	// (Samil:) Will every app client have one node attached to it?
	// If so we can move this node ID to mediaZ SDK.
	//std::atomic_bool IsChannelReady = false;
};

class MZCLIENT_API ContextMenuActions
{
public:
	TArray< TPair<FString, std::function<void(AActor*)>> >  ActorMenu;
	TArray< TPair<FString, Task> >  FunctionMenu;
	TArray< TPair<FString, Task> >  PropertyMenu;

	std::vector<flatbuffers::Offset<mz::ContextMenuItem>> SerializeActorMenuItems(flatbuffers::FlatBufferBuilder& fbb)
	{
		std::vector<flatbuffers::Offset<mz::ContextMenuItem>> result;
		int command = 0;
		for (auto item : ActorMenu)
		{
			result.push_back(mz::CreateContextMenuItemDirect(fbb, TCHAR_TO_UTF8(*item.Key), command++, 0));
		}
		return result;
	}

	ContextMenuActions()
	{
		TPair<FString, std::function<void(AActor*)> > deleteAction(FString("Delete"), [](AActor* actor)
			{
				//actor->Destroy();
				actor->GetWorld()->EditorDestroyActor(actor, false);
			});
		ActorMenu.Add(deleteAction);
	}

	void ExecuteActorAction(uint32 command, AActor* actor)
	{
		if (ActorMenu.IsValidIndex(command))
		{
			ActorMenu[command].Value(actor);
		}
	}
};

class UENodeStatusHandler
{
public:
	void SetClient(FMZClient* PluginClient);
	void Add(std::string const& Id, mz::fb::TNodeStatusMessage const& Status);
	void Remove(std::string const& Id);
	void Update();
private:
	void SendStatus();
	FMZClient* PluginClient = nullptr;
	std::unordered_map<std::string, mz::fb::TNodeStatusMessage> StatusMessages;
	bool Dirty = false;
};

class FPSCounter
{
public:
	bool Update(float dt);
	mz::fb::TNodeStatusMessage GetNodeStatusMessage() const;
private:
	float DeltaTimeAccum = 0;
	uint64_t FrameCount = 0;
	float FramesPerSecond = 0;
};

using PFN_MakeAppServiceClient = decltype(&mz::app::MakeAppServiceClient);
using PFN_mzGetD3D12Resources = decltype(&mzGetD3D12Resources);

class MZCLIENT_API FMediaZ
{
public:
	static bool Initialize();
	static void Shutdown();
	static PFN_MakeAppServiceClient MakeAppServiceClient;
	static PFN_mzGetD3D12Resources GetD3D12Resources;
private:
	// MediaZ SDK DLL handle
	static void* LibHandle;
};

class MZCLIENT_API FMZClient : public IModuleInterface {

public:
	 
	//Empty constructor
	FMZClient();

	//Called on startup of the module on Unreal Engine start
	virtual void StartupModule() override;

	//Called on shutdown of the module on Unreal Engine exit
	virtual void ShutdownModule() override;

	//This function is called when the connection with the MediaZ Engine is started
	virtual void Connected();

	//This function is called when the connection with the MediaZ Engine is finished
	virtual void Disconnected();
	 
	/// @return Connection status with MediaZ Engine
	virtual bool IsConnected();

	//Tries to initialize connection with the MediaZ engine
	void TryConnect();

	//Tick is called every frame once and handles the tasks queued from grpc threads
	bool Tick(float dt);

	//Test action to test wheter debug menu works
	void TestAction();

	//Called when the level is initiated
	void OnPostWorldInit(UWorld* World, const UWorld::InitializationValues InitValues);
	 
	//Called when the level destruction began
	void OnPreWorldFinishDestroy(UWorld* World);

	//Called when the node is executed from mediaZ
	void OnUpdatedNodeExecuted();

	//delegate called when a actors folder path is changed
	void OnActorFolderChanged(const AActor* actor, FName oldPath);

	//delegate called when actor outer object changed
	void OnActorOuterChanged(AActor* actor, UObject* OldOuter);
	
	//Grpc client to communicate
	TSharedPtr<MZEventDelegates> EventDelegates = 0;

	//To send events to mediaz and communication
	TSharedPtr<mz::app::IAppServiceClient> AppServiceClient = nullptr;

	//Task queue
	TQueue<Task, EQueueMode::Mpsc> TaskQueue;

	//Custom time step implementation for mediaZ controlling the unreal editor in play mode
	class UMZCustomTimeStep* MZTimeStep = nullptr;
	bool CustomTimeStepBound = false;

	//MediaZ root node id
	static FGuid NodeId;

	TMap<FGuid, FName> PathUpdates;

	//TODO consider adding The world we currently see
	//static TObjectPtr<UWorld> sceneWorld;

	//EXPERIMENTAL START
	FMZNodeConnected OnMZConnected;
	FMZNodeUpdated OnMZNodeUpdated;
	FMZContextMenuRequested OnMZContextMenuRequested;
	FMZContextMenuCommandFired OnMZContextMenuCommandFired;
	FMZNodeRemoved OnMZNodeRemoved;
	FMZPinValueChanged OnMZPinValueChanged;
	FMZPinShowAsChanged OnMZPinShowAsChanged;
	FMZExecutedApp OnMZExecutedApp;
	FMZFunctionCalled OnMZFunctionCalled;
	FMZNodeSelected OnMZNodeSelected;
	FMZNodeImported OnMZNodeImported;
	FMZConnectionClosed OnMZConnectionClosed;

protected:
	void Reset();

	FPSCounter FPSCounter;
	UENodeStatusHandler UENodeStatusHandler;
	bool IsWorldInitialized = false;

};



