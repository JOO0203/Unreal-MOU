#include "Item/ConsumableItemBase.h"
#include "Net/UnrealNetwork.h"

AConsumableItemBase::AConsumableItemBase()
{
}

void AConsumableItemBase::BeginPlay()
{
	Super::BeginPlay();

	// 복제 카운트를 MaxUseCount로 초기화 (ItemBase가 CurrentUseCount=MaxUseCount 한 뒤)
	ConsumeUseCount = MaxUseCount;
}

void AConsumableItemBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AConsumableItemBase, ConsumeUseCount);
}

// [CONSUME-010] 집을 때 소유권 설정 (클라 → 서버 RPC 가능하게)
void AConsumableItemBase::PickUp_Implementation(AActor* Picker)
{
	Super::PickUp_Implementation(Picker);

	// PickUp은 서버에서 실행됨. Owner를 든 플레이어로 설정 → 그 플레이어 클라가 ServerRPC 전송 가능
	if (HasAuthority() && Picker)
	{
		SetOwner(Picker);
	}
}

// [CONSUME-011] 놓을 때 소유권 해제
void AConsumableItemBase::Drop_Implementation(FVector DropLocation)
{
	if (HasAuthority())
	{
		SetOwner(nullptr);
	}

	Super::Drop_Implementation(DropLocation);
}

// [CONSUME-012] 던질 때 소유권 해제
void AConsumableItemBase::Throw_Implementation(FVector ThrowVelocity)
{
	if (HasAuthority())
	{
		SetOwner(nullptr);
	}

	Super::Throw_Implementation(ThrowVelocity);
}

// [CONSUME-000] 좌클릭: 소비는 항상 서버에서 처리 (차감/판정/복제 신뢰성)
void AConsumableItemBase::OnUse_Implementation()
{
	// 차감·효과는 서버에서만. 클라에서 불리면 ServerConsume로 넘겨 서버가 처리한다.
	// (Super::OnUse는 부르지 않음 - ItemBase의 CurrentUseCount 대신 복제되는 ConsumeUseCount 사용)
	if (HasAuthority())
	{
		TryConsumeOnServer();
	}
	else
	{
		ServerConsume();
	}
}

// [CONSUME-009] 서버 공통 진입점: 잔여 체크 → 차감(복제) → ApplyEffect → 소진 시 Destroy
void AConsumableItemBase::TryConsumeOnServer()
{
	// 서버에서만 실행되어야 함
	if (!HasAuthority())
	{
		return;
	}

	if (ConsumeUseCount <= 0)
	{
		return;
	}

	// 서버에서 차감 → ConsumeUseCount 복제되어 모든 클라 화면에 동기화
	ConsumeUseCount--;

	// 실제 효과 적용 (자식 override)
	ApplyEffect();

	// 효과 연출(VFX/사운드)을 모든 클라에서 재생
	MulticastPlayUseEffect();

	// 다 쓰면 아이템 소멸
	if (ConsumeUseCount <= 0)
	{
		Destroy();
	}
}

// [CONSUME-008] 클라이언트 → 서버 소비 위임 (서버에서 차감+효과)
void AConsumableItemBase::ServerConsume_Implementation()
{
	TryConsumeOnServer();
}

// 복제된 ConsumeUseCount가 각 클라에 도착했을 때 호출 (UI 갱신 등 훅으로 활용 가능)
void AConsumableItemBase::OnRep_ConsumeUseCount()
{
}

// [CONSUME-007] 실제 소비 효과 (기본 빈 구현, 자식이 override)
void AConsumableItemBase::ApplyEffect_Implementation()
{
	// 자식 클래스에서 구현 (포션=회복, 수리=수리 등)
	// ResolveEffectTarget()으로 효과 대상을 얻어 처리하면 됨.
}

// [CONSUME-005] 효과 연출 멀티캐스트 → BP 훅 호출
void AConsumableItemBase::MulticastPlayUseEffect_Implementation()
{
	OnUseEffect();
}

// [CONSUME-004] 효과 대상 조회
AActor* AConsumableItemBase::ResolveEffectTarget() const
{
	switch (TargetMode)
	{
	case EConsumeTarget::SelfOnly:
		// 자기 자신 = 마지막으로 이 아이템을 든 플레이어
		return LastOwner;

	case EConsumeTarget::FocusedTarget:
	default:
		// 앞의 대상 탐색은 추후 구현 (치료/수리 도구 만들 때 override)
		return nullptr;
	}
}
