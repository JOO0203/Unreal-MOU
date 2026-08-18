#include "Components/InteractionComponent.h"
#include "Interfaces/InteractableInterface.h"
#include "Interfaces/PushableInterface.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Player/MainCharacter.h"
#include "Kismet/KismetSystemLibrary.h"
#include "DrawDebugHelpers.h"

UInteractionComponent::UInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UInteractionComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UpdateFocusedInteractable();
}

void UInteractionComponent::PerformInteraction()
{
	if (!FocusedActor)
	{
		return;
	}

	if (FocusedActor->Implements<UInteractableInterface>())
	{
		if (IInteractableInterface::Execute_CanInteract(FocusedActor, GetOwner()))
		{
			IInteractableInterface::Execute_Interact(FocusedActor, GetOwner());
		}
	}
	else if (FocusedActor->Implements<UPushableInterface>())
	{
		// F키를 눌렀을 때 상호작용이 아니라면 밀기(Push) 시도
		IPushableInterface::Execute_Push(FocusedActor, GetOwner(), GetOwner()->GetActorForwardVector());
	}
}

void UInteractionComponent::UpdateFocusedInteractable()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	FVector StartLocation;
	FRotator ViewRotation;

	APawn* PawnOwner = Cast<APawn>(OwnerActor);
	if (PawnOwner)
	{
		// 로컬 컨트롤러(자신)가 아닐 경우 다른 플레이어의 시야 방향으로 UI가 뜨는 것을 방지
		if (!PawnOwner->IsLocallyControlled())
		{
			return;
		}

		if (PawnOwner->GetController())
		{
			PawnOwner->GetController()->GetPlayerViewPoint(StartLocation, ViewRotation);
		}
		else
		{
			StartLocation = OwnerActor->GetActorLocation();
			ViewRotation = OwnerActor->GetActorRotation();
		}
	}
	else
	{
		StartLocation = OwnerActor->GetActorLocation();
		ViewRotation = OwnerActor->GetActorRotation();
	}

	FVector EndLocation = StartLocation + (ViewRotation.Vector() * InteractionDistance);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(OwnerActor);

	FHitResult HitResult;
	bool bHit = GetWorld()->SweepSingleByChannel(
		HitResult,
		StartLocation,
		EndLocation,
		FQuat::Identity,
		TraceChannel,
		FCollisionShape::MakeSphere(InteractionSphereRadius),
		QueryParams
	);

	AActor* NewFocusedActor = nullptr;
	
	// 일반 상호작용 검사
	if (bHit && HitResult.GetActor())
	{
		if (HitResult.GetActor()->Implements<UInteractableInterface>() || HitResult.GetActor()->Implements<UPushableInterface>())
		{
			NewFocusedActor = HitResult.GetActor();
		}
	}

	// [추가] 주변의 그로기(Groggy) 상태 플레이어 감지 (카메라 방향과 무관하게 일정 거리 내에 있으면 살리기 가능)
	// 혹은 시야에 들어온 플레이어가 거리가 좀 멀어도 부활 가능하도록 범위 설정
	float ReviveDistance = 300.0f; // 살리기 가능한 반경
	bool bDrawDebug = true; // 디버그 라인 출력 켜기/끄기
	
	if (bDrawDebug)
	{
		DrawDebugSphere(GetWorld(), OwnerActor->GetActorLocation(), ReviveDistance, 16, FColor::Green, false, -1.0f, 0, 1.0f);
	}

	// 겹치는 캐릭터 찾기
	TArray<AActor*> OverlappedActors;
	UKismetSystemLibrary::SphereOverlapActors(
		this,
		OwnerActor->GetActorLocation(),
		ReviveDistance,
		{ UEngineTypes::ConvertToObjectType(ECC_Pawn) },
		AMainCharacter::StaticClass(),
		{ OwnerActor },
		OverlappedActors
	);

	for (AActor* Actor : OverlappedActors)
	{
		if (AMainCharacter* OtherChar = Cast<AMainCharacter>(Actor))
		{
			// 대상이 그로기 상태이고 죽지 않았다면 상호작용 대상으로 최우선 지정
			if (OtherChar->bIsGroggy && !OtherChar->bIsDead)
			{
				NewFocusedActor = OtherChar;
				if (bDrawDebug)
				{
					DrawDebugLine(GetWorld(), OwnerActor->GetActorLocation(), OtherChar->GetActorLocation(), FColor::Red, false, -1.0f, 0, 3.0f);
				}
				break; // 한 명만 찾으면 종료
			}
		}
	}

	if (FocusedActor != NewFocusedActor)
	{
		// 이전 포커스 대상의 UI 끄기
		if (AMainCharacter* OldChar = Cast<AMainCharacter>(FocusedActor))
		{
			OldChar->SetReviveUIVisibility(false);
		}

		FocusedActor = NewFocusedActor;

		// 새 포커스 대상의 UI 켜기
		if (AMainCharacter* NewChar = Cast<AMainCharacter>(FocusedActor))
		{
			NewChar->SetReviveUIVisibility(true);
		}

		OnFocusedInteractableChanged.Broadcast(FocusedActor);
	}
}
