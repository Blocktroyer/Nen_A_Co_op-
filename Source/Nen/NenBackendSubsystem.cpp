// NenBackendSubsystem.cpp
#include "NenBackendSubsystem.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Kismet/GameplayStatics.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "JsonObjectConverter.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogNenBackend, Log, All);

void UNenBackendSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // Each test window needs its own login, or they'd all be the same player.
    //   Standalone / packaged:  launch with -NenProfile=B   -> slot "NenAuth_B"
    //   Play In Editor:         2nd window, 3rd window...   -> slot "NenAuth_PIE1", "NenAuth_PIE2"
    FString Profile;
    if (FParse::Value(FCommandLine::Get(), TEXT("NenProfile="), Profile) && !Profile.IsEmpty())
    {
        AuthSlot = TEXT("NenAuth_") + Profile;
    }
#if WITH_EDITOR
    else if (const FWorldContext* Context = GetGameInstance()->GetWorldContext();
             Context && Context->WorldType == EWorldType::PIE && Context->PIEInstance > 0)
    {
        AuthSlot = FString::Printf(TEXT("NenAuth_PIE%d"), Context->PIEInstance);
    }
#endif
    UE_LOG(LogNenBackend, Log, TEXT("Backend using login slot %s"), *AuthSlot);

    ProjectUrl.RemoveFromEnd(TEXT("/"));
    if (PublishableKey.IsEmpty())
    {
        UE_LOG(LogNenBackend, Error, TEXT("PublishableKey is empty in Config/DefaultGame.ini"));
    }

    if (UNenAuthSave* Save = Cast<UNenAuthSave>(UGameplayStatics::LoadGameFromSlot(AuthSlot, 0)))
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
        UGameplayStatics::SaveGameToSlot(Save, AuthSlot, 0);
    }
    Done(bOk && !AccessToken.IsEmpty());
}

void UNenBackendSubsystem::EnsureLoggedIn(TFunction<void(bool)> Done)
{
    if (IsLoggedIn())
    {
        Done(true);
        return;
    }
    // Refresh tokens rotate, so two refreshes at once would knock each other out: share one.
    LoginWaiters.Add(MoveTemp(Done));
    if (LoginWaiters.Num() == 1)
    {
        Login([this](bool bOk)
        {
            TArray<TFunction<void(bool)>> Waiters = MoveTemp(LoginWaiters);
            LoginWaiters.Reset();
            for (TFunction<void(bool)>& Waiter : Waiters)
            {
                Waiter(bOk);
            }
        });
    }
}

void UNenBackendSubsystem::FetchCouple(
    TFunction<void(bool bOk, bool bHasCouple, const FString& InviteCode, bool bPartnerJoined)> Done)
{
    // Row-level security only returns the couple this player belongs to
    Rest(TEXT("GET"), TEXT("couples?select=invite_code,player_b"), FString(),
        [Done](bool bOk, const FString& Json)
        {
            TArray<TSharedPtr<FJsonValue>> Rows;
            if (!bOk || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Rows))
            {
                Done(false, false, FString(), false);
                return;
            }
            const TSharedPtr<FJsonObject> Row = Rows.Num() > 0 ? Rows[0]->AsObject() : nullptr;
            if (!Row.IsValid())
            {
                Done(true, false, FString(), false);
                return;
            }
            FString InviteCode, PartnerId;
            Row->TryGetStringField(TEXT("invite_code"), InviteCode);                 // null once used
            const bool bPartnerJoined = Row->TryGetStringField(TEXT("player_b"), PartnerId);
            Done(true, true, InviteCode, bPartnerJoined);
        });
}

void UNenBackendSubsystem::ResetLogin()
{
    AccessToken.Reset();
    RefreshToken.Reset();
    UserId.Reset();
    World = FNenWorldState();
    UGameplayStatics::DeleteGameInSlot(AuthSlot, 0);
    if (Inbox.Num() + Trash.Num() > 0)
    {
        Inbox.Reset();
        Trash.Reset();
        OnInboxChanged.Broadcast();
    }
}

// ---------------------------------------------------------------- mail

static FString JsonToString(const TSharedRef<FJsonObject>& Obj)
{
    FString Out;
    FJsonSerializer::Serialize(Obj, TJsonWriterFactory<>::Create(&Out));
    return Out;
}

static TArray<TSharedPtr<FJsonValue>> ToJsonStrings(const TArray<FString>& Strings)
{
    TArray<TSharedPtr<FJsonValue>> Values;
    for (const FString& String : Strings)
    {
        Values.Add(MakeShared<FJsonValueString>(String));
    }
    return Values;
}

// The message column holds {"v":1,"to":"...","body":"...","from":"..."}.
// Anything else (old letters, nen.Pigeon from an older build) is shown as the body.
static FString PackLetterText(const FString& To, const FString& Body, const FString& From)
{
    TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
    Obj->SetNumberField(TEXT("v"), 1);
    Obj->SetStringField(TEXT("to"), To);
    Obj->SetStringField(TEXT("body"), Body);
    Obj->SetStringField(TEXT("from"), From);
    return JsonToString(Obj);
}

static void UnpackLetterText(const FString& Message, FNenLetter& Out)
{
    TSharedPtr<FJsonObject> Obj;
    if (Message.StartsWith(TEXT("{"))
        && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Message), Obj) && Obj.IsValid()
        && Obj->TryGetStringField(TEXT("body"), Out.Body))
    {
        Obj->TryGetStringField(TEXT("to"), Out.To);
        Obj->TryGetStringField(TEXT("from"), Out.From);
        return;
    }
    Out.Body = Message;
}

static bool ParseLetter(const TSharedPtr<FJsonObject>& Row, FNenLetter& Out)
{
    if (!Row.IsValid() || !Row->TryGetStringField(TEXT("id"), Out.Id))
    {
        return false;
    }
    FString Message;
    Row->TryGetStringField(TEXT("message"), Message);
    UnpackLetterText(Message, Out);
    FString CollectedAt;
    Out.bRead = Row->TryGetStringField(TEXT("collected_at"), CollectedAt);   // null = unread
    const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
    if (Row->TryGetArrayField(TEXT("items"), Items))
    {
        for (const TSharedPtr<FJsonValue>& Item : *Items)
        {
            FString ItemName;
            if (Item.IsValid() && Item->TryGetString(ItemName))
            {
                Out.Items.Add(ItemName);
            }
        }
    }
    FString SentAt;
    if (Row->TryGetStringField(TEXT("sent_at"), SentAt))
    {
        // "2026-11-20T10:00:00.123456+00:00": keep the seconds-precision part (always UTC)
        FDateTime::ParseIso8601(*SentAt.Left(19), Out.SentAt);
    }
    return true;
}

void UNenBackendSubsystem::FetchMail(TFunction<void(bool)> Done)
{
    EnsureLoggedIn([this, Done](bool bLoggedIn)
    {
        if (!bLoggedIn)
        {
            Done(false);
            return;
        }
        // Row-level security returns only letters I sent or that arrived for me
        Rest(TEXT("GET"),
             TEXT("mailbox?select=id,sender,message,items,sent_at,collected_at,trashed_at,sender_trashed_at")
                 TEXT("&order=sent_at.desc&limit=300"),
             FString(),
            [this, Done](bool bOk, const FString& Json)
            {
                TArray<TSharedPtr<FJsonValue>> Rows;
                if (!bOk || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Rows))
                {
                    Done(false);
                    return;
                }
                Inbox.Reset();
                Trash.Reset();
                for (const TSharedPtr<FJsonValue>& Value : Rows)
                {
                    const TSharedPtr<FJsonObject> Row = Value.IsValid() ? Value->AsObject() : nullptr;
                    FNenLetter Letter;
                    if (!ParseLetter(Row, Letter))
                    {
                        continue;
                    }
                    FString Sender, TrashedAt;
                    Row->TryGetStringField(TEXT("sender"), Sender);
                    Letter.bSentByMe = Sender == UserId;
                    // Each side has its own trash: the sender's and the recipient's are separate columns
                    const bool bTrashed = Row->TryGetStringField(
                        Letter.bSentByMe ? TEXT("sender_trashed_at") : TEXT("trashed_at"), TrashedAt);
                    (bTrashed ? Trash : Inbox).Add(MoveTemp(Letter));
                }
                OnInboxChanged.Broadcast();
                Done(true);
            });
    });
}

void UNenBackendSubsystem::FetchMailBP(FOnNenBackendDone OnDone)
{
    FetchMail([OnDone](bool bOk) { OnDone.ExecuteIfBound(bOk); });
}

void UNenBackendSubsystem::SendPigeon(const FString& To, const FString& Body, const FString& From,
                                      const TArray<FString>& Items, TFunction<void(bool, const FString&)> Done)
{
    TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("p_message"), PackLetterText(To, Body, From));
    Args->SetArrayField(TEXT("p_items"), ToJsonStrings(Items));
    const FString ArgsString = JsonToString(Args);

    EnsureLoggedIn([this, ArgsString, Done](bool bLoggedIn)
    {
        if (!bLoggedIn)
        {
            Done(false, FString());
            return;
        }
        CallRpc(TEXT("send_pigeon"), ArgsString, [this, Done](bool bOk, const FString& Json)
        {
            // send_pigeon returns the new mailbox row
            TSharedPtr<FJsonObject> Row;
            FNenLetter Letter;
            if (bOk && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Row)
                && ParseLetter(Row, Letter))
            {
                Letter.bSentByMe = true;
                Inbox.Insert(MoveTemp(Letter), 0);
                OnInboxChanged.Broadcast();
            }
            Done(bOk, Json);
        });
    });
}

void UNenBackendSubsystem::SendPigeonBP(const FString& To, const FString& Body, const FString& From,
                                        const TArray<FString>& Items, FOnNenBackendDone OnDone)
{
    SendPigeon(To, Body, From, Items, [OnDone](bool bOk, const FString&) { OnDone.ExecuteIfBound(bOk); });
}

int32 UNenBackendSubsystem::GetUnreadCount() const
{
    int32 Count = 0;
    for (const FNenLetter& Letter : Inbox)
    {
        Count += (Letter.bSentByMe || Letter.bRead) ? 0 : 1;
    }
    return Count;
}

void UNenBackendSubsystem::MarkLettersRead(const TArray<FString>& LetterIds)
{
    // Update locally straight away so the unread icons and mailbox flag drop without waiting.
    // If the request fails, the icons come back on the next FetchMail.
    TArray<FString> Changed;
    for (TArray<FNenLetter>* List : { &Inbox, &Trash })
    {
        for (FNenLetter& Letter : *List)
        {
            // Only the recipient opens a letter; my own sent letters keep my partner's read state
            if (!Letter.bSentByMe && !Letter.bRead && LetterIds.Contains(Letter.Id))
            {
                Letter.bRead = true;
                Changed.Add(Letter.Id);
            }
        }
    }
    if (Changed.Num() == 0)
    {
        return;
    }
    OnInboxChanged.Broadcast();

    TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetArrayField(TEXT("p_ids"), ToJsonStrings(Changed));
    CallRpc(TEXT("mark_mail_collected"), JsonToString(Args), [](bool, const FString&) {});
}

void UNenBackendSubsystem::TrashLetters(const TArray<FString>& LetterIds)
{
    SetLettersTrashed(LetterIds, true);
}

void UNenBackendSubsystem::RestoreLetters(const TArray<FString>& LetterIds)
{
    SetLettersTrashed(LetterIds, false);
}

void UNenBackendSubsystem::SetLettersTrashed(const TArray<FString>& LetterIds, bool bTrashed)
{
    TArray<FNenLetter>& From = bTrashed ? Inbox : Trash;
    TArray<FNenLetter>& To   = bTrashed ? Trash : Inbox;

    TArray<FString> Moved;
    for (int32 i = From.Num() - 1; i >= 0; --i)
    {
        if (LetterIds.Contains(From[i].Id))
        {
            Moved.Add(From[i].Id);
            To.Add(MoveTemp(From[i]));
            From.RemoveAt(i);
        }
    }
    if (Moved.Num() == 0)
    {
        return;
    }
    To.Sort([](const FNenLetter& A, const FNenLetter& B) { return A.SentAt > B.SentAt; });   // newest first
    OnInboxChanged.Broadcast();

    TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetArrayField(TEXT("p_ids"), ToJsonStrings(Moved));
    Args->SetBoolField(TEXT("p_trashed"), bTrashed);
    TWeakObjectPtr<UNenBackendSubsystem> Weak(this);
    CallRpc(TEXT("set_mail_trashed"), JsonToString(Args), [Weak](bool bOk, const FString& Json)
    {
        if (!bOk && Weak.IsValid())
        {
            UE_LOG(LogNenBackend, Warning, TEXT("set_mail_trashed failed: %s"), *Json);
            Weak->FetchMail([](bool) {});   // put the lists back the way the server has them
        }
    });
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

// ---------------------------------------------------------------- test console commands
// Type these in the in-game console (` key). Each runs as the player signed in to that window.

#if !UE_BUILD_SHIPPING
namespace NenConsole
{
    static void Print(const FString& Message, bool bError = false)
    {
        if (bError)
        {
            UE_LOG(LogNenBackend, Warning, TEXT("%s"), *Message);
        }
        else
        {
            UE_LOG(LogNenBackend, Log, TEXT("%s"), *Message);
        }
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 10.f, bError ? FColor::Red : FColor::Cyan, Message);
        }
    }

    // Pulls the "message" out of a Supabase error body
    static FString ErrorText(const FString& Json)
    {
        TSharedPtr<FJsonObject> Obj;
        FString Message;
        if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Obj) && Obj.IsValid()
            && Obj->TryGetStringField(TEXT("message"), Message))
        {
            return Message;
        }
        return Json.IsEmpty() ? TEXT("no response (check ProjectUrl / network)") : Json;
    }

    static FString ToJson(const TSharedRef<FJsonObject>& Obj)
    {
        FString Out;
        FJsonSerializer::Serialize(Obj, TJsonWriterFactory<>::Create(&Out));
        return Out;
    }

    // Finds this window's backend, logs in if needed, then runs Fn
    static void WithBackend(UWorld* World, TFunction<void(UNenBackendSubsystem*)> Fn)
    {
        UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
        UNenBackendSubsystem* Backend = GameInstance ? GameInstance->GetSubsystem<UNenBackendSubsystem>() : nullptr;
        if (!Backend)
        {
            Print(TEXT("Nen backend not available (run this while playing)"), true);
            return;
        }
        TWeakObjectPtr<UNenBackendSubsystem> Weak(Backend);
        Backend->EnsureLoggedIn([Weak, Fn](bool bOk)
        {
            if (!Weak.IsValid())
            {
                return;
            }
            if (!bOk)
            {
                Print(TEXT("Login failed. If you deleted test users in Supabase, run nen.ResetLogin"), true);
                return;
            }
            Fn(Weak.Get());
        });
    }

    static void ReportRpc(bool bOk, const FString& Json, const FString& Success)
    {
        if (bOk)
        {
            Print(Success);
        }
        else
        {
            Print(TEXT("Failed: ") + ErrorText(Json), true);
        }
    }

    static void PrintWorld(const FNenWorldState& World)
    {
        if (World.Levels.Num() == 0 && World.Checkpoints.Num() == 0)
        {
            Print(TEXT("World: nothing has changed yet"));
        }
        for (const TPair<FString, FNenLevelState>& Level : World.Levels)
        {
            Print(FString::Printf(TEXT("World: %s stream flow = %.2f"), *Level.Key, Level.Value.StreamFlow));
        }
        for (const TPair<FString, FNenCheckpointState>& Checkpoint : World.Checkpoints)
        {
            Print(FString::Printf(TEXT("World: checkpoint %s %s (by %s)"), *Checkpoint.Key,
                                  Checkpoint.Value.Unlocked ? TEXT("unlocked") : TEXT("locked"),
                                  *Checkpoint.Value.By));
        }
    }

    // Prints unread letters, then marks them read
    static void CollectMail(UNenBackendSubsystem* Backend)
    {
        TWeakObjectPtr<UNenBackendSubsystem> Weak(Backend);
        Backend->FetchMail([Weak](bool bOk)
        {
            if (!Weak.IsValid())
            {
                return;
            }
            if (!bOk)
            {
                Print(TEXT("Mail fetch failed"), true);
                return;
            }
            if (!Weak->HasUnreadMail())
            {
                Print(TEXT("Mail: no new letters"));
                return;
            }
            TArray<FString> Ids;
            for (const FNenLetter& Letter : Weak->Inbox)
            {
                if (Letter.bRead || Letter.bSentByMe)
                {
                    continue;
                }
                const FString Items = FString::Join(Letter.Items, TEXT(", "));
                const FString Sender = Letter.From.IsEmpty() ? TEXT("partner") : Letter.From;
                Print(FString::Printf(TEXT("Mail from %s: \"%s\"%s"), *Sender, *Letter.Body,
                                      Items.IsEmpty() ? TEXT("") : *(TEXT("  + ") + Items)));
                Ids.Add(Letter.Id);
            }
            Weak->MarkLettersRead(Ids);
        });
    }

    static void PrintInviteCode(const FString& Code)
    {
        Print(FString::Printf(TEXT("Invite code: %s   (in the other window: nen.Join %s)"), *Code, *Code));
    }

    // Prints the existing couple's invite code, or why there isn't one
    static void PrintCouple(UNenBackendSubsystem* Backend)
    {
        Backend->FetchCouple([](bool bOk, bool bHasCouple, const FString& InviteCode, bool bPartnerJoined)
        {
            if (!bOk)
            {
                Print(TEXT("Couldn't read your couple"), true);
            }
            else if (!bHasCouple)
            {
                Print(TEXT("No couple yet. Run nen.CreateCouple, or nen.Join <code>"));
            }
            else if (bPartnerJoined)
            {
                Print(TEXT("Already paired: your partner has joined, so the invite code is used up"));
            }
            else
            {
                PrintInviteCode(InviteCode);
            }
        });
    }

    static FAutoConsoleCommandWithWorldAndArgs CreateCoupleCmd(
        TEXT("nen.CreateCouple"),
        TEXT("Start a couple and print its invite code (or the existing one). Usage: nen.CreateCouple"),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
        {
            WithBackend(World, [](UNenBackendSubsystem* Backend)
            {
                TWeakObjectPtr<UNenBackendSubsystem> Weak(Backend);
                Backend->CallRpc(TEXT("create_couple"), TEXT("{}"), [Weak](bool bOk, const FString& Json)
                {
                    if (bOk)
                    {
                        PrintInviteCode(Json.TrimQuotes());
                    }
                    else if (ErrorText(Json).Contains(TEXT("already paired")) && Weak.IsValid())
                    {
                        PrintCouple(Weak.Get());   // already has a couple: show its code instead
                    }
                    else
                    {
                        Print(TEXT("CreateCouple failed: ") + ErrorText(Json), true);
                    }
                });
            });
        }));

    static FAutoConsoleCommandWithWorldAndArgs InviteCodeCmd(
        TEXT("nen.InviteCode"),
        TEXT("Print your couple's invite code if it hasn't been used yet. Usage: nen.InviteCode"),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
        {
            WithBackend(World, [](UNenBackendSubsystem* Backend) { PrintCouple(Backend); });
        }));

    static FAutoConsoleCommandWithWorldAndArgs JoinCmd(
        TEXT("nen.Join"),
        TEXT("Join a couple with an invite code. Usage: nen.Join A1B2C3"),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
        {
            if (Args.Num() < 1)
            {
                Print(TEXT("Usage: nen.Join <invite code>"), true);
                return;
            }
            const FString Code = Args[0];
            WithBackend(World, [Code](UNenBackendSubsystem* Backend)
            {
                TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
                Body->SetStringField(TEXT("p_code"), Code);
                Backend->CallRpc(TEXT("join_couple"), ToJson(Body), [](bool bOk, const FString& Json)
                {
                    ReportRpc(bOk, Json, TEXT("Paired! You're now partners."));
                });
            });
        }));

    static FAutoConsoleCommandWithWorldAndArgs PigeonCmd(
        TEXT("nen.Pigeon"),
        TEXT("Send your partner a letter. Usage: nen.Pigeon miss you already"),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
        {
            const FString Message = FString::Join(Args, TEXT(" "));
            if (Message.IsEmpty())
            {
                Print(TEXT("Usage: nen.Pigeon <message>"), true);
                return;
            }
            WithBackend(World, [Message](UNenBackendSubsystem* Backend)
            {
                Backend->SendPigeon(TEXT(""), Message, TEXT("nen.Pigeon"), TArray<FString>(),
                                    [Message](bool bOk, const FString& Json)
                {
                    ReportRpc(bOk, Json, FString::Printf(TEXT("Pigeon sent: \"%s\""), *Message));
                });
            });
        }));

    static FAutoConsoleCommandWithWorldAndArgs StormCmd(
        TEXT("nen.Storm"),
        TEXT("Raise the stream in a level on your partner's side. Usage: nen.Storm Ravine 1"),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
        {
            if (Args.Num() < 1)
            {
                Print(TEXT("Usage: nen.Storm <LevelId> [amount, default 1]"), true);
                return;
            }
            const FString LevelId = Args[0];
            const double Amount = Args.Num() > 1 ? FCString::Atod(*Args[1]) : 1.0;
            WithBackend(World, [LevelId, Amount](UNenBackendSubsystem* Backend)
            {
                TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
                Payload->SetNumberField(TEXT("v"), 1);
                Payload->SetNumberField(TEXT("delta_flow"), Amount);
                Payload->SetStringField(TEXT("cause"), TEXT("storm"));

                TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
                Body->SetStringField(TEXT("p_type"), TEXT("water_stream"));
                Body->SetStringField(TEXT("p_target"), LevelId);
                Body->SetObjectField(TEXT("p_payload"), Payload);
                Backend->CallRpc(TEXT("send_world_event"), ToJson(Body),
                    [LevelId, Amount](bool bOk, const FString& Json)
                    {
                        ReportRpc(bOk, Json, FString::Printf(TEXT("Storm sent: %s stream +%.2f"), *LevelId, Amount));
                    });
            });
        }));

    static FAutoConsoleCommandWithWorldAndArgs SyncCmd(
        TEXT("nen.Sync"),
        TEXT("Apply your partner's world events and read new letters. Usage: nen.Sync"),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
        {
            WithBackend(World, [](UNenBackendSubsystem* Backend)
            {
                TWeakObjectPtr<UNenBackendSubsystem> Weak(Backend);
                Backend->SyncWorld([Weak](bool bOk)
                {
                    if (!Weak.IsValid())
                    {
                        return;
                    }
                    if (!bOk)
                    {
                        Print(TEXT("Sync failed (not paired yet, or the SQL isn't set up)"), true);
                        return;
                    }
                    PrintWorld(Weak->World);
                    CollectMail(Weak.Get());
                });
            });
        }));

    static FAutoConsoleCommandWithWorldAndArgs StatusCmd(
        TEXT("nen.Status"),
        TEXT("Show which player this window is signed in as. Usage: nen.Status"),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
        {
            WithBackend(World, [](UNenBackendSubsystem* Backend)
            {
                Print(FString::Printf(TEXT("Signed in as %s (slot %s)"), *Backend->GetUserId(), *Backend->GetAuthSlot()));
                PrintWorld(Backend->World);
            });
        }));

    static FAutoConsoleCommandWithWorldAndArgs ResetLoginCmd(
        TEXT("nen.ResetLogin"),
        TEXT("Forget this window's saved login; the next command signs in as a new player. Usage: nen.ResetLogin"),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
        {
            UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
            if (UNenBackendSubsystem* Backend = GameInstance ? GameInstance->GetSubsystem<UNenBackendSubsystem>() : nullptr)
            {
                Backend->ResetLogin();
                Print(TEXT("Login forgotten. The next nen.* command signs in as a new player."));
            }
        }));
}
#endif // !UE_BUILD_SHIPPING