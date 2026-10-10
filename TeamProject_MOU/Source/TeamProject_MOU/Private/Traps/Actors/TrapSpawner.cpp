#include "Traps/Actors/TrapSpawner.h"
#include "Traps/Actors/TrapBearTrap.h"
#include "Traps/Actors/TrapElectricPanel.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/BillboardComponent.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFramework/Volume.h"

ATrapSpawner::ATrapSpawner()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	EditorIcon = CreateDefaultSubobject<UBillboardComponent>(TEXT("EditorIcon"));
	EditorIcon->SetupAttachment(SceneRoot);
	EditorIcon->bIsScreenSizeScaled = true;

	SpawnAreaBox = CreateDefaultSubobject<UBoxComponent>(TEXT("SpawnAreaBox"));
	SpawnAreaBox->SetupAttachment(SceneRoot);
	SpawnAreaBox->SetBoxExtent(FVector(1000.0f, 1000.0f, 200.0f));
	SpawnAreaBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SpawnAreaBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	SpawnAreaBox->ShapeColor = AreaBoxColor;
	SpawnAreaBox->SetLineThickness(AreaLineThickness);
	SpawnAreaBox->bDrawOnlyIfSelected = false;

	static ConstructorHelpers::FClassFinder<ATrapBase> BearTrapBPClass(TEXT("/Game/05_JYH/Trap/BP_Trap_BearTrap"));
	if (BearTrapBPClass.Succeeded())
	{
		TrapClasses.Add(BearTrapBPClass.Class);
	}
	else
	{
		TrapClasses.Add(ATrapBearTrap::StaticClass());
	}

	static ConstructorHelpers::FClassFinder<ATrapBase> ElectricPanelBPClass(TEXT("/Game/05_JYH/Trap/BP_Trap_ElectricPanel"));
	if (ElectricPanelBPClass.Succeeded())
	{
		TrapClasses.Add(ElectricPanelBPClass.Class);
	}
	else
	{
		TrapClasses.Add(ATrapElectricPanel::StaticClass());
	}
}

void ATrapSpawner::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (SpawnAreaBox)
	{
		SpawnAreaBox->ShapeColor = AreaBoxColor;
		SpawnAreaBox->SetLineThickness(AreaLineThickness);
		SpawnAreaBox->MarkRenderStateDirty();
	}
}

void ATrapSpawner::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority() || !bAutoSpawnOnBeginPlay)
	{
		return;
	}

	if (AutoSpawnDelay > 0.0f)
	{
		GetWorldTimerManager().SetTimer(
			AutoSpawnTimerHandle,
			this,
			&ATrapSpawner::SpawnTraps,
			AutoSpawnDelay,
			false
		);
	}
	else
	{
		SpawnTraps();
	}
}

void ATrapSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoSpawnTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void ATrapSpawner::SpawnTraps()
{
	UWorld* World = GetWorld();
	if (!World || !IsValid(SpawnAreaBox))
	{
		return;
	}

	if (World->IsGameWorld() && !HasAuthority())
	{
		return;
	}

	ClearSpawnedTraps();

	const FBox Bounds = SpawnAreaBox->Bounds.GetBox();

	for (int32 Index = 0; Index < TotalTrapCount; ++Index)
	{
		bool bFoundLocation = false;
		FTransform SpawnTransform;

		for (int32 Attempt = 0; Attempt < MaxSpawnAttemptsPerTrap; ++Attempt)
		{
			const float RandomX = FMath::RandRange(Bounds.Min.X, Bounds.Max.X);
			const float RandomY = FMath::RandRange(Bounds.Min.Y, Bounds.Max.Y);
			const float StartZ = Bounds.Max.Z;
			const float EndZ = Bounds.Min.Z - GroundTraceDistance;

			const FVector TraceStart(RandomX, RandomY, StartZ);
			const FVector TraceEnd(RandomX, RandomY, EndZ);

			FHitResult HitResult;
			FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TrapSpawnerGroundTrace), false, this);
			QueryParams.bTraceComplex = true;

			for (const TObjectPtr<ATrapBase>& ExistingTrap : SpawnedTraps)
			{
				if (IsValid(ExistingTrap))
				{
					QueryParams.AddIgnoredActor(ExistingTrap);
				}
			}

			FCollisionObjectQueryParams ObjectQueryParams;
			ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldStatic);
			ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldDynamic);

			TArray<FHitResult> HitResults;
			bool bHit = World->LineTraceMultiByObjectType(
				HitResults,
				TraceStart,
				TraceEnd,
				ObjectQueryParams,
				QueryParams
			);

			if (!bHit)
			{
				bHit = World->LineTraceMultiByChannel(
					HitResults,
					TraceStart,
					TraceEnd,
					ECC_Visibility,
					QueryParams
				);
			}

			if (!bHit)
			{
				continue;
			}

			FHitResult ValidFloorHit;
			bool bFoundValidFloor = false;

			for (const FHitResult& Hit : HitResults)
			{
				if (!Hit.bBlockingHit)
				{
					continue;
				}

				AActor* HitActor = Hit.GetActor();
				if (!HitActor || HitActor == this || HitActor->IsA<ATrapBase>() || HitActor->IsA<AVolume>())
				{
					continue;
				}

				if (Hit.ImpactNormal.Z < 0.65f)
				{
					continue;
				}

				if (UPrimitiveComponent* HitComp = Hit.GetComponent())
				{
					if (HitComp->CanCharacterStepUpOn == ECB_No)
					{
						continue;
					}
				}

				ValidFloorHit = Hit;
				bFoundValidFloor = true;
				break;
			}

			if (!bFoundValidFloor)
			{
				continue;
			}

			const FVector SurfaceNormal = ValidFloorHit.ImpactNormal;
			const FVector CandidateLocation = ValidFloorHit.ImpactPoint + (SurfaceNormal * FloorClearanceOffset);

			if (!IsLocationFarEnoughFromExisting(CandidateLocation))
			{
				continue;
			}

			FRotator SpawnRotation = FRotator(0.0f, FMath::RandRange(0.0f, 360.0f), 0.0f);
			if (bAlignToFloorNormal)
			{
				const FVector RandomDirection = SpawnRotation.Vector();
				FVector ForwardDir = FVector::VectorPlaneProject(RandomDirection, SurfaceNormal).GetSafeNormal();
				if (ForwardDir.IsNearlyZero())
				{
					ForwardDir = FVector::VectorPlaneProject(FVector::ForwardVector, SurfaceNormal).GetSafeNormal();
				}
				const FVector RightDir = FVector::CrossProduct(SurfaceNormal, ForwardDir).GetSafeNormal();
				const FMatrix RotationMatrix(ForwardDir, RightDir, SurfaceNormal, FVector::ZeroVector);
				SpawnRotation = RotationMatrix.Rotator();
			}

			SpawnTransform.SetLocation(CandidateLocation);
			SpawnTransform.SetRotation(SpawnRotation.Quaternion());
			SpawnTransform.SetScale3D(FVector::OneVector);

			bFoundLocation = true;
			break;
		}

		if (!bFoundLocation)
		{
			continue;
		}

		const TSubclassOf<ATrapBase> SelectedClass = SelectRandomTrapClass();
		if (!SelectedClass)
		{
			continue;
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

		ATrapBase* NewTrap = World->SpawnActor<ATrapBase>(SelectedClass, SpawnTransform, SpawnParams);
		if (IsValid(NewTrap))
		{
			if (NewTrap->GetRootComponent() && NewTrap->GetRootComponent()->Mobility != EComponentMobility::Movable)
			{
				NewTrap->GetRootComponent()->SetMobility(EComponentMobility::Movable);
			}

			TArray<UPrimitiveComponent*> PrimitiveComponents;
			NewTrap->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
			for (UPrimitiveComponent* Prim : PrimitiveComponents)
			{
				if (IsValid(Prim) && Prim->IsSimulatingPhysics())
				{
					Prim->SetSimulatePhysics(false);
				}
			}

			SpawnedTraps.Add(NewTrap);
			SpawnedTrapLocations.Add(SpawnTransform.GetLocation());
		}
	}

	if (bShowSpawnDebugInfo)
	{
		TMap<FString, int32> ClassCounts;
		for (const TObjectPtr<ATrapBase>& Trap : SpawnedTraps)
		{
			if (IsValid(Trap))
			{
				const FString ClassName = Trap->GetClass()->GetName();
				ClassCounts.FindOrAdd(ClassName)++;
			}
		}

		const int32 SuccessCount = SpawnedTraps.Num();
		const int32 FailedCount = FMath::Max(0, TotalTrapCount - SuccessCount);

		FString DetailsString;
		for (const auto& Pair : ClassCounts)
		{
			DetailsString += FString::Printf(TEXT("\n   - %s: %d개"), *Pair.Key, Pair.Value);
		}

		const FString SummaryMessage = FString::Printf(
			TEXT("[TrapSpawner - %s] 스폰 완료 | 목표: %d개, 성공: %d개, 실패: %d개%s"),
			*GetName(),
			TotalTrapCount,
			SuccessCount,
			FailedCount,
			*DetailsString
		);

		UE_LOG(LogTemp, Warning, TEXT("%s"), *SummaryMessage);

		if (GEngine)
		{
			const FColor MsgColor = (FailedCount == 0) ? FColor::Green : FColor::Orange;
			GEngine->AddOnScreenDebugMessage(-1, DebugDrawDuration, MsgColor, SummaryMessage);
		}

		if (bDrawDebugVisuals && World)
		{
			for (int32 i = 0; i < SpawnedTraps.Num() && i < SpawnedTrapLocations.Num(); ++i)
			{
				if (IsValid(SpawnedTraps[i]))
				{
					const FVector Loc = SpawnedTrapLocations[i];
					const FString TrapName = SpawnedTraps[i]->GetClass()->GetName();
					DrawDebugSphere(World, Loc, 30.0f, 12, FColor::Cyan, false, DebugDrawDuration, 0, 2.0f);
					DrawDebugString(World, Loc + FVector(0.0f, 0.0f, 40.0f), TrapName, nullptr, FColor::Yellow, DebugDrawDuration, true, 1.2f);
				}
			}
		}
	}
}

void ATrapSpawner::ClearSpawnedTraps()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (World->IsGameWorld() && !HasAuthority())
	{
		return;
	}

	for (const TObjectPtr<ATrapBase>& Trap : SpawnedTraps)
	{
		if (IsValid(Trap))
		{
			Trap->SetActorEnableCollision(false);
#if WITH_EDITOR
			if (!World->IsGameWorld())
			{
				World->EditorDestroyActor(Trap, true);
			}
			else
#endif
			{
				Trap->Destroy();
			}
		}
	}
	SpawnedTraps.Empty();
	SpawnedTrapLocations.Empty();
}

void ATrapSpawner::DebugSpawnTrapsInEditor()
{
	SpawnTraps();
}

void ATrapSpawner::DebugClearTrapsInEditor()
{
	ClearSpawnedTraps();
}

TArray<ATrapBase*> ATrapSpawner::GetSpawnedTraps() const
{
	TArray<ATrapBase*> Result;
	Result.Reserve(SpawnedTraps.Num());
	for (const TObjectPtr<ATrapBase>& Trap : SpawnedTraps)
	{
		if (IsValid(Trap))
		{
			Result.Add(Trap.Get());
		}
	}
	return Result;
}

bool ATrapSpawner::IsLocationFarEnoughFromExisting(const FVector& CandidateLocation) const
{
	const float MinDistSq = FMath::Square(MinDistanceBetweenTraps);
	for (const FVector& Location : SpawnedTrapLocations)
	{
		const float DistSq = bUse2DDistanceCheck
			? FVector::DistSquared2D(CandidateLocation, Location)
			: FVector::DistSquared(CandidateLocation, Location);

		if (DistSq < MinDistSq)
		{
			return false;
		}
	}
	return true;
}

TSubclassOf<ATrapBase> ATrapSpawner::SelectRandomTrapClass() const
{
	TArray<TSubclassOf<ATrapBase>> ValidClasses;
	for (const TSubclassOf<ATrapBase>& ClassCandidate : TrapClasses)
	{
		if (ClassCandidate)
		{
			ValidClasses.Add(ClassCandidate);
		}
	}

	if (ValidClasses.IsEmpty())
	{
		return nullptr;
	}

	const int32 RandomIndex = FMath::RandRange(0, ValidClasses.Num() - 1);
	return ValidClasses[RandomIndex];
}

void ATrapSpawner::RemoveInvalidSpawnedTraps()
{
	SpawnedTraps.RemoveAll([](const TObjectPtr<ATrapBase>& Trap)
	{
		return !IsValid(Trap);
	});
}
