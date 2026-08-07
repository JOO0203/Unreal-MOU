#include "Components/CarryingComponent.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"

UCarryingComponent::UCarryingComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UCarryingComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UCarryingComponent::GrabOrDrop()
{
	if (IsCarrying())
	{
		// Drop implementation stub
		CarriedActor = nullptr;
		OnCarriedStateChanged.Broadcast(nullptr);
	}
	else
	{
		// Grab implementation stub
	}
}

void UCarryingComponent::Throw()
{
	if (!IsCarrying())
	{
		return;
	}

	// Throw implementation stub
	CarriedActor = nullptr;
	OnCarriedStateChanged.Broadcast(nullptr);
}
