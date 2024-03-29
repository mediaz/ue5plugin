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

#include "MediaZ/AppAPI.h"
#include <uuid.h>
#include "mzFlatBuffersCommon.h"
#include "AppEvents_generated.h"
#include <mzFlatBuffersCommon.h>
#include <functional> 

struct PinDataQueue : public TQueue<TPair<mz::Buffer, uint32_t>>
{
	bool LiveNow = true;

	void DiscardExcessThenDequeue(TPair<mz::Buffer, uint32_t>& result, uint32_t requestedFrameNumber, bool wait)
	{
		u32 tryCount = 0;
		bool dequeued = false;
		bool oldLiveNow = LiveNow;
		FPlatformProcess::ConditionalSleep([&]()
			{
				while (Dequeue(result))
				{
					LiveNow = true;
					dequeued = true;
					if (result.Value >= requestedFrameNumber)
						return true;
				}

				return !LiveNow || !wait || tryCount++ > 20;
			}, 0.001f);
		
		LiveNow = dequeued;
		if (oldLiveNow != LiveNow)
			UE_LOG(LogCore, Warning, TEXT("LiveNow Changed"));

		if (LiveNow && result.Value != requestedFrameNumber)
			UE_LOG(LogCore, Warning, TEXT("Mismatch between popped frame number and requested frame number: %i, %i"), result.Value, requestedFrameNumber);
	}
};

class PinDataQueues : public mz::app::IEventDelegates
{
public:
	virtual ~PinDataQueues() {}

	PinDataQueue* GetAddQueue(mz::fb::UUID const& pinId)
	{
		uuids::uuid id(pinId.bytes()->begin(), pinId.bytes()->end());

		std::scoped_lock<std::mutex> lock(Guard);
		return &Queues[id];
	}

	virtual void OnPinValueChanged(mz::fb::UUID const& pinId, uint8_t const* data, size_t size, bool reset, uint32_t frameNumber) override
	{
		if (reset)
		{
			std::scoped_lock<std::mutex> lock(Guard);
			for (auto& [_, queue] : Queues)
				queue.Empty();

			return;
		}

		auto queue = GetAddQueue(pinId);
		queue->Enqueue({ mz::Buffer(data, size), frameNumber });
	}

	mz::Buffer Pop(mz::fb::UUID const& pinId, bool wait, uint32_t frameNumber)
	{
		auto queue = GetAddQueue(pinId);

		TPair<mz::Buffer, uint32_t> result;
		queue->DiscardExcessThenDequeue(result, frameNumber, wait);
		return result.Key;
	}

	std::mutex Guard;
	std::unordered_map<uuids::uuid, PinDataQueue> Queues;
};


class UMZCustomTimeStep;
typedef std::function<void()> Task;

DECLARE_LOG_CATEGORY_EXTERN(LogMZClient, Log, All);

//events coming from mediaz
DECLARE_EVENT_OneParam(FMZClient, FMZNodeConnected, mz::fb::Node const*);
DECLARE_EVENT_OneParam(FMZClient, FMZNodeUpdated, mz::fb::Node const&);
DECLARE_EVENT_OneParam(FMZClient, FMZContextMenuRequested, mz::ContextMenuRequest const&);
DECLARE_EVENT_OneParam(FMZClient, FMZContextMenuCommandFired, mz::ContextMenuAction const&);
DECLARE_EVENT(FMZClient, FMZNodeRemoved);
DECLARE_EVENT_FourParams(FMZClient, FMZPinValueChanged, mz::fb::UUID const&, uint8_t const*, size_t, bool);
DECLARE_EVENT_TwoParams(FMZClient, FMZPinShowAsChanged, mz::fb::UUID const&, mz::fb::ShowAs);
DECLARE_EVENT_TwoParams(FMZClient, FMZFunctionCalled, mz::fb::UUID const&, mz::fb::Node const&);
DECLARE_EVENT_OneParam(FMZClient, FMZNodeSelected, mz::fb::UUID const&);
DECLARE_EVENT_OneParam(FMZClient, FMZNodeImported, mz::fb::Node const&);
DECLARE_EVENT(FMZClient, FMZConnectionClosed);

// DECLARE_EVENT_OneParam(FMZClient, FMZConsoleCommandExecuted, FString);

/**
 * Implements communication with the MediaZ Engine
 */
class FMZClient;

class MZCLIENT_API MZEventDelegates : public PinDataQueues
{
public:
	~MZEventDelegates() {}

	virtual void OnAppConnected(mz::fb::Node const* appNode) override;
	virtual void OnNodeUpdated(mz::fb::Node const& appNode) override;
	virtual void OnContextMenuRequested(mz::ContextMenuRequest const& request) override;
	virtual void OnContextMenuCommandFired(mz::ContextMenuAction const& action) override;
	virtual void OnNodeRemoved() override;
	virtual void OnPinValueChanged(mz::fb::UUID const& pinId, uint8_t const* data, size_t size, bool reset, uint32_t frameNumber) override;
	virtual void OnPinShowAsChanged(mz::fb::UUID const& pinId, mz::fb::ShowAs newShowAs) override;
	virtual void OnExecuteAppInfo(mz::app::AppExecuteInfo const* appExecuteInfo) override; 
	virtual void OnFunctionCall(mz::fb::UUID const& nodeId, mz::fb::Node const& function) override;
	virtual void OnNodeSelected(mz::fb::UUID const& nodeId) override;
	virtual void OnNodeImported(mz::fb::Node const& appNode) override;
	virtual void OnConnectionClosed() override;
	virtual void OnStateChanged(mz::app::ExecutionState newState) override;
	virtual void OnConsoleCommand(mz::app::ConsoleCommand const* consoleCommand) override;
	virtual void OnConsoleAutoCompleteSuggestionRequest(mz::app::ConsoleAutoCompleteSuggestionRequest const* consoleAutoCompleteSuggestionRequest) override;
	virtual void OnLoadNodesOnPaths(mz::LoadNodesOnPaths const* loadNodesOnPathsRequest) override;
	virtual void OnCloseApp() override;

	FMZClient* PluginClient;
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

class MZCLIENT_API FMediaZ
{
public:
	static bool Initialize();
	static void Shutdown();
	static mz::app::FN_MakeAppServiceClient* MakeAppServiceClient;
	static mz::app::FN_ShutdownClient* ShutdownClient;
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
	void OnUpdatedNodeExecuted(mz::fb::vec2u deltaSeconds);

	bool ExecuteConsoleCommand(const TCHAR* Input);

	bool ExecInternal(const TCHAR* Input);
	
	//Grpc client to communicate
	TSharedPtr<MZEventDelegates> EventDelegates = 0;

	//To send events to mediaz and communication
	mz::app::IAppServiceClient* AppServiceClient = nullptr;

	//Task queue
	TQueue<Task, EQueueMode::Mpsc> TaskQueue;

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
	FMZFunctionCalled OnMZFunctionCalled;
	FMZNodeSelected OnMZNodeSelected;
	FMZNodeImported OnMZNodeImported;
	FMZConnectionClosed OnMZConnectionClosed;
	TMulticastDelegate<void(mz::app::ExecutionState), FDefaultTSDelegateUserPolicy> OnMZStateChanged_GRPCThread;
	TMulticastDelegate<void(const TArray<FString>&), FDefaultTSDelegateUserPolicy> OnMZLoadNodesOnPaths;
	// FMZConsoleCommandExecuted OnMZConsoleCommandExecuted;
	
	UENodeStatusHandler UENodeStatusHandler;

	int ReloadingLevel = 0;
	
protected:
	void Reset();

	FPSCounter FPSCounter;
	bool IsWorldInitialized = false;

};

class MZConsoleOutput : public FOutputDevice
{
public:
	FMZClient* MZClient;
	MZConsoleOutput(FMZClient* MZClient)
		: FOutputDevice(), MZClient(MZClient)
	{
	}

	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category) override
	{
		if (!MZClient || FString(V).IsEmpty())
		{
			return;
		}
		
		flatbuffers::FlatBufferBuilder mb;
		auto offset = mz::CreateAppEventOffset(mb ,mz::app::CreateConsoleOutputDirect(mb, TCHAR_TO_UTF8(V)));
		mb.Finish(offset);
		auto buf = mb.Release();
		auto root = flatbuffers::GetRoot<mz::app::AppEvent>(buf.data());
		MZClient->AppServiceClient->Send(*root);
	}
};


