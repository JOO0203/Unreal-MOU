#include "Components/InteractionComponent.h"
#include "Interfaces/InteractableInterface.h"
#include "Interfaces/PushableInterface.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

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
	if (PawnOwner && PawnOwner->GetController())
	{
		PawnOwner->GetController()->GetPlayerViewPoint(StartLocation, ViewRotation);
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
	if (bHit && HitResult.GetActor())
	{
		// 대상이 상호작용 가능하거나 밀 수 있는 대상이라면 포커스 등록
		if (HitResult.GetActor()->Implements<UInteractableInterface>() || HitResult.GetActor()->Implements<UPushableInterface>())
		{
			NewFocusedActor = HitResult.GetActor();
		}
	}

	if (FocusedActor != NewFocusedActor)
	{
		FocusedActor = NewFocusedActor;
		OnFocusedInteractableChanged.Broadcast(FocusedActor);
	}
}
