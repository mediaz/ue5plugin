// Copyright MediaZ AS. All Rights Reserved.

#include "MZTextureShareManager.h"

#include "HardwareInfo.h"

#pragma warning (disable : 4800)
#pragma warning (disable : 4668)

#include "Windows/AllowWindowsPlatformTypes.h"
#pragma intrinsic(_InterlockedCompareExchange64)
#define InterlockedCompareExchange64 _InterlockedCompareExchange64
#include <d3d12.h>
#include "Windows/HideWindowsPlatformTypes.h"

#include "D3D12RHIPrivate.h"
#include "D3D12RHI.h"
#include "D3D12Resources.h"
#include "ID3D12DynamicRHI.h"
#include "RHI.h"
#include "MZActorProperties.h"

#include "MZClient.h"

#include <Builtins_generated.h>



MZTextureShareManager* MZTextureShareManager::singleton;


mzTextureInfo GetResourceInfo(MZProperty* mzprop)
{
	UObject* obj = mzprop->GetRawObjectContainer();
	FObjectProperty* prop = CastField<FObjectProperty>(mzprop->Property);

	if (!obj)
	{
		return mzTextureInfo{
			.Width = 1600,
			.Height = 900,
			.Format = mzFormat::MZ_FORMAT_R16G16B16A16_SFLOAT,
			.Usage = (mzImageUsage)(MZ_IMAGE_USAGE_RENDER_TARGET | MZ_IMAGE_USAGE_SAMPLED | MZ_IMAGE_USAGE_TRANSFER_SRC | MZ_IMAGE_USAGE_TRANSFER_DST) 
		};
	}

	UTextureRenderTarget2D* trt2d = Cast<UTextureRenderTarget2D>(prop->GetObjectPropertyValue(prop->ContainerPtrToValuePtr<UTextureRenderTarget2D>(obj)));

	if (!trt2d)
	{
		return mzTextureInfo{
			.Width = 1600,
			.Height = 900,
			.Format = mzFormat::MZ_FORMAT_R16G16B16A16_SFLOAT,
			.Usage = (mzImageUsage)(MZ_IMAGE_USAGE_RENDER_TARGET | MZ_IMAGE_USAGE_SAMPLED | MZ_IMAGE_USAGE_TRANSFER_SRC | MZ_IMAGE_USAGE_TRANSFER_DST)
		};
	}

	mzTextureInfo info = {
		.Width = (uint32_t)trt2d->GetSurfaceWidth(),
		.Height = (uint32_t)trt2d->GetSurfaceHeight(),
		.Usage = (mzImageUsage)(MZ_IMAGE_USAGE_RENDER_TARGET | MZ_IMAGE_USAGE_SAMPLED | MZ_IMAGE_USAGE_TRANSFER_SRC | MZ_IMAGE_USAGE_TRANSFER_DST),
	};

	switch (trt2d->RenderTargetFormat)
	{
	case ETextureRenderTargetFormat::RTF_R8:
		info.Format = MZ_FORMAT_R8_UNORM;
		break;
	case ETextureRenderTargetFormat::RTF_RG8:
		info.Format = MZ_FORMAT_R8G8_UNORM;
		break;
	case ETextureRenderTargetFormat::RTF_RGBA8:
		info.Format = MZ_FORMAT_R8G8B8A8_UNORM;
		break;
	case ETextureRenderTargetFormat::RTF_RGBA8_SRGB:
		info.Format = MZ_FORMAT_R8G8B8A8_SRGB;
		break;

	case ETextureRenderTargetFormat::RTF_R16f:
		info.Format = MZ_FORMAT_R16_SFLOAT;
		break;
	case ETextureRenderTargetFormat::RTF_RG16f:
		info.Format = MZ_FORMAT_R16G16_SFLOAT;
		break;
	case ETextureRenderTargetFormat::RTF_RGBA16f:
		info.Format = MZ_FORMAT_R16G16B16A16_SFLOAT;
		break;

	case ETextureRenderTargetFormat::RTF_R32f:
		info.Format = MZ_FORMAT_R32_SFLOAT;
		break;
	case ETextureRenderTargetFormat::RTF_RG32f:
		info.Format = MZ_FORMAT_R32G32_SFLOAT;
		break;
	case ETextureRenderTargetFormat::RTF_RGBA32f:
		info.Format = MZ_FORMAT_R32G32B32A32_SFLOAT;
		break;

	case ETextureRenderTargetFormat::RTF_RGB10A2:
		info.Format = MZ_FORMAT_A2R10G10B10_UNORM_PACK32;
		break;
	}

	return info;
}

MZTextureShareManager::MZTextureShareManager()
{
	Initiate();
}

MZTextureShareManager* MZTextureShareManager::GetInstance()
{
	if (singleton == nullptr) {
		singleton = new MZTextureShareManager();
	}
	return singleton;
}

MZTextureShareManager::~MZTextureShareManager()
{
}

mz::fb::TTexture MZTextureShareManager::AddTexturePin(MZProperty* mzprop)
{
	mzTextureInfo info = GetResourceInfo(mzprop);
	
	UObject* obj = mzprop->GetRawObjectContainer();
	FObjectProperty* prop = CastField<FObjectProperty>(mzprop->Property);
	UTextureRenderTarget2D* trt2d = Cast<UTextureRenderTarget2D>(prop->GetObjectPropertyValue(prop->ContainerPtrToValuePtr<UTextureRenderTarget2D>(obj)));
	auto TargetFormat = static_cast<DXGI_FORMAT>(GPixelFormats[trt2d->GetFormat()].PlatformFormat);
	// Shared heaps are not supported on CPU-accessible heaps
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Alignment        = 0;
	desc.Width            = info.Width;
	desc.Height           = info.Height;
	desc.DepthOrArraySize = 1;
	desc.MipLevels        = 1;
	desc.Format           = TargetFormat;
	desc.SampleDesc.Count = 1;
	desc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	ID3D12Resource* res;
	HANDLE handle;
	D3D12_HEAP_PROPERTIES props = {.Type = D3D12_HEAP_TYPE_DEFAULT};
	auto state = D3D12_RESOURCE_STATE_COMMON;
	MZ_D3D12_ASSERT_SUCCESS(Dev->CreateCommittedResource(&props, D3D12_HEAP_FLAG_SHARED, &desc, state, 0, IID_PPV_ARGS(&res)));
	MZ_D3D12_ASSERT_SUCCESS(Dev->CreateSharedHandle(res, 0, GENERIC_ALL, 0, &handle));
	//res->Release();

	mz::fb::TTexture tex;
	tex.size = mz::fb::SizePreset::CUSTOM;
	tex.width = info.Width;
	tex.height = info.Height;
	tex.format = mz::fb::Format(info.Format);
	tex.usage = mz::fb::ImageUsage(info.Usage) | mz::fb::ImageUsage::SAMPLED;
	tex.type = 0x00000040;
	tex.memory = (u64)handle;
	tex.pid = FPlatformProcess::GetCurrentProcessId();
	tex.unmanaged = true;
	tex.offset = 0;
	tex.handle = 0;
	tex.semaphore = 0;
	
	ResourceInfo copyInfo = {
		.SrcMzp = mzprop,
		.DstResource = res,
		.ShowAs = mzprop->PinShowAs,
	};
	
	{
		//start property pins as output pins
		Copies.Add(mzprop, copyInfo);
	}
	return tex;
}

void MZTextureShareManager::UpdateTexturePin(MZProperty* mzprop, mz::fb::ShowAs RealShowAs)
{
	UpdatePinShowAs(mzprop, RealShowAs);

#if 0 
	// since we are using the texture pin data queue for frame counters,
	// this part assuming it's a fb::TTexture is obsolete



	//TODO better update handling (texture size etc.)
	mz::fb::Texture const* tex = flatbuffers::GetRoot<mz::fb::Texture>(data);
	auto curtex = flatbuffers::GetRoot<mz::fb::Texture>(mzprop->data.data());
	auto pid = tex->pid();
	if(pid != (uint64_t)FPlatformProcess::GetCurrentProcessId() || (tex->handle() == curtex->handle() && tex->memory() == curtex->memory() && tex->offset() == curtex->offset() && !PendingCopyQueue.Contains(mzprop->Id)))
	{
		return;
	}
	

	mzprop->data.resize(size);
	memcpy(mzprop->data.data(), data, size);
	//std::unique_lock lock(CopyOnTickMutex);
	mzTextureShareInfo info = {
		.Memory = {
			.Type = tex->type(),
			.Handle = tex->handle(),
			.PID = tex->pid(),
			.Memory = tex->memory(),
			.Offset = tex->offset(),
		},
		.Info = {
			.Width = tex->width(),
			.Height = tex->height(),
			.Format = (mzFormat)tex->format(),
			.Usage =  (mzImageUsage)tex->usage()},
	};
	ResourceInfo copyInfo = {
		.SrcMzp = mzprop,
		// .Info = info,
	};

	if (mzResult::MZ_RESULT_SUCCESS != FMediaZ::GetD3D12Resources(&info, Dev, &copyInfo.DstResource))
	{
		abort();
	}

	UObject* obj = mzprop->GetRawObjectContainer();
	if (!obj) return;
	auto prop = CastField<FObjectProperty>(mzprop->Property);
	if (!prop) return;
	auto URT = Cast<UTextureRenderTarget2D>(prop->GetObjectPropertyValue(prop->ContainerPtrToValuePtr<UTextureRenderTarget2D>(obj)));
	if (!URT) return;
	{
		if(PendingCopyQueue.Contains(mzprop->Id))
		{
			PendingCopyQueue.Remove(mzprop->Id);
			CopyOnTick.Add(mzprop, copyInfo);
		}
		else if(CopyOnTick.Contains(mzprop))
		{
			auto resource = CopyOnTick.FindRef(mzprop).DstResource;
			if(resource && copyInfo.DstResource != resource)
			{
				ResourcesToDelete.Add(resource);
			}
			CopyOnTick.Add(mzprop, copyInfo);
		}
	}
#endif
}

void MZTextureShareManager::UpdatePinShowAs(MZProperty* MzProperty, mz::fb::ShowAs NewShowAs)
{
	if(Copies.Contains(MzProperty))
	{
		auto resourceInfo = Copies.Find(MzProperty);
		resourceInfo->ShowAs = NewShowAs;
	}
}

void MZTextureShareManager::WaitCommands()
{
}


void MZTextureShareManager::ExecCommands(CmdStruct* cmdData, mz::fb::ShowAs CopyShowAs, TMap<ID3D12Fence*, u64>& SignalGroup)
{
	cmdData->CmdList->Close();
	CmdQueue->ExecuteCommandLists(1, (ID3D12CommandList**)&cmdData->CmdList);
	CmdQueue->Signal(cmdData->CmdFence, ++cmdData->CmdFenceValue);
	
	for(auto& [fence, val] : SignalGroup)
	{
		CmdQueue->Signal(fence, val);
	}
	
	// cmdData->CmdList->Reset(CmdAlloc, 0);
	cmdData->State = CmdState::Running;
#if 0
	{
		if (cmdData->CmdFence->GetCompletedValue() < cmdData->CmdFenceValue)
		{
			HANDLE CmdEvent = CreateEventA(0, 0, 0, 0);
			cmdData->CmdFence->SetEventOnCompletion(cmdData->CmdFenceValue, CmdEvent);
			WaitForSingleObject(CmdEvent, INFINITE);
		}
		// cmdData->CmdAlloc->Reset();
		// cmdData->CmdList->Reset(cmdData->CmdAlloc, 0);
	}
#endif
}

void MZTextureShareManager::TextureDestroyed(MZProperty* textureProp)
{
	Copies.Remove(textureProp);
	
	//TODO delete real resource	
}

static HANDLE DupeHandle(uint64_t pid, HANDLE handle)
{
    HANDLE re = 0;
    HANDLE src = OpenProcess(GENERIC_ALL, false, pid);
    HANDLE cur = GetCurrentProcess();
    if (!DuplicateHandle(src, handle, cur, &re, GENERIC_ALL, 0, DUPLICATE_SAME_ACCESS))
    {
        return 0;
    }
    CloseHandle(src);
    return re;
}

static bool ImportSharedFence(uint64_t pid, HANDLE handle, ID3D12Device* pDevice, ID3D12Fence** pFence)
{
       HANDLE xmemory = DupeHandle(pid, handle);
       if (FAILED(pDevice->OpenSharedHandle(xmemory, IID_PPV_ARGS(pFence))))
       {
               return false;
       }
       CloseHandle(xmemory);
       return true;
}

void MZTextureShareManager::AllocateCommandLists()
{
	for(size_t i = 0; i < CommandListCount; i ++)
	{
		ID3D12GraphicsCommandList* cmdList;
		ID3D12Fence* cmdFence;
		ID3D12CommandAllocator* cmdAlloc;
		HRESULT re = Dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc));
		Dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc, 0, IID_PPV_ARGS(&cmdList));
		Dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&cmdFence));
		cmdList->Close();
		// cmdList->Reset(CmdAlloc, 0);
		auto cmdData = new CmdStruct;
		cmdData->CmdAlloc = cmdAlloc;
		cmdData->CmdList = cmdList;
		cmdData->CmdFence = cmdFence;
		cmdData->State = CmdState::Pending;
		cmdData->CmdFenceValue = 0;
		Cmds.push_back(cmdData);
	}
}

CmdStruct* MZTextureShareManager::GetNewCommandList()
{
	static int i = 0;
	i = (i + 1) % CommandListCount;
	
	if(Cmds[i]->State == CmdState::Pending)
	{
		Cmds[i]->CmdList->Reset(Cmds[i]->CmdAlloc, 0);
		Cmds[i]->State = Recording;
		return Cmds[i];
	}
	else if(Cmds[i]->State == CmdState::Running)
	{
		if (Cmds[i]->CmdFence->GetCompletedValue() < Cmds[i]->CmdFenceValue)
		{
			HANDLE CmdEvent = CreateEventA(0, 0, 0, 0);
			Cmds[i]->CmdFence->SetEventOnCompletion(Cmds[i]->CmdFenceValue, CmdEvent);
			WaitForSingleObject(CmdEvent, INFINITE);
		}
		Cmds[i]->CmdAlloc->Reset();
		Cmds[i]->CmdList->Reset(Cmds[i]->CmdAlloc, 0);
		Cmds[i]->State = CmdState::Recording;
		return Cmds[i];
	}
	else
	{
		abort();
		return NULL;
	}
}

bool MZCopyTexture_RenderThread(mz::fb::ShowAs CopyShowAs, UTextureRenderTarget2D* RenderTarget, ID3D12Resource* PinTexture, ID3D12GraphicsCommandList* CmdList, TArray<D3D12_RESOURCE_BARRIER>& Barriers)
{
	auto rt = RenderTarget->GetRenderTargetResource();
	if (!rt) return false;
	auto RHIResource = rt->GetTexture2DRHI();

	if (!RHIResource || !RHIResource->IsValid())
	{
		return false;
	}
	FRHITexture* RHITexture = RHIResource;
	FD3D12Texture* Result((FD3D12Texture*)RHITexture->GetTextureBaseRHI());
	FD3D12Texture* Base = Result;

	ID3D12Resource* SrcResource = Base->GetResource()->GetResource();
	ID3D12Resource* DstResource = PinTexture;
	D3D12_RESOURCE_DESC SrcDesc = SrcResource->GetDesc();
	D3D12_RESOURCE_DESC DstDesc = DstResource->GetDesc();

	if (SrcResource == DstResource)
	{
		return false;
	}


	if(CopyShowAs == mz::fb::ShowAs::INPUT_PIN)
	{
		Swap(SrcResource, DstResource);
		D3D12_RESOURCE_BARRIER barrier = {
			.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
			.Transition = {
				.pResource = SrcResource,
				.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
				.StateBefore = D3D12_RESOURCE_STATE_COMMON,
				.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE,
			}
		};
		CmdList->ResourceBarrier(1, &barrier);

		D3D12_RESOURCE_BARRIER barrier2 = {
                			.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                			.Transition = {
                				.pResource = DstResource,
                				.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                				.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET,
                				.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST,
                			}
                		};
		CmdList->ResourceBarrier(1, &barrier2);
		CmdList->CopyResource(DstResource, SrcResource);
		D3D12_RESOURCE_BARRIER barrier3 = {
        			.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        			.Transition = {
        				.pResource = DstResource,
        				.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        				.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST,
        				.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET,
        			}
        		};
		
		CmdList->ResourceBarrier(1, &barrier3);
		// Swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
		// Barriers.Add(barrier);
	}
	else if (CopyShowAs == mz::fb::ShowAs::OUTPUT_PIN)
	{
		D3D12_RESOURCE_BARRIER barrier = {
			.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
			.Transition = {
				.pResource = SrcResource,
				.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
				.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET,
				.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE,
			}
		};
		CmdList->ResourceBarrier(1, &barrier);
		CmdList->CopyResource(DstResource, SrcResource);
		D3D12_RESOURCE_BARRIER barrier2 = {
        			.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        			.Transition = {
        				.pResource = DstResource,
        				.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        				.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST,
        				.StateAfter = D3D12_RESOURCE_STATE_COMMON,
        			}
        		};
		
		CmdList->ResourceBarrier(1, &barrier2);
		
		
		D3D12_RESOURCE_BARRIER barrier3 = {
			.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
			.Transition = {
				.pResource = SrcResource,
				.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
				.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE,
				.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET,
			}
		};
		
		CmdList->ResourceBarrier(1, &barrier3);
	}
	// D3D12_RESOURCE_BARRIER barrier = {
	// 	.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
	// 	.Transition = {
	// 		.pResource = SrcResource,
	// 		.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
	// 		.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET,
	// 		.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE,
	// 	}
	// };
	//
	//
	//
	// if (bIsInputPin)
	// {
	// 	Swap(SrcResource, DstResource);
	// 	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
	// }
	// CmdList->ResourceBarrier(1, &barrier);
	// CmdList->CopyResource(DstResource, SrcResource);
	// Swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
	// Barriers.Add(barrier);

	return true;
}

void FilterCopies(mz::fb::ShowAs FilterShowAs, TMap<MZProperty*, ResourceInfo>& Copies, TMap<UTextureRenderTarget2D*, ResourceInfo>& FilteredCopies)
{
	for (auto [mzprop, info] : Copies)
	{
		UObject* obj = mzprop->GetRawObjectContainer();
		if (!obj) continue;
		auto prop = CastField<FObjectProperty>(mzprop->Property);
		if (!prop) continue;
		auto URT = Cast<UTextureRenderTarget2D>(prop->GetObjectPropertyValue(prop->ContainerPtrToValuePtr<UTextureRenderTarget2D>(obj)));
		if (!URT) continue;

		if(info.ShowAs == FilterShowAs)
		{
			FilteredCopies.Add(URT, info);
		}
	}
}

void MZTextureShareManager::SetupFences(mz::fb::ShowAs CopyShowAs,
	TMap<ID3D12Fence*, u64>& SignalGroup)
{
	if(ExecutionState == mz::app::ExecutionState::SYNCED)
	{
		if(CopyShowAs == mz::fb::ShowAs::INPUT_PIN)
		{
			CmdQueue->Wait(InputFence, (2 * FrameCounter) + 1);
			SignalGroup.Add(InputFence, (2 * FrameCounter) + 2);
			UE_LOG(LogTemp, Warning, TEXT("Input pins are waiting on %d") , 2 * FrameCounter + 1);
		}
		else if (CopyShowAs == mz::fb::ShowAs::OUTPUT_PIN)
		{
			CmdQueue->Wait(OutputFence, (2 * FrameCounter));
			SignalGroup.Add(OutputFence, (2 * FrameCounter) + 1);
			UE_LOG(LogTemp, Warning, TEXT("Out pins are waiting on %d") , 2 * FrameCounter);
			FrameCounter++;
		}
	}
}

void MZTextureShareManager::ProcessCopies(mz::fb::ShowAs CopyShowAs, TMap<MZProperty*, ResourceInfo>& CopyMap)
{
	{
		if (CopyMap.IsEmpty())
		{
			return;
		}
	}
	TMap<UTextureRenderTarget2D*, ResourceInfo> CopiesFiltered;
	FilterCopies(CopyShowAs, CopyMap, CopiesFiltered);
	// if (CopiesFiltered.IsEmpty())
	// 	return;

	auto cmdData = GetNewCommandList();
	ENQUEUE_RENDER_COMMAND(FMZClient_CopyOnTick)(
		[this, CopyShowAs, CopiesFiltered, cmdData](FRHICommandListImmediate& RHICmdList)
		{
			TArray<D3D12_RESOURCE_BARRIER> barriers;
			std::vector<flatbuffers::Offset<mz::app::AppEvent>> events;
			TMap<ID3D12Fence*, u64> SignalGroup;
			flatbuffers::FlatBufferBuilder fbb;

			SetupFences(CopyShowAs, SignalGroup);
			
			for (auto& [URT, pin] : CopiesFiltered)
			{
				if(MZCopyTexture_RenderThread(CopyShowAs, URT, pin.DstResource, cmdData->CmdList, barriers)
					&& CopyShowAs == mz::fb::ShowAs::OUTPUT_PIN
					&& events.empty())
				{
					//events.push_back(mz::CreateAppEventOffset(fbb, mz::app::CreatePinDirtied(fbb, (mz::fb::UUID*)&pin.SrcMzp->Id, FrameCounter)));
				}
			}
			
			cmdData->CmdList->ResourceBarrier(barriers.Num(), barriers.GetData());
			ExecCommands(cmdData, CopyShowAs, SignalGroup);
			if (!events.empty() && MZClient && MZClient->IsConnected())
			{
				//MZClient->AppServiceClient->Send(mz::CreateAppEvent(fbb, mz::app::CreateBatchAppEventDirect(fbb, &events)));
			}
		});
}

void MZTextureShareManager::OnBeginFrame()
{
	ProcessCopies(mz::fb::ShowAs::INPUT_PIN, Copies);
}

void MZTextureShareManager::OnEndFrame()
{
	ProcessCopies(mz::fb::ShowAs::OUTPUT_PIN, Copies);
}

void MZTextureShareManager::ExecutionStateChanged(mz::app::ExecutionState newState, bool& outSemaphoresRenewed)
{
	if(newState != ExecutionState)
	{
		switch (newState)
		{
		case mz::app::ExecutionState::SYNCED:
			RenewSemaphores();
			outSemaphoresRenewed = true;
			break;
		case mz::app::ExecutionState::IDLE:
			outSemaphoresRenewed = false;
			break;
		}
	}
	ENQUEUE_RENDER_COMMAND(FMZClient_CopyOnTick)(
    		[this, newState](FRHICommandListImmediate& RHICmdList)
    		{
				ExecutionState = newState;
    		});
}

void MZTextureShareManager::Reset()
{
	CopyOnTick.Empty();
	PendingCopyQueue.Empty();
}

void MZTextureShareManager::Initiate()
{
	auto hwinfo = FHardwareInfo::GetHardwareInfo(NAME_RHI);
	if ("D3D12" != hwinfo)
	{
		return;
	}

	MZClient = &FModuleManager::LoadModuleChecked<FMZClient>("MZClient");
	
	// Create DX resources
	Dev = (ID3D12Device*)GetID3D12DynamicRHI()->RHIGetNativeDevice();

	CmdQueue = GetID3D12DynamicRHI()->RHIGetCommandQueue();
	CmdQueue->AddRef();
	
	AllocateCommandLists();

	RenewSemaphores();
}

void MZTextureShareManager::RenewSemaphores()
{
	if (InputFence)
	{
		::CloseHandle(SyncSemaphoresExportHandles.InputSemaphore);
		InputFence->Release();

	}
	if (OutputFence)
	{
		::CloseHandle(SyncSemaphoresExportHandles.OutputSemaphore);
		OutputFence->Release();
	}

	FrameCounter = 0;

	Dev->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&InputFence));
	Dev->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&OutputFence));
	MZ_D3D12_ASSERT_SUCCESS(Dev->CreateSharedHandle(InputFence, 0, GENERIC_ALL, 0, &SyncSemaphoresExportHandles.InputSemaphore));
	MZ_D3D12_ASSERT_SUCCESS(Dev->CreateSharedHandle(OutputFence, 0, GENERIC_ALL, 0, &SyncSemaphoresExportHandles.OutputSemaphore));
}

	
	
