#include "Components/StatusComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "Abilities/GameplayAbility.h"
#include "StatusEffect/StatusEffectAbilitySetDataAsset.h"
#include "StatusEffect/StatusEffectDataAsset.h"

UStatusComponent::UStatusComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UStatusComponent::BeginPlay()
{
	Super::BeginPlay();

	AbilitySystemComponent = FindOwnerAbilitySystemComponent();
	GrantStatusEffectAbilities();
}

UAbilitySystemComponent* UStatusComponent::FindOwnerAbilitySystemComponent() const
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return nullptr;
	}

	if (const IAbilitySystemInterface* AbilitySystemInterface = Cast<IAbilitySystemInterface>(OwnerActor))
	{
		if (UAbilitySystemComponent* OwnerASC = AbilitySystemInterface->GetAbilitySystemComponent())
		{
			return OwnerASC;
		}
	}

	return OwnerActor->FindComponentByClass<UAbilitySystemComponent>();
}

void UStatusComponent::GrantStatusEffectAbilities()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority() || !AbilitySetData)
	{
		return;
	}

	if (!AbilitySystemComponent)
	{
		AbilitySystemComponent = FindOwnerAbilitySystemComponent();
	}

	if (!AbilitySystemComponent)
	{
		return;
	}

	const int32 AbilityLevel = FMath::Max(1, AbilitySetData->AbilityLevel);
	for (const TSubclassOf<UGameplayAbility>& AbilityClass : AbilitySetData->Abilities)
	{
		if (!AbilityClass || AbilitySystemComponent->FindAbilitySpecFromClass(AbilityClass))
		{
			continue;
		}

		const FGameplayAbilitySpecHandle GrantedHandle = AbilitySystemComponent->GiveAbility(
			FGameplayAbilitySpec(AbilityClass, AbilityLevel));

		if (GrantedHandle.IsValid())
		{
			GrantedAbilityHandles.Add(GrantedHandle);
		}
	}
}

UStatusEffectDataAsset* UStatusComponent::GetStatusEffectData(EStatusEffectType EffectType) const
{
	if (!AbilitySetData)
	{
		return nullptr;
	}

	for (UStatusEffectDataAsset* EffectData : AbilitySetData->EffectDataAssets)
	{
		if (EffectData && EffectData->EffectType == EffectType)
		{
			return EffectData;
		}
	}

	return nullptr;
}

bool UStatusComponent::HasStatusTag(FGameplayTag Tag) const
{
	return AbilitySystemComponent && Tag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(Tag);
}

FGameplayTagContainer UStatusComponent::GetActiveStatusTags() const
{
	FGameplayTagContainer OwnedTags;
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->GetOwnedGameplayTags(OwnedTags);
	}
	return OwnedTags;
}

void UStatusComponent::AddStatusTag(FGameplayTag Tag)
{
	if (AbilitySystemComponent && Tag.IsValid())
	{
		AbilitySystemComponent->AddLooseGameplayTag(Tag);
	}
}

void UStatusComponent::RemoveStatusTag(FGameplayTag Tag)
{
	if (AbilitySystemComponent && Tag.IsValid())
	{
		AbilitySystemComponent->RemoveLooseGameplayTag(Tag);
	}
}

bool UStatusComponent::CanMove() const
{
	static const FGameplayTag StunTag = FGameplayTag::RequestGameplayTag(FName("State.Primary.Stuned"), false);
	static const FGameplayTag HeldTag = FGameplayTag::RequestGameplayTag(FName("State.Held"), false);
	static const FGameplayTag KnockedTag = FGameplayTag::RequestGameplayTag(FName("State.KnockedBack"), false);

	return !HasStatusTag(StunTag) && !HasStatusTag(HeldTag) && !HasStatusTag(KnockedTag);
}

bool UStatusComponent::CanAct() const
{
	static const FGameplayTag StunTag = FGameplayTag::RequestGameplayTag(FName("State.Primary.Stuned"), false);
	static const FGameplayTag HeldTag = FGameplayTag::RequestGameplayTag(FName("State.Held"), false);

	return !HasStatusTag(StunTag) && !HasStatusTag(HeldTag);
}

bool UStatusComponent::CanSprint() const
{
	static const FGameplayTag ExhaustedTag = FGameplayTag::RequestGameplayTag(FName("State.Exhausted"), false);
	return CanMove() && !HasStatusTag(ExhaustedTag);
}

bool UStatusComponent::CanCarry() const
{
	static const FGameplayTag ArmTag = FGameplayTag::RequestGameplayTag(FName("Debuff.ArmDamaged"), false);
	return CanAct() && !HasStatusTag(ArmTag);
}
