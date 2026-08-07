#include "Base/PackageBase.h"
#include "Base/CharacterBase.h"
#include "Base/BaseAttributeSet.h"
#include "AbilitySystemComponent.h"

APackageBase::APackageBase()
{
	PrimaryActorTick.bCanEverTick = true;
}

void APackageBase::BeginPlay()
{
	Super::BeginPlay();
	
	CurrentSpoilTime = MaxSpoilTime;
}

void APackageBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 상하기 쉬운 물품의 경우 유통기한 감소
	if (PackageType == EPackageType::Perishable && CurrentSpoilTime > 0.0f)
	{
		CurrentSpoilTime -= DeltaTime;
		if (CurrentSpoilTime <= 0.0f)
		{
			// 유통기한 만료 처리 (예: 가치 하락, 파괴 등)
			BaseValue = FMath::Max(0, BaseValue / 2);
		}
	}
}

void APackageBase::AddCarrier(AActor* Carrier)
{
	if (Carrier && !CurrentCarriers.Contains(Carrier))
	{
		CurrentCarriers.Add(Carrier);
		UpdateCarriersSpeedModifier();
	}
}

void APackageBase::RemoveCarrier(AActor* Carrier)
{
	if (Carrier && CurrentCarriers.Contains(Carrier))
	{
		// 이탈한 플레이어의 속도 배율을 원래대로(1.0) 복구하는 로직은
		// Carrier의 GAS 시스템에서 해당 Effect를 지우는 방식으로 블루프린트나 C++에서 처리
		
		// 운반자 이탈 시 분배되었던 무게를 원상복구
		if (AppliedWeightMap.Contains(Carrier))
		{
			if (ACharacterBase* BaseChar = Cast<ACharacterBase>(Carrier))
			{
				if (UBaseAttributeSet* AttrSet = BaseChar->BaseAttribute)
				{
					AttrSet->SetCurrentWeight(FMath::Max(0.0f, AttrSet->GetCurrentWeight() - AppliedWeightMap[Carrier]));
				}
			}
			AppliedWeightMap.Remove(Carrier);
		}

		CurrentCarriers.Remove(Carrier);
		UpdateCarriersSpeedModifier();
	}
}

void APackageBase::UpdateCarriersSpeedModifier()
{
	if (ItemWeight <= 0.0f)
	{
		return;
	}

	float TotalLiftPower = 0.0f;
	float WeightPerCarrier = CurrentCarriers.Num() > 0 ? (ItemWeight / CurrentCarriers.Num()) : 0.0f;

	// 참여한 모든 플레이어의 LiftPower 합산 및 무게 재분배
	for (AActor* Carrier : CurrentCarriers)
	{
		if (ACharacterBase* BaseChar = Cast<ACharacterBase>(Carrier))
		{
			if (UBaseAttributeSet* AttrSet = BaseChar->BaseAttribute)
			{
				TotalLiftPower += AttrSet->GetLiftPower();
				
				// 기존에 분배된 무게가 있다면 일단 차감 (재분배를 위함)
				if (AppliedWeightMap.Contains(Carrier))
				{
					AttrSet->SetCurrentWeight(FMath::Max(0.0f, AttrSet->GetCurrentWeight() - AppliedWeightMap[Carrier]));
				}

				// 새로운 N분의 1 무게 부여
				AttrSet->SetCurrentWeight(AttrSet->GetCurrentWeight() + WeightPerCarrier);
				AppliedWeightMap.Add(Carrier, WeightPerCarrier);
			}
		}
	}

	if (CurrentCarriers.Num() > 0)
	{
		// (총 드는 힘 / 택배 무게) 비율 계산 (최대 1.0 = 정상 속도)
		float SpeedRatio = FMath::Clamp(TotalLiftPower / ItemWeight, 0.1f, 1.0f);

		// TODO: 계산된 SpeedRatio를 각 Carrier에게 GameplayEffect(Modifier)로 적용하여 속도 저하
		UE_LOG(LogTemp, Log, TEXT("택배 운반 인원: %d명, 총 힘: %f, 인당 분배 무게: %f -> 속도 배율: %f"), 
			CurrentCarriers.Num(), TotalLiftPower, WeightPerCarrier, SpeedRatio);
	}
}

void APackageBase::OnUse_Implementation()
{
	// 택배는 사용이 불가능하므로 아무것도 하지 않음 (또는 안내 메시지 출력)
}

float APackageBase::GetPushResistance_Implementation() const
{
	return ItemWeight;
}

void APackageBase::Push_Implementation(AActor* Pusher, FVector PushDirection)
{
	// 바닥에 놓여있을 때만 밀기 가능
	if (CurrentCarriers.Num() == 0)
	{
		// 블루프린트나 C++에서 Lerp 또는 RootMotion 기반 이동 로직을 추가
		// 물리 기반 임펄스 대신 정해진 애니메이션과 거리만큼 이동하게 할 예정
		UE_LOG(LogTemp, Log, TEXT("택배가 %s 방향으로 밀렸습니다! 저항: %f"), *PushDirection.ToString(), ItemWeight);
	}
}
