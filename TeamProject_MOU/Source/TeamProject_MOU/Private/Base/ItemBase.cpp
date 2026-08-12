#include "Base/ItemBase.h"
#include "Components/StaticMeshComponent.h"

AItemBase::AItemBase()
{
	PrimaryActorTick.bCanEverTick = true;
	
	// [멀티플레이] 아이템 상태 및 이동 동기화 필수 설정
	bReplicates = true;
	SetReplicateMovement(true);

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	RootComponent = MeshComponent;
	
	// 물리 연산 및 어태치를 위해 Mobility를 Movable로 필수 설정
	MeshComponent->SetMobility(EComponentMobility::Movable);
	
	// 기본 물리 설정 (바닥에 놓여있는 상태를 기본으로 가정)
	MeshComponent->SetSimulatePhysics(true);
	MeshComponent->SetCollisionProfileName(TEXT("PhysicsActor"));
	
	// 외곽선 렌더링을 위한 커스텀 뎁스 비활성화 (포커스 시 활성화 예정)
	MeshComponent->SetRenderCustomDepth(false);
}

void AItemBase::BeginPlay()
{
	Super::BeginPlay();
	CurrentUseCount = MaxUseCount;
	CurrentDurability = MaxDurability;
}

void AItemBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

bool AItemBase::CanInteract_Implementation(AActor* Interactor) const
{
	return true;
}

void AItemBase::Interact_Implementation(AActor* Interactor)
{
	PickUp(Interactor);
}

FText AItemBase::GetInteractPrompt_Implementation() const
{
	return FText::Format(NSLOCTEXT("Interaction", "PickupPrompt", "{0} 줍기"), ItemName);
}

void AItemBase::MulticastPickUp_Implementation(AActor* Picker)
{
	// 모든 클라이언트에서 물리 비활성화, 충돌은 유지(다른 플레이어에게 막힘)
	MeshComponent->SetSimulatePhysics(false);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	
	if (Picker)
	{
		// 짐을 든 본인(Picker)과 택배 간의 충돌만 무시 (다른 플레이어는 여전히 막힘)
		MeshComponent->IgnoreActorWhenMoving(Picker, true);
		// 캐릭터의 CapsuleComponent(루트)에도 역방향 무시 설정
		TArray<UPrimitiveComponent*> PickerPrimComps;
		Picker->GetComponents<UPrimitiveComponent>(PickerPrimComps);
		for (UPrimitiveComponent* Comp : PickerPrimComps)
		{
			Comp->IgnoreActorWhenMoving(this, true);
		}
	}
}

void AItemBase::PickUp_Implementation(AActor* Picker)
{
	LastOwner = Picker;
	
	// 물리 비활성화는 모든 클라이언트가 알아야 함
	MulticastPickUp(Picker);
	
	// 여기서 플레이어의 인벤토리 및 CarryingComponent 연동을 진행합니다.
	// (추후 MainCharacter 및 Component에서 호출 처리 연동)
}

void AItemBase::MulticastDrop_Implementation(FVector DropLocation)
{
	SetActorLocation(DropLocation);
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	
	// 물리 재활성화
	MeshComponent->SetSimulatePhysics(true);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	
	// 운반할 때 설정했던 충돌 무시 옵션 해제
	MeshComponent->ClearMoveIgnoreActors();
}

void AItemBase::Drop_Implementation(FVector DropLocation)
{
	MulticastDrop(DropLocation);
}

void AItemBase::MulticastThrow_Implementation(FVector ThrowVelocity)
{
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	
	MeshComponent->SetSimulatePhysics(true);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComponent->AddImpulse(ThrowVelocity, NAME_None, true);
}

void AItemBase::Throw_Implementation(FVector ThrowVelocity)
{
	MulticastThrow(ThrowVelocity);
}

void AItemBase::OnUse_Implementation()
{
	// 일반 아이템 사용 로직 (자식 클래스에서 구현)
	if (CurrentUseCount > 0)
	{
		CurrentUseCount--;
	}
}

void AItemBase::OnEquipped_Implementation(AActor* Equipper)
{
	// 메시를 보이게 하거나 특정 소켓에 어태치
	SetActorHiddenInGame(false);
}

void AItemBase::OnUnequipped_Implementation(AActor* Equipper)
{
	// 메시를 숨김 (인벤토리에 보관된 상태)
	SetActorHiddenInGame(true);
}
