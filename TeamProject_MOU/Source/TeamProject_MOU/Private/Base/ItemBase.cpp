#include "Base/ItemBase.h"
#include "Components/StaticMeshComponent.h"

AItemBase::AItemBase()
{
	PrimaryActorTick.bCanEverTick = true;

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	RootComponent = MeshComponent;
	
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

void AItemBase::PickUp_Implementation(AActor* Picker)
{
	LastOwner = Picker;
	
	// 물리 비활성화 및 충돌 무시 설정
	MeshComponent->SetSimulatePhysics(false);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	
	// 여기서 플레이어의 인벤토리 및 CarryingComponent 연동을 진행합니다.
	// (추후 MainCharacter 및 Component에서 호출 처리 연동)
}

void AItemBase::Drop_Implementation(FVector DropLocation)
{
	SetActorLocation(DropLocation);
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	
	// 물리 재활성화
	MeshComponent->SetSimulatePhysics(true);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
}

void AItemBase::Throw_Implementation(FVector ThrowVelocity)
{
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	
	MeshComponent->SetSimulatePhysics(true);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComponent->AddImpulse(ThrowVelocity, NAME_None, true);
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
