#pragma once
#include "CoreMinimal.h"
#include "NenWorldState.generated.h"

// Everything stored for one level (each level has exactly one stream)
USTRUCT(BlueprintType)
struct FNenLevelState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadWrite) float StreamFlow = 0.f;
};

USTRUCT(BlueprintType)
struct FNenCheckpointState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadWrite) bool Unlocked = false;
	UPROPERTY(BlueprintReadWrite) FString By;
};

// Mirrors the `world` jsonb column in player_state
USTRUCT(BlueprintType)
struct FNenWorldState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadWrite) int32 V = 1;
	UPROPERTY(BlueprintReadWrite) TMap<FString, FNenLevelState>      Levels;       // key: level ID, e.g. "Ravine"
	UPROPERTY(BlueprintReadWrite) TMap<FString, FNenCheckpointState> Checkpoints;  // key: checkpoint ID, e.g. "cp_ravine"
};