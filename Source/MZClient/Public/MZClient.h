/*
 * Copyright MediaZ AS. All Rights Reserved.
 */

#pragma once

#include "Engine/EngineTypes.h"

#include "CoreMinimal.h"
#include <numeric>
#include "Logging/LogMacros.h"

#pragma warning (disable : 4800)
#pragma warning (disable : 4668)

#include "Mediaz/PinDataQueues.h"
#include "MediaZ/AppAPI.h"
//#include "MediaZ/MediaZ.h"
#include "AppEvents_generated.h"
#include <mzFlatBuffersCommon.h>
#include <functional> 

class UMZCustomTimeStep;
typedef std::function<void()> Task;

DECLARE_LOG_CATEGORY_EXTERN(LogMZClient, Log, All);

//events coming from mediaz
DECLARE_EVENT_OneParam(FMZClient, FMZNodeConnected, mz::fb::Node const&);
DECLARE_EVENT_OneParam(FMZClient, FMZNodeUpdated, mz::fb::Node const&);
DECLARE_EVENT_OneParam(FMZClient, FMZContextMenuRequested, mz::ContextMenuRequest const&);
DECLARE_EVENT_OneParam(FMZClient, FMZContextMenuCommandFired, mz::ContextMenuAction const&);
DECLARE_EVENT(FMZClient, FMZNodeRemoved);
DECLARE_EVENT_FourParams(FMZClient, FMZPinValueChanged, mz::fb::UUID const&, uint8_t const*, size_t, bool);
DECLARE_EVENT_TwoParams(FMZClient, FMZPinShowAsChanged, mz::fb::UUID const&, mz::fb::ShowAs);
DECLARE_EVENT_OneParam(FMZClient, FMZExecutedApp, mz::app::AppExecute const&);
DECLARE_EVENT_TwoParams(FMZClient, FMZFunctionCalled, mz::fb::UUID const&, mz::fb::Node const&);
DECLARE_EVENT_OneParam(FMZClient, FMZNodeSelected, mz::fb::UUID const&);
DECLARE_EVENT_OneParam(FMZClient, FMZNodeImported, mz::fb::Node const&);
DECLARE_EVENT_OneParam(FMZClient, FMZStateChanged, mz::app::ExecutionState);
DECLARE_EVENT(FMZClient, FMZConnectionClosed);


/**
 * Implements communication with the MediaZ Engine
 */
class FMZClient;

class MZCLIENT_API MZEventDelegates : public mz::app::PinDataQueues
{
public:
	~MZEventDelegates() {}

	virtual void OnAppConnected(mz::fb::Node const& appNode) override;
	virtual void OnNodeUpdated(mz::fb::Node const& appNode) override;
	virtual void OnContextMenuRequested(mz::ContextMenuRequest const& request) override;
	virtual void OnContextMenuCommandFired(mz::ContextMenuAction const& action) override;
	virtual void OnNodeRemoved() override;
	virtual void OnPinValueChanged(mz::fb::UUID const& pinId, uint8_t const* data, size_t size, bool reset) override;
	virtual void OnPinShowAsChanged(mz::fb::UUID const& pinId, mz::fb::ShowAs newShowAs) override;
	virtual void OnExecuteApp(mz::app::AppExecute const& appExecute) override; 
	virtual void OnFunctionCall(mz::fb::UUID const& nodeId, mz::fb::Node const& function) override;
	virtual void OnNodeSelected(mz::fb::UUID const& nodeId) override;
	virtual void OnNodeImported(mz::fb::Node const& appNode) override;
	virtual void OnConnectionClosed() override;
	virtual void OnStateChanged(mz::app::ExecutionState newState) override;


	FMZClient* PluginClient;
	// (Samil:) Will every app client have one node attached to it?
	// If so we can move this node ID to mediaZ SDK.
	//std::atomic_bool IsChannelReady = false;
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
using PFN_ShutdownClient = decltype(&mz::app::ShutdownClient);
using PFN_mzGetD3D12Resources = decltype(&mzGetD3D12Resources);

class MZCLIENT_API FMediaZ
{
public:
	static bool Initialize();
	static void Shutdown();
	static PFN_MakeAppServiceClient MakeAppServiceClient;
	static PFN_ShutdownClient ShutdownClient;
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

	void OnBeginFrame();

	//Called when the level is initiated
	void OnPostWorldInit(UWorld* World, const UWorld::InitializationValues InitValues);
	 
	//Called when the level destruction began
	void OnPreWorldFinishDestroy(UWorld* World);

	//Called when the node is executed from mediaZ
	void OnUpdatedNodeExecuted(float deltaTime);
	
	//Grpc client to communicate
	TSharedPtr<MZEventDelegates> EventDelegates = 0;

	//To send events to mediaz and communication
	mz::app::IAppServiceClient* AppServiceClient = nullptr;

	//Task queue
	TQueue<Task, EQueueMode::Mpsc> TaskQueue;
	TQueue<mz::app::TAppExecute, EQueueMode::Mpsc> ExecuteQueue;

	//Custom time step implementation for mediaZ controlling the unreal editor in play mode
	UPROPERTY()
	TWeakObjectPtr<UMZCustomTimeStep> MZTimeStep = nullptr;
	bool CustomTimeStepBound = false;

	// MediaZ root node id
	static FGuid NodeId;
	// The app key we are using for MediaZ
	static FString AppKey;

	TMap<FGuid, FName> PathUpdates;

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
	FMZStateChanged OnMZStateChanged;

	UENodeStatusHandler UENodeStatusHandler;
protected:
	void Reset();

	FPSCounter FPSCounter;
	bool IsWorldInitialized = false;

};



