#include "Item/PotionItem.h"
#include "Base/CharacterBase.h"
#include "Player/MainCharacter.h"
#include "Components/StatusComponent.h"
#include "Components/StaticMeshComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "GameplayEffect.h"
#include "Kismet/KismetSystemLibrary.h"
#include "TimerManager.h"

APotionItem::APotionItem()
{
	// 포션은 자기 자신에게 효과를 준다 (치료 도구 등은 FocusedTarget으로 override)
	TargetMode = EConsumeTarget::SelfOnly;
}

// [POTION-001] 소비 효과: 자기 사용 시 대상 하나(자신)에게 적용
// (부모 TryConsumeOnServer에서 서버 권한으로만 호출됨)
void APotionItem::ApplyEffect_Implementation()
{
	// 효과 대상 (SelfOnly면 든 플레이어 = LastOwner)
	ApplyPotionEffectToTarget(ResolveEffectTarget());
}

// 실제 GE 적용 + 상태이상 태그 제거를 한 대상에게 수행 (자기 사용 / 광역 공용)
void APotionItem::ApplyPotionEffectToTarget(AActor* Target)
{
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

	// 상태이상 태그 제거 (감전 State.CC.Electirc 등) - StatusComponent 경유
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

	// 상태이상 태그 부여 (감전 등) - 테이저와 동일하게 Loose 태그, 대상 타이머로 해제
	if (!TagsToApply.IsEmpty())
	{
		if (ACharacterBase* TargetCharacter = Cast<ACharacterBase>(Target))
		{
			if (UStatusComponent* Status = TargetCharacter->GetStatusComponent())
			{
				TArray<FGameplayTag> ApplyArray;
				TagsToApply.GetGameplayTagArray(ApplyArray);
				for (const FGameplayTag& Tag : ApplyArray)
				{
					Status->AddStatusTag(Tag);
				}

				// 지속시간 후 해제 예약 (대상 캐릭터 타이머 - 포션이 소멸돼도 유지)
				if (AppliedTagDuration > 0.0f)
				{
					TWeakObjectPtr<UStatusComponent> WeakStatus = Status;
					FGameplayTagContainer TagsCopy = TagsToApply;
					FTimerDelegate ClearDelegate = FTimerDelegate::CreateLambda([WeakStatus, TagsCopy]()
					{
						if (WeakStatus.IsValid())
						{
							TArray<FGameplayTag> ClearArray;
							TagsCopy.GetGameplayTagArray(ClearArray);
							for (const FGameplayTag& T : ClearArray)
							{
								WeakStatus->RemoveStatusTag(T);
							}
						}
					});
					FTimerHandle TmpHandle;
					TargetCharacter->GetWorldTimerManager().SetTimer(TmpHandle, ClearDelegate, AppliedTagDuration, false);
				}
			}
		}
	}
}

// [POTION-002] 투척: 부모(소유권 해제 + 물리 투척) 후, 투척형이면 충돌 감지 켜기
void APotionItem::Throw_Implementation(FVector ThrowVelocity, AActor* Thrower)
{
	Super::Throw_Implementation(ThrowVelocity, Thrower);

	if (bApplyOnImpact && HasAuthority() && MeshComponent)
	{
		bHasImpacted = false;
		MeshComponent->SetNotifyRigidBodyCollision(true); // OnComponentHit 활성화
		MeshComponent->OnComponentHit.AddDynamic(this, &APotionItem::OnImpact);
	}
}

// [POTION-003] 첫 충돌 → 반경 내 플레이어 전원에게 적용 → 깨짐
void APotionItem::OnImpact(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	FVector NormalImpulse, const FHitResult& Hit)
{
	if (!HasAuthority() || bHasImpacted)
	{
		return;
	}

	// 던진 본인과 손에서 나가자마자 충돌한 경우 자폭 방지
	if (OtherActor && OtherActor == LastOwner)
	{
		return;
	}

	bHasImpacted = true;

	// 반경 내 모든 플레이어(AMainCharacter)에게 효과 적용
	TArray<AActor*> Overlapped;
	TArray<AActor*> IgnoreActors;
	UKismetSystemLibrary::SphereOverlapActors(
		this, GetActorLocation(), ImpactRadius,
		{ UEngineTypes::ConvertToObjectType(ECC_Pawn) },
		AMainCharacter::StaticClass(), IgnoreActors, Overlapped);

	for (AActor* Actor : Overlapped)
	{
		ApplyPotionEffectToTarget(Actor);
	}

	// 깨짐 연출(전 클라, OnUseEffect BP 훅) 후 소멸
	MulticastPlayUseEffect();
	Destroy();
}
