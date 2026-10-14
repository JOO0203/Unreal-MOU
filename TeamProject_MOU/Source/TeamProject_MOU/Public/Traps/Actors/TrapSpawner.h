#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TrapSpawner.generated.h"

class UBoxComponent;
class USceneComponent;
class UBillboardComponent;
class ATrapBase;

UCLASS()
class TEAMPROJECT_MOU_API ATrapSpawner : public AActor
{
	GENERATED_BODY()

public:
	ATrapSpawner();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Trap Spawner")
	void SpawnTraps();

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Trap Spawner")
	void ClearSpawnedTraps();

	UFUNCTION(CallInEditor, Category = "Trap Spawner|Debug")
	void DebugSpawnTrapsInEditor();

	UFUNCTION(CallInEditor, Category = "Trap Spawner|Debug")
	void DebugClearTrapsInEditor();

	UFUNCTION(BlueprintPure, Category = "Trap Spawner")
	TArray<ATrapBase*> GetSpawnedTraps() const;

	UFUNCTION(BlueprintPure, Category = "Trap Spawner")
	UBoxComponent* GetSpawnAreaBox() const { return SpawnAreaBox; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBillboardComponent> EditorIcon;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> SpawnAreaBox;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap Spawner|Visuals")
	FColor AreaBoxColor = FColor(255, 130, 0, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap Spawner|Visuals", meta = (ClampMin = "1.0"))
	float AreaLineThickness = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap Spawner|Config", meta = (ClampMin = "1"))
	int32 TotalTrapCount = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap Spawner|Config", meta = (ClampMin = "0.0", Units = "cm"))
	float MinDistanceBetweenTraps = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap Spawner|Config")
	TArray<TSubclassOf<ATrapBase>> TrapClasses;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap Spawner|Config")
	bool bAutoSpawnOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap Spawner|Config", meta = (ClampMin = "0.0", Units = "s"))
	float AutoSpawnDelay = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap Spawner|Config", meta = (ClampMin = "1"))
	int32 MaxSpawnAttemptsPerTrap = 40;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap Spawner|Config", meta = (ClampMin = "100.0", Units = "cm"))
	float GroundTraceDistance = 1500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap Spawner|Config")
	bool bAlignToFloorNormal = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap Spawner|Config", meta = (Units = "cm"))
	float FloorClearanceOffset = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap Spawner|Config", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinFloorNormalZ = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap Spawner|Config")
	bool bUse2DDistanceCheck = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap Spawner|Debug")
	bool bShowSpawnDebugInfo = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap Spawner|Debug", meta = (EditCondition = "bShowSpawnDebugInfo"))
	bool bDrawDebugVisuals = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap Spawner|Debug", meta = (EditCondition = "bShowSpawnDebugInfo", Units = "s"))
	float DebugDrawDuration = 10.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Trap Spawner|Status")
	TArray<TObjectPtr<ATrapBase>> SpawnedTraps;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Trap Spawner|Status")
	TArray<FVector> SpawnedTrapLocations;

private:
	FTimerHandle AutoSpawnTimerHandle;

	bool IsLocationFarEnoughFromExisting(const FVector& CandidateLocation) const;
	bool FindFloorHit(const FVector& TraceStart, const FVector& TraceEnd, FHitResult& OutHit) const;
	FVector GetRandomPointInSpawnBox() const;
	void SnapTrapToFloor(ATrapBase* Trap, const FVector& FloorPoint) const;
	TSubclassOf<ATrapBase> SelectRandomTrapClass() const;
	void RemoveInvalidSpawnedTraps();
};
