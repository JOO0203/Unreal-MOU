#include "Traps/Actors/TrapCrusher.h"
#include "Components/StaticMeshComponent.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Traps/Components/TrapTriggerComponent.h"
#include "Traps/Components/TrapPayloadComponent.h"
#include "Traps/Data/TrapDataAsset.h"
#include "Base/EventObjectBase.h"

ATrapCrusher::ATrapCrusher()
{
	PrimaryActorTick.bCanEverTick = true;

	CrushDirectionArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("CrushDirectionArrow"));
	CrushDirectionArrow->SetupAttachment(RootScene);
	CrushDirectionArrow->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f)); // 기본 하방 방향
	CrushDirectionArrow->ArrowColor = FColor::Orange;
	CrushDirectionArrow->ArrowSize = 1.5f;
	CrushDirectionArrow->bIsEditorOnly = false;

	CrusherMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CrusherMesh"));
	CrusherMesh->SetupAttachment(RootScene);
	CrusherMesh->CanCharacterStepUpOn = ECB_No;

	CrushDamageBox = CreateDefaultSubobject<UBoxComponent>(TEXT("CrushDamageBox"));
	CrushDamageBox->SetupAttachment(CrusherMesh);
	CrushDamageBox->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	CrushDamageBox->SetGenerateOverlapEvents(true);

	if (BaseTriggerBox)
	{
		BaseTriggerBox->SetupAttachment(RootScene);
		BaseTriggerBox->InitBoxExtent(FVector(100.0f, 100.0f, 150.0f));
	}
}

void ATrapCrusher::BeginPlay()
{
	Super::BeginPlay();

	if (CrusherMesh)
	{
		InitialCrusherRelativeLocation = CrusherMesh->GetRelativeLocation();
		TargetCrusherRelativeLocation = InitialCrusherRelativeLocation;
	}

	if (CrushDamageBox)
	{
		CrushDamageBox->OnComponentBeginOverlap.AddDynamic(this, &ATrapCrusher::HandleCrushOverlap);
	}
}

void ATrapCrusher::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UpdateCrusherMovement(DeltaTime);
}

void ATrapCrusher::OnStateEntered(ETrapState NewState)
{
	Super::OnStateEntered(NewState);

	if (NewState == ETrapState::Active)
	{
		bIsDescending = true;
		bIsAscending = false;
		bIsBlocked = false;
		BottomHoldTimer = 0.0f;

		FVector MoveDir = CrushDirectionArrow ? CrushDirectionArrow->GetRelativeRotation().Vector() : FVector(0.0f, 0.0f, -1.0f);
		TargetCrusherRelativeLocation = InitialCrusherRelativeLocation + (MoveDir * MaxDropDistance);
	}
	else if (NewState == ETrapState::Cooldown || NewState == ETrapState::Idle)
	{
		bIsDescending = false;
		bIsAscending = true;
		TargetCrusherRelativeLocation = InitialCrusherRelativeLocation;
	}
}

void ATrapCrusher::UpdateCrusherMovement(float DeltaTime)
{
	if (!CrusherMesh)
	{
		return;
	}

	FVector CurrentLoc = CrusherMesh->GetRelativeLocation();

	if (bIsDescending)
	{
		if (bIsBlocked)
		{
			// 퍼즐 상자에 걸려 저지됨 -> 즉시 상승 모드로 전환
			bIsDescending = false;
			bIsAscending = true;
			TargetCrusherRelativeLocation = InitialCrusherRelativeLocation;
			return;
		}

		FVector NewLoc = FMath::VInterpConstantTo(CurrentLoc, TargetCrusherRelativeLocation, DeltaTime, DropSpeed);
		CrusherMesh->SetRelativeLocation(NewLoc);

		if (FVector::DistSquared(NewLoc, TargetCrusherRelativeLocation) < 4.0f)
		{
			bIsDescending = false;
			BottomHoldTimer = BottomHoldDuration;
		}
	}
	else if (BottomHoldTimer > 0.0f)
	{
		BottomHoldTimer -= DeltaTime;
		if (BottomHoldTimer <= 0.0f)
		{
			bIsAscending = true;
			TargetCrusherRelativeLocation = InitialCrusherRelativeLocation;
		}
	}
	else if (bIsAscending)
	{
		FVector NewLoc = FMath::VInterpConstantTo(CurrentLoc, TargetCrusherRelativeLocation, DeltaTime, RetractSpeed);
		CrusherMesh->SetRelativeLocation(NewLoc);

		if (FVector::DistSquared(NewLoc, TargetCrusherRelativeLocation) < 4.0f)
		{
			bIsAscending = false;
		}
	}
}

void ATrapCrusher::HandleCrushOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!HasAuthority() || !OtherActor || OtherActor == this || !bIsDescending)
	{
		return;
	}

	// 1. 퍼즐 상자(AEventObjectBase)가 끼어 있으면 압살기 저지
	if (OtherActor->IsA(AEventObjectBase::StaticClass()))
	{
		bIsBlocked = true;
		return;
	}

	// 2. 캐릭터 또는 패키지에 치명적 압살 대미지 인가
	if (PayloadComponent)
	{
		PayloadComponent->ExecutePayloadOnActor(OtherActor, TrapData);
	}

	// 3. 대상을 찍어누른 즉시 하강 정지 -> 해당 위치에서 홀드 후 복귀 (물리 클리핑 방지)
	bIsDescending = false;
	BottomHoldTimer = BottomHoldDuration;
}

void ATrapCrusher::ExecuteTrapPayload()
{
	// 하강 중 충돌(HandleCrushOverlap)로 실시간 인가
}
