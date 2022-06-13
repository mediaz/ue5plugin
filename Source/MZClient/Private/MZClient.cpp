
#include "MZClient.h"


#define LOCTEXT_NAMESPACE "FMZClient"


FMZClient::FMZClient() {}

class MZCLIENT_API ClientImpl : public mz::app::AppClient
{
public:
    using mz::app::AppClient::AppClient;

    virtual void OnAppConnected(mz::app::AppConnectedEvent const& event) override
    {
        FMessageDialog::Debugf(FText::FromString("Connected to mzEngine"), 0);
        if (event.has_node())
        {
            id = event.node().id().c_str();
        }
    }

    virtual void OnNodeUpdate(mz::proto::Node const& archive) override;
    virtual void OnMenuFired(mz::app::ContextMenuRequest const& request) override
    {
    }

    void OnTextureCreated(mz::proto::Texture const& texture)
    {
        //IMZClient::Get()->OnTextureReceived(texture);
    }

    virtual void Done(grpc::Status const& Status) override
    {
        //FMessageDialog::Debugf(FText::FromString("App Client shutdown"), 0);
        FMZClient* fmz = (FMZClient*)IMZClient::Get();
        fmz->ClearResources();
    }

    virtual void OnNodeRemoved(mz::app::NodeRemovedEvent const& action) override
    {
        id.Empty();
        FMZClient* fmz = (FMZClient*)IMZClient::Get();
        fmz->ClearResources();
    }

    FString id;
};

size_t FMZClient::HashTextureParams(uint32_t width, uint32_t height, uint32_t format, uint32_t usage)
{
    return
        (0xd24908d710a ^ ((size_t)width << 40ull)) |
        (0X6a6826a9abd ^ ((size_t)height << 18ull)) |
        ((size_t)usage << (6ull)) | (size_t)(format);
}

void FMZClient::ClearResources()
{
    for (auto& [_, pin] : CopyOnTick)
    {
        pin.DstResource->Release();
        pin.Fence->Release();
        CloseHandle(pin.Event);
    }

    PendingCopyQueue.Empty();
    CopyOnTick.Empty();
}

void FMZClient::Disconnect() {
    delete Client;
    Client = 0;
    ClearResources();
}

void FMZClient::InitConnection()
{
    if (Client)
    {
        if (!Client->m_Cancelled)
        {
            return;
        }
        delete Client;
    }

    std::string protoPath = (std::filesystem::path(std::getenv("PROGRAMDATA")) / "mediaz" / "core" / "Applications" / "Unreal Engine 5").string();
    Client = new ClientImpl("A45A5459-997E-4F63-988C-4B2DDD8E9BC0", "Unreal Engine", protoPath.c_str(), true);
}

void FMZClient::StartupModule() {

    // FMessageDialog::Debugf(FText::FromString("Loaded MZClient module"), 0);
    InitConnection();
    InitRHI();
    FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FMZClient::Tick));
}

void FMZClient::ShutdownModule() 
{
}

#pragma optimize( "", off )

void FMZClient::SendPinValueChanged(MZEntity entity)
{
    // SendNodeUpdate(entity);
}

void FMZClient::SendNodeUpdate(MZEntity entity) 
{
    if (!Client || Client->id.IsEmpty())
    {
        return;
    }

    mz::proto::msg<mz::app::AppEvent> event;
    mz::app::NodeUpdate* req = event->mutable_node_update();
    mz::proto::Pin* pin = req->add_pins_to_add();
    mz::proto::Dynamic* dyn = pin->mutable_dynamic();
    
    FString id = entity.Entity->GetId().ToString();
    FString label = entity.Entity->GetLabel().ToString();
    
    pin->set_pin_show_as(mz::proto::ShowAs::OUTPUT_PIN);
    pin->set_pin_can_show_as(mz::proto::CanShowAs::OUTPUT_PIN_ONLY);

    mz::app::SetField(req, mz::app::NodeUpdate::kNodeIdFieldNumber, TCHAR_TO_UTF8(*Client->id));
    mz::app::SetField(pin, mz::proto::Pin::kIdFieldNumber, TCHAR_TO_UTF8(*id));
    mz::app::SetField(pin, mz::proto::Pin::kDisplayNameFieldNumber, TCHAR_TO_UTF8(*label));
    mz::app::SetField(pin, mz::proto::Pin::kNameFieldNumber, TCHAR_TO_UTF8(*label));
    entity.SerializeToProto(dyn);

    Client->Write(event);
}

void FMZClient::SendPinRemoved(FGuid guid)
{
    FString id = guid.ToString();

    {
        std::unique_lock lock(Mutex);
        if (PendingCopyQueue.Remove(guid) || CopyOnTick.Remove(guid))
        {
            mz::proto::msg<mz::app::AppEvent> event;

            mz::app::SetField(event.m_Ptr, mz::app::AppEvent::kRemoveTexture, TCHAR_TO_UTF8(*guid.ToString()));
   
            Client->Write(event);
        }
    }

    if (!Client || Client->id.IsEmpty())
    {
        return;
    }
    mz::proto::msg<mz::app::AppEvent> event;
    mz::app::NodeUpdate* req = event->mutable_node_update();

    mz::app::SetField(req, mz::app::NodeUpdate::kNodeIdFieldNumber, TCHAR_TO_UTF8(*Client->id));
    mz::app::AddRepeatedField(req, mz::app::NodeUpdate::kPinsToDeleteFieldNumber, TCHAR_TO_UTF8(*id));
    
    Client->Write(event);
}


#pragma optimize( "", on )

bool FMZClient::Connect() {
    return true;
}

uint32 FMZClient::Run() {
  return 0;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMZClient, MZClient)

//
//#include "DispelUnrealMadnessPostlude.h"

