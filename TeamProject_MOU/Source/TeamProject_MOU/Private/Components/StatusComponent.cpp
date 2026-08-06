#include "Components/StatusComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"

UStatusComponent::UStatusComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UStatusComponent::BeginPlay()
{
	Super::BeginPlay();

	// 소유자 Actor로부터 AbilitySystemComponent 참조 가져오기
	AActor* OwnerActor = GetOwner();
	if (OwnerActor)
	{
		IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(OwnerActor);
		if (ASI)
		{
			AbilitySystemComponent = ASI->GetAbilitySystemComponent();
		}
	}
}

void UStatusComponent::AddStatusTag(FGameplayTag Tag)
{
	if (!Tag.IsValid())
	{
		return;
	}

	if (!ActiveStatusTags.HasTagExact(Tag))
	{
		ActiveStatusTags.AddTag(Tag);

		// GAS AbilitySystemComponent에도 Loose Tag 추가 (GAS 능력 및 GE 조건 연동)
		if (AbilitySystemComponent)
		{
			AbilitySystemComponent->AddLooseGameplayTag(Tag);
		}

		OnStatusTagChanged.Broadcast(Tag, true);
	}
}

void UStatusComponent::RemoveStatusTag(FGameplayTag Tag)
{
	if (!Tag.IsValid())
	{
		return;
	}

	if (ActiveStatusTags.HasTagExact(Tag))
	{
		ActiveStatusTags.RemoveTag(Tag);

		// GAS AbilitySystemComponent에서 Loose Tag 제거
		if (AbilitySystemComponent)
		{
			AbilitySystemComponent->RemoveLooseGameplayTag(Tag);
		}

		OnStatusTagChanged.Broadcast(Tag, false);
	}
}

bool UStatusComponent::HasStatusTag(FGameplayTag Tag) const
{
	if (!Tag.IsValid())
	{
		return false;
	}

	return ActiveStatusTags.HasTagExact(Tag);
}

bool UStatusComponent::CanMove() const
{
	// 기절, 잡힘, 넉백 태그 보유 시 이동 불가
	static const FGameplayTag StunTag = FGameplayTag::RequestGameplayTag(FName("State.Stunned"), false);
	static const FGameplayTag HeldTag = FGameplayTag::RequestGameplayTag(FName("State.Held"), false);
	static const FGameplayTag KnockedTag = FGameplayTag::RequestGameplayTag(FName("State.KnockedBack"), false);

	if ((StunTag.IsValid() && HasStatusTag(StunTag)) ||
		(HeldTag.IsValid() && HasStatusTag(HeldTag)) ||
		(KnockedTag.IsValid() && HasStatusTag(KnockedTag)))
	{
		return false;
	}

	return true;
}

bool UStatusComponent::CanAct() const
{
	// 기절, 잡힘 태그 보유 시 일반 상호작용 및 공격 행동 불가
	static const FGameplayTag StunTag = FGameplayTag::RequestGameplayTag(FName("State.Stunned"), false);
	static const FGameplayTag HeldTag = FGameplayTag::RequestGameplayTag(FName("State.Held"), false);

	if ((StunTag.IsValid() && HasStatusTag(StunTag)) ||
		(HeldTag.IsValid() && HasStatusTag(HeldTag)))
	{
		return false;
	}

	return true;
}

bool UStatusComponent::CanSprint() const
{
	// 이동 불가 상태이거나 스태미나 고갈 태그가 있을 때 달리기 불가
	static const FGameplayTag ExhaustedTag = FGameplayTag::RequestGameplayTag(FName("State.Exhausted"), false);

	if (!CanMove() || (ExhaustedTag.IsValid() && HasStatusTag(ExhaustedTag)))
	{
		return false;
	}

	return true;
}

bool UStatusComponent::CanCarry() const
{
	// 팔 중상 손상 태그 보유 시 운반 불가능
	static const FGameplayTag ArmTag = FGameplayTag::RequestGameplayTag(FName("Debuff.ArmDamaged"), false);

	if (!CanAct() || (ArmTag.IsValid() && HasStatusTag(ArmTag)))
	{
		return false;
	}

	return true;
}
