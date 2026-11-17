// NenBackendSubsystem.cpp
#include "NenBackendSubsystem.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Kismet/GameplayStatics.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "JsonObjectConverter.h"

static const FString NenAuthSlot = TEXT("NenAuth");

void UNenBackendSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    if (UNenAuthSave* Save = Cast<UNenAuthSave>(UGameplayStatics::LoadGameFromSlot(NenAuthSlot, 0)))
    {
        RefreshToken = Save->RefreshToken;
        UserId = Save->UserId;
    }
}

void UNenBackendSubsystem::Login(TFunction<void(bool)> Done)
{
    const bool bResume = !RefreshToken.IsEmpty();
    const FString Url = ProjectUrl + (bResume ? TEXT("/auth/v1/token?grant_type=refresh_token")
                                              : TEXT("/auth/v1/signup"));
    const FString Body = bResume ? FString::Printf(TEXT("{\"refresh_token\":\"%s\"}"), *RefreshToken)
                                 : TEXT("{}");
    // Auth calls send only the apikey header, no Authorization
    Send(TEXT("POST"), Url, Body, false,
         [this, Done](bool bOk, const FString& Json) { HandleAuthResponse(bOk, Json, Done); });
}

void UNenBackendSubsystem::HandleAuthResponse(bool bOk, const FString& Json, TFunction<void(bool)> Done)
{
    TSharedPtr<FJsonObject> Obj;
    if (bOk && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Obj) && Obj.IsValid())
    {
        AccessToken  = Obj->GetStringField(TEXT("access_token"));
        RefreshToken = Obj->GetStringField(TEXT("refresh_token"));   // rotates on every refresh
        UserId       = Obj->GetObjectField(TEXT("user"))->GetStringField(TEXT("id"));

        UNenAuthSave* Save = Cast<UNenAuthSave>(
            UGameplayStatics::CreateSaveGameObject(UNenAuthSave::StaticClass()));
        Save->RefreshToken = RefreshToken;
        Save->UserId = UserId;
        UGameplayStatics::SaveGameToSlot(Save, NenAuthSlot, 0);
    }
    Done(bOk && !AccessToken.IsEmpty());
}

void UNenBackendSubsystem::Send(const FString& Verb, const FString& Url, const FString& Body,
                                bool bWithUser, TFunction<void(bool, const FString&)> Done)
{
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
    Req->SetURL(Url);
    Req->SetVerb(Verb);
    Req->SetHeader(TEXT("apikey"), PublishableKey);
    if (bWithUser && !AccessToken.IsEmpty())
    {
        Req->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + AccessToken);
    }
    Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    if (!Body.IsEmpty())
    {
        Req->SetContentAsString(Body);
    }
    Req->OnProcessRequestComplete().BindLambda(
        [Done](FHttpRequestPtr, FHttpResponsePtr Res, bool bConnected)
        {
            const bool bOk = bConnected && Res.IsValid()
                          && EHttpResponseCodes::IsOk(Res->GetResponseCode());
            Done(bOk, Res.IsValid() ? Res->GetContentAsString() : FString());
        });
    Req->ProcessRequest();
}

void UNenBackendSubsystem::CallRpc(const FString& Fn, const FString& JsonBody,
                                   TFunction<void(bool, const FString&)> Done)
{
    Send(TEXT("POST"), ProjectUrl + TEXT("/rest/v1/rpc/") + Fn,
         JsonBody.IsEmpty() ? TEXT("{}") : JsonBody, true, MoveTemp(Done));
}

void UNenBackendSubsystem::Rest(const FString& Verb, const FString& PathAndQuery, const FString& JsonBody,
                                TFunction<void(bool, const FString&)> Done)
{
    Send(Verb, ProjectUrl + TEXT("/rest/v1/") + PathAndQuery, JsonBody, true, MoveTemp(Done));
}

void UNenBackendSubsystem::CallRpcBP(const FString& Fn, const FString& JsonBody, FOnNenBackendResult OnDone)
{
    CallRpc(Fn, JsonBody, [OnDone](bool bOk, const FString& Json) { OnDone.ExecuteIfBound(bOk, Json); });
}

void UNenBackendSubsystem::LoginBP(FOnNenBackendDone OnDone)
{
    Login([OnDone](bool bOk) { OnDone.ExecuteIfBound(bOk); });
}

// ---------------------------------------------------------------- world state

float UNenBackendSubsystem::GetStreamFlow(const FString& LevelId) const
{
    const FNenLevelState* State = World.Levels.Find(LevelId);
    return State ? State->StreamFlow : 0.f;
}

bool UNenBackendSubsystem::ParseWorld(const TSharedPtr<FJsonObject>& Row)
{
    const TSharedPtr<FJsonObject>* WorldObj = nullptr;
    if (!Row.IsValid() || !Row->TryGetObjectField(TEXT("world"), WorldObj))
    {
        return false;
    }
    World = FNenWorldState();   // reset, then fill
    return FJsonObjectConverter::JsonObjectToUStruct((*WorldObj).ToSharedRef(), &World);
}

void UNenBackendSubsystem::SaveWorld(const TArray<FString>& AppliedEventIds, TFunction<void(bool)> Done)
{
    TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
    Body->SetObjectField(TEXT("p_world"), FJsonObjectConverter::UStructToJsonObject(World));

    TArray<TSharedPtr<FJsonValue>> Ids;
    for (const FString& Id : AppliedEventIds)
    {
        Ids.Add(MakeShared<FJsonValueString>(Id));
    }
    Body->SetArrayField(TEXT("p_applied"), Ids);

    FString BodyString;
    FJsonSerializer::Serialize(Body, TJsonWriterFactory<>::Create(&BodyString));
    CallRpc(TEXT("save_world"), BodyString, [Done](bool bOk, const FString&) { Done(bOk); });
}

bool UNenBackendSubsystem::ApplyWorldEvent(const TSharedPtr<FJsonObject>& Event)
{
    FString Type, TargetId;
    if (!Event.IsValid()
        || !Event->TryGetStringField(TEXT("type"), Type)
        || !Event->TryGetStringField(TEXT("target_id"), TargetId))
    {
        return false;
    }
    const TSharedPtr<FJsonObject>* Payload = nullptr;
    Event->TryGetObjectField(TEXT("payload"), Payload);

    if (Type == TEXT("water_stream"))
    {
        // target_id = level ID; changes stack so storms add up in any order
        double DeltaFlow = 0.0;
        if (Payload)
        {
            (*Payload)->TryGetNumberField(TEXT("delta_flow"), DeltaFlow);
        }
        World.Levels.FindOrAdd(TargetId).StreamFlow += static_cast<float>(DeltaFlow);
        return true;
    }
    if (Type == TEXT("unlock_checkpoint"))
    {
        FNenCheckpointState& Checkpoint = World.Checkpoints.FindOrAdd(TargetId);
        Checkpoint.Unlocked = true;
        Checkpoint.By = TEXT("partner");
        return true;
    }
    return false;   // unknown type: leave it for a newer build
}

void UNenBackendSubsystem::SyncWorld(TFunction<void(bool)> Done)
{
    // 1. my saved world
    Rest(TEXT("GET"), TEXT("player_state?select=world&player_id=eq.") + UserId, FString(),
        [this, Done](bool bOk, const FString& Json)
        {
            TArray<TSharedPtr<FJsonValue>> Rows;
            if (!bOk || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Rows) || Rows.Num() == 0)
            {
                Done(false);   // request failed, or not paired yet
                return;
            }
            ParseWorld(Rows[0]->AsObject());

            // 2. what my partner did to my side since last time
            Rest(TEXT("GET"),
                 TEXT("world_events?recipient=eq.") + UserId + TEXT("&applied_at=is.null&order=created_at"),
                 FString(),
                [this, Done](bool bOk2, const FString& Json2)
                {
                    TArray<TSharedPtr<FJsonValue>> Events;
                    if (!bOk2 || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json2), Events))
                    {
                        Done(false);
                        return;
                    }
                    TArray<FString> Applied;
                    for (const TSharedPtr<FJsonValue>& Value : Events)
                    {
                        const TSharedPtr<FJsonObject> Event = Value->AsObject();
                        FString Id;
                        if (Event.IsValid() && Event->TryGetStringField(TEXT("id"), Id) && ApplyWorldEvent(Event))
                        {
                            Applied.Add(Id);
                        }
                    }
                    if (Applied.Num() == 0)
                    {
                        Done(true);
                        return;
                    }
                    // 3. save the new world and acknowledge the events in one transaction
                    SaveWorld(Applied, Done);
                });
        });
}

void UNenBackendSubsystem::SyncWorldBP(FOnNenBackendDone OnDone)
{
    SyncWorld([OnDone](bool bOk) { OnDone.ExecuteIfBound(bOk); });
}