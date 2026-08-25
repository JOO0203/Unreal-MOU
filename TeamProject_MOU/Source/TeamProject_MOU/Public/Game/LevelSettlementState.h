#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "LevelSettlementState.generated.h"

UENUM(BlueprintType)
enum class ELevelSettlementReason : uint8
{
	None,
	Cleared,
	TimeExpired,
	AllPlayersDead,
	Aborted
};

USTRUCT(BlueprintType)
struct TEAMPROJECT_MOU_API FLevelSettlementData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settlement")
	ELevelSettlementReason Reason = ELevelSettlementReason::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settlement")
	bool bSucceeded = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settlement")
	int32 EarnedGold = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settlement")
	int32 DeliveredItemCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settlement")
	int32 FailedDeliveryCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settlement")
	int32 DeathCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settlement")
	float LevelPlayTimeSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settlement")
	float FinalThreatLevel = 0.0f;
};

USTRUCT(BlueprintType)
struct TEAMPROJECT_MOU_API FLevelSettlementSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Settlement")
	FLevelSettlementData Result;

	UPROPERTY(BlueprintReadOnly, Category = "Settlement")
	bool bFinalized = false;

	// 같은 값으로 다시 정산되더라도 복제 변경을 보장합니다.
	UPROPERTY(BlueprintReadOnly, Category = "Settlement")
	int32 Revision = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnLevelSettlementFinalized, const FLevelSettlementData&, Result);

/** 서버에서 한 번 확정한 레벨 정산 결과를 모든 클라이언트에 알립니다. */
UCLASS(BlueprintType)
class TEAMPROJECT_MOU_API ALevelSettlementState : public AInfo
{
	GENERATED_BODY()

public:
	ALevelSettlementState();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(BlueprintAssignable, Category = "Settlement|Events")
	FOnLevelSettlementFinalized OnSettlementFinalized;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Snapshot, Category = "Settlement")
	FLevelSettlementSnapshot Snapshot;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Settlement")
	bool FinalizeSettlement(const FLevelSettlementData& Result);

	UFUNCTION(BlueprintPure, Category = "Settlement")
	bool IsFinalized() const { return Snapshot.bFinalized; }

	UFUNCTION(BlueprintPure, Category = "Settlement")
	FLevelSettlementData GetResult() const { return Snapshot.Result; }

	UFUNCTION(BlueprintPure, Category = "Settlement", meta = (WorldContext = "WorldContextObject"))
	static ALevelSettlementState* GetLevelSettlementState(const UObject* WorldContextObject);

private:
	UFUNCTION()
	void OnRep_Snapshot();
};
