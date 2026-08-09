#include "Components/GrabFollowComponent.h"

#include "Components/StatusComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"

UGrabFollowComponent::UGrabFollowComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UGrabFollowComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UGrabFollowComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (CarrierCharacter)
	{
		SyncGrabTransform();
	}
}

void UGrabFollowComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UGrabFollowComponent, CarrierCharacter);
	DOREPLIFETIME(UGrabFollowComponent, GrabSocketName);
	DOREPLIFETIME(UGrabFollowComponent, RelativeOffset);
	DOREPLIFETIME(UGrabFollowComponent, bFollowRotation);
}

void UGrabFollowComponent::StartGrabFollow(ACharacter* NewCarrier, FName NewSocketName, FVector NewRelativeOffset, bool bInheritRotation)
{
	CarrierCharacter = NewCarrier;
	GrabSocketName = NewSocketName;
	RelativeOffset = NewRelativeOffset;
	bFollowRotation = bInheritRotation;

	ApplyHeldTag(true);
	SyncGrabTransform();
}

void UGrabFollowComponent::StopGrabFollow()
{
	CarrierCharacter = nullptr;
	ApplyHeldTag(false);
}

void UGrabFollowComponent::OnRep_GrabState()
{
	if (CarrierCharacter)
	{
		SyncGrabTransform();
	}
}

void UGrabFollowComponent::SyncGrabTransform()
{
	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (!OwnerCharacter || !CarrierCharacter)
	{
		return;
	}

	USkeletalMeshComponent* CarrierMesh = CarrierCharacter->GetMesh();
	FTransform TargetTransform = CarrierCharacter->GetActorTransform();

	if (CarrierMesh && CarrierMesh->DoesSocketExist(GrabSocketName))
	{
		TargetTransform = CarrierMesh->GetSocketTransform(GrabSocketName, RTS_World);
	}

	const FVector TargetLocation = TargetTransform.TransformPosition(RelativeOffset);
	const FRotator TargetRotation = bFollowRotation ? TargetTransform.GetRotation().Rotator() : OwnerCharacter->GetActorRotation();

	OwnerCharacter->SetActorLocationAndRotation(TargetLocation, TargetRotation, false, nullptr, ETeleportType::TeleportPhysics);

	if (UCharacterMovementComponent* MovementComponent = OwnerCharacter->GetCharacterMovement())
	{
		MovementComponent->StopMovementImmediately();
	}
}

void UGrabFollowComponent::ApplyHeldTag(bool bAdd) const
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	const FGameplayTag HeldTag = FGameplayTag::RequestGameplayTag(FName("State.Held"), false);
	if (!HeldTag.IsValid())
	{
		return;
	}

	if (ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
	{
		if (UStatusComponent* StatusComponent = OwnerCharacter->FindComponentByClass<UStatusComponent>())
		{
			if (bAdd)
			{
				StatusComponent->AddStatusTag(HeldTag);
			}
			else
			{
				StatusComponent->RemoveStatusTag(HeldTag);
			}
		}
	}
}
