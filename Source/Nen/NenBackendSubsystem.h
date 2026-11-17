// NenBackendSubsystem.h
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameFramework/SaveGame.h"
#include "NenWorldState.h"
#include "NenBackendSubsystem.generated.h"

class FJsonObject;

// Keeps the player's login between launches
UCLASS()
class UNenAuthSave : public USaveGame
{
	GENERATED_BODY()
public:
	UPROPERTY() FString RefreshToken;
	UPROPERTY() FString UserId;
};

DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnNenBackendResult, bool, bSuccess, const FString&, Json);
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnNenBackendDone, bool, bSuccess);

UCLASS(Config = Game)
class NEN_API UNenBackendSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// First launch: anonymous sign-up. Later launches: resume the saved session.
	void Login(TFunction<void(bool)> Done);

	// POST /rest/v1/rpc/<Fn>
	void CallRpc(const FString& Fn, const FString& JsonBody, TFunction<void(bool, const FString&)> Done);
	// GET / PATCH /rest/v1/<PathAndQuery>
	void Rest(const FString& Verb, const FString& PathAndQuery, const FString& JsonBody,
			  TFunction<void(bool, const FString&)> Done);

	// Blueprint version of CallRpc
	UFUNCTION(BlueprintCallable, Category = "Nen|Backend")
	void CallRpcBP(const FString& Fn, const FString& JsonBody, FOnNenBackendResult OnDone);

	UFUNCTION(BlueprintCallable, Category = "Nen|Backend")
	void LoginBP(FOnNenBackendDone OnDone);

	// Logs in only if needed; callers that arrive while a login is running share it
	void EnsureLoggedIn(TFunction<void(bool)> Done);

	// Forget the saved login, e.g. after deleting test users in the Supabase dashboard
	UFUNCTION(BlueprintCallable, Category = "Nen|Backend")
	void ResetLogin();

	// Save slot holding this window's login: "NenAuth", "NenAuth_PIE1", "NenAuth_B"...
	FString GetAuthSlot() const { return AuthSlot; }

	// Reads this player's couple. InviteCode is set while waiting for the partner,
	// and empty once they've joined (join_couple clears it).
	void FetchCouple(TFunction<void(bool bOk, bool bHasCouple, const FString& InviteCode, bool bPartnerJoined)> Done);

	UFUNCTION(BlueprintPure, Category = "Nen|Backend") FString GetUserId() const { return UserId; }
	UFUNCTION(BlueprintPure, Category = "Nen|Backend") bool IsLoggedIn() const { return !AccessToken.IsEmpty(); }

	// ---- World state (the `world` column of player_state) ----

	// This player's world as last loaded / applied
	UPROPERTY(BlueprintReadOnly, Category = "Nen|World")
	FNenWorldState World;

	// Load my world, apply the partner's pending world events, then save + acknowledge them together.
	// Call after Login() succeeds. Fails if the player isn't paired yet (no player_state row).
	void SyncWorld(TFunction<void(bool)> Done);

	UFUNCTION(BlueprintCallable, Category = "Nen|World")
	void SyncWorldBP(FOnNenBackendDone OnDone);

	// Stream flow for a level ID such as "Ravine" (0 if nothing has happened there yet)
	UFUNCTION(BlueprintPure, Category = "Nen|World")
	float GetStreamFlow(const FString& LevelId) const;

	// Fill World from a player_state row returned by GET /rest/v1/player_state
	bool ParseWorld(const TSharedPtr<FJsonObject>& PlayerStateRow);

	// Upload World and acknowledge the events that changed it, in one call (save_world RPC)
	void SaveWorld(const TArray<FString>& AppliedEventIds, TFunction<void(bool)> Done);

private:
	// Applies one world_events row to World. Returns false for unknown types,
	// which stay unapplied so a newer build can handle them.
	bool ApplyWorldEvent(const TSharedPtr<FJsonObject>& Event);

	UPROPERTY(Config) FString ProjectUrl;
	UPROPERTY(Config) FString PublishableKey;

	FString AccessToken;
	FString RefreshToken;
	FString UserId;

	FString AuthSlot = TEXT("NenAuth");
	TArray<TFunction<void(bool)>> LoginWaiters;

	void Send(const FString& Verb, const FString& Url, const FString& Body, bool bWithUser,
			  TFunction<void(bool, const FString&)> Done);
	void HandleAuthResponse(bool bOk, const FString& Json, TFunction<void(bool)> Done);
};