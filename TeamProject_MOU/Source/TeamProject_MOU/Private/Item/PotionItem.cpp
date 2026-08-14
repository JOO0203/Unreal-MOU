#include "Item/PotionItem.h"
#include "Base/CharacterBase.h"
#include "Components/StatusComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "GameplayEffect.h"

APotionItem::APotionItem()
{
	// 포션은 자기 자신에게 효과를 준다 (치료 도구 등은 FocusedTarget으로 override)
	TargetMode = EConsumeTarget::SelfOnly;
}

// [POTION-001] 소비 효과: 대상 ASC에 GE 적용 + 상태이상 태그 제거
// (부모 TryConsumeOnServer에서 서버 권한으로만 호출됨)
void APotionItem::ApplyEffect_Implementation()
{
	// 효과 대상 (SelfOnly면 든 플레이어 = LastOwner)
	AActor* Target = ResolveEffectTarget();
	if (!Target)
	{
		return;
	}

	// 대상의 AbilitySystemComponent 획득 (ACharacterBase는 IAbilitySystemInterface 구현)
	UAbilitySystemComponent* TargetASC = nullptr;
	if (IAbilitySystemInterface* AscInterface = Cast<IAbilitySystemInterface>(Target))
	{
		TargetASC = AscInterface->GetAbilitySystemComponent();
	}

	// GameplayEffect 목록 적용 (지속시간/원복/복제는 GAS가 처리)
	if (TargetASC)
	{
		FGameplayEffectContextHandle Context = TargetASC->MakeEffectContext();
		Context.AddSourceObject(this);

		for (const TSubclassOf<UGameplayEffect>& EffectClass : EffectsToApply)
		{
			if (!EffectClass)
			{
				continue;
			}

			FGameplayEffectSpecHandle SpecHandle = TargetASC->MakeOutgoingSpec(EffectClass, EffectLevel, Context);
			if (SpecHandle.IsValid())
			{
				TargetASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
			}
		}
	}

	// 상태이상 태그 제거 (Slowed, Exhausted, CC.Stuned 등) - StatusComponent 경유
	if (!TagsToRemove.IsEmpty())
	{
		if (ACharacterBase* TargetCharacter = Cast<ACharacterBase>(Target))
		{
			if (UStatusComponent* Status = TargetCharacter->GetStatusComponent())
			{
				TArray<FGameplayTag> TagArray;
				TagsToRemove.GetGameplayTagArray(TagArray);
				for (const FGameplayTag& Tag : TagArray)
				{
					Status->RemoveStatusTag(Tag);
				}
			}
		}
	}
}
