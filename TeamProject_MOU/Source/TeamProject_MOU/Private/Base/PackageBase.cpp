#include "Base/PackageBase.h"
#include "Base/CharacterBase.h"
#include "Base/BaseAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Components/CarryingComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"

APackageBase::APackageBase()
{
	PrimaryActorTick.bCanEverTick = true;

	// 운반을 위한 전방/후방 손잡이 생성 (일반/무거운 물품 공통 기반)
	Handle_Front = CreateDefaultSubobject<USceneComponent>(TEXT("Handle_Front"));
	Handle_Front->SetupAttachment(RootComponent);

	Handle_Back = CreateDefaultSubobject<USceneComponent>(TEXT("Handle_Back"));
	Handle_Back->SetupAttachment(RootComponent);
}

void APackageBase::BeginPlay()
{
	Super::BeginPlay();
	
	CurrentSpoilTime = MaxSpoilTime;
}

void APackageBase::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// 메시에 물리 충돌 이벤트 바인딩
	if (MeshComponent)
	{
		MeshComponent->SetNotifyRigidBodyCollision(true); // Hit 이벤트 발생 허용
		MeshComponent->OnComponentHit.AddDynamic(this, &APackageBase::OnPackageHit);
	}
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
			BaseValue = FMath::Max(0, BaseValue / 2);
		}
	}

	// 서버에서만 운반 로직 수행
	if (HasAuthority())
	{
		// [Heavy 전용] 운반 중 패키지 위치/회전 업데이트
		if (PackageType == EPackageType::Heavy && CurrentCarriers.Num() > 0)
		{
			UpdateHeavyPackagePosition(DeltaTime);
		}

		// [협동 이탈 검사] 운반자들이 택배에서 너무 멀어지면 강제로 놓게 만듦
		if (CurrentCarriers.Num() > 0)
		{
			TArray<AActor*> CarriersToDrop;
			for (AActor* Carrier : CurrentCarriers)
			{
				if (FVector::DistXY(Carrier->GetActorLocation(), GetActorLocation()) > MaxCarryDistance)
				{
					CarriersToDrop.Add(Carrier);
				}
			}

			for (AActor* Carrier : CarriersToDrop)
			{
				if (UCarryingComponent* CarryComp = Carrier->FindComponentByClass<UCarryingComponent>())
				{
					CarryComp->GrabOrDrop();
				}
				else
				{
					RemoveCarrier(Carrier);
				}
				UE_LOG(LogTemp, Warning, TEXT("[%s] 운반자가 너무 멀어져 택배를 놓쳤습니다!"), *Carrier->GetName());
			}
		}

		// [내구도 시스템] 끌림 상태 지속 대미지
		if (!bIsBroken && CurrentCarriers.Num() > 0 && CurrentSpeedRatio < 0.5f)
		{
			DamagePackage(5.0f * DeltaTime);
		}
	}
}

void APackageBase::UpdateHeavyPackagePosition(float DeltaTime)
{
	if (CurrentCarriers.Num() == 0) return;

	AActor* FrontCarrier = CurrentCarriers[0];
	if (!FrontCarrier) return;

	ACharacter* FrontChar = Cast<ACharacter>(FrontCarrier);
	if (!FrontChar || !FrontChar->GetMesh()) return;

	// 소켓이 없을 경우 오른손 본(hand_r)을 직접 사용
	FVector FrontHandPos;
	if (FrontChar->GetMesh()->DoesSocketExist(CarrySocketName))
	{
		FrontHandPos = FrontChar->GetMesh()->GetSocketLocation(CarrySocketName);
	}
	else
	{
		// 소켓이 없으면 캐릭터 위치에서 앞쪽 60cm를 기본값으로 사용
		FrontHandPos = FrontCarrier->GetActorLocation()
			+ FrontCarrier->GetActorForwardVector() * 60.0f
			+ FVector(0, 0, 30.0f);
	}

	// Handle_Front의 로컬 오프셋 (이 값만큼 패키지 원점을 역방향으로 밀어야 함)
	FVector HFLocal = Handle_Front ? Handle_Front->GetRelativeLocation() : FVector::ZeroVector;

	if (CurrentCarriers.Num() == 1)
	{
		// [1인 운반] 캐릭터 전방 방향 + 뒷끝 20도 기울기 (바닥에 끌리는 느낌)
		FRotator DragRot = FrontCarrier->GetActorRotation();
		DragRot.Pitch = -20.0f; // 한쪽 끝이 바닥으로 처지는 효과

		// 핵심: 패키지가 회전된 상태에서 Handle_Front의 월드 위치 = 손 위치가 되도록
		// PackageOrigin = FrontHandPos - Rotate(HFLocal)
		FQuat PackageQuat = DragRot.Quaternion();
		FVector RotatedHF = PackageQuat.RotateVector(HFLocal);
		FVector NewPackageLoc = FrontHandPos - RotatedHF;

		SetActorLocationAndRotation(NewPackageLoc, DragRot, false, nullptr, ETeleportType::None);
	}
	else if (CurrentCarriers.Num() >= 2)
	{
		// [2인 운반] Handle_Front = 앞사람 손, Handle_Back = 뒷사람 손
		AActor* BackCarrier = CurrentCarriers[1];
		if (!BackCarrier) return;

		ACharacter* BackChar = Cast<ACharacter>(BackCarrier);
		if (!BackChar || !BackChar->GetMesh()) return;

		FVector BackHandPos;
		if (BackChar->GetMesh()->DoesSocketExist(CarrySocketName))
		{
			BackHandPos = BackChar->GetMesh()->GetSocketLocation(CarrySocketName);
		}
		else
		{
			BackHandPos = BackCarrier->GetActorLocation()
				+ BackCarrier->GetActorForwardVector() * 60.0f
				+ FVector(0, 0, 30.0f);
		}

		// 패키지 회전: 앞사람 손 → 뒷사람 손 방향
		FVector Dir = BackHandPos - FrontHandPos;
		if (Dir.IsNearlyZero()) return;

		FRotator PackageRot = Dir.Rotation();
		FQuat PackageQuat = PackageRot.Quaternion();

		// 핵심: Handle_Front 월드 위치 = FrontHandPos 가 되도록 패키지 원점 계산
		// PackageOrigin + Rotate(HFLocal) = FrontHandPos
		// → PackageOrigin = FrontHandPos - Rotate(HFLocal)
		FVector RotatedHF = PackageQuat.RotateVector(HFLocal);
		FVector NewPackageOrigin = FrontHandPos - RotatedHF;

		SetActorLocationAndRotation(NewPackageOrigin, PackageRot, false, nullptr, ETeleportType::None);
	}
}

void APackageBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APackageBase, CurrentCarriers);
	DOREPLIFETIME(APackageBase, bIsBroken);  // 파손 상태 복제: 클라이언트 접속 시에도 동기화
}

void APackageBase::OnRep_CurrentCarriers()
{
	// 클라이언트에서 운반자 목록이 변경될 때 수행할 로직 (필요시 추가)
}

void APackageBase::OnRep_bIsBroken()
{
	// 서버에서 bIsBroken이 true로 바뀌면 클라이언트에서 이 함수가 자동 호출됨
	if (bIsBroken)
	{
		// 블루프린트에서 구현한 파손 메시 교체 / 파티클 등 연출 실행
		OnPackageBroken();
	}
}

void APackageBase::AddCarrier(AActor* Carrier)
{
	if (!Carrier || CurrentCarriers.Contains(Carrier)) return;

	// [Heavy] 무거운 택배는 최대 2명까지만 운반 가능
	if (PackageType == EPackageType::Heavy && CurrentCarriers.Num() >= 2)
	{
		UE_LOG(LogTemp, Warning, TEXT("[%s] 무거운 택배는 최대 2명까지 운반 가능합니다."), *GetName());
		return;
	}

	if (CurrentCarriers.Num() == 0)
	{
		// 첫 번째 운반자: 물리 비활성화, 충돌은 운반자에게만 무시 설정
		MeshComponent->SetSimulatePhysics(false);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		// 운반자와 택배 충돌 상호 무시 (Carrier 돌보지 않도록)
		MeshComponent->IgnoreActorWhenMoving(Carrier, true);
		if (ACharacter* Char = Cast<ACharacter>(Carrier))
		{
			Char->GetCapsuleComponent()->IgnoreActorWhenMoving(this, true);
		}
	}
	else
	{
		// 두 번째 운반자 추가 시 충돌도 동일하게 무시
		MeshComponent->IgnoreActorWhenMoving(Carrier, true);
		if (ACharacter* Char = Cast<ACharacter>(Carrier))
		{
			Char->GetCapsuleComponent()->IgnoreActorWhenMoving(this, true);
		}
	}

	CurrentCarriers.Add(Carrier);
	UpdateCarriersSpeedModifier();
}

void APackageBase::RemoveCarrier(AActor* Carrier)
{
	if (!Carrier || !CurrentCarriers.Contains(Carrier)) return;

	// 무게 원상복구
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

	// 충돌 무시 해제
	MeshComponent->IgnoreActorWhenMoving(Carrier, false);
	if (ACharacter* Char = Cast<ACharacter>(Carrier))
	{
		Char->GetCapsuleComponent()->IgnoreActorWhenMoving(this, false);
	}

	CurrentCarriers.Remove(Carrier);
	UpdateCarriersSpeedModifier();

	// 아무도 들고 있지 않게 되면 물리 재활성
	if (CurrentCarriers.Num() == 0)
	{
		// Attach 해제 후 물리 재활성
		DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		MeshComponent->SetSimulatePhysics(true);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		MeshComponent->ClearMoveIgnoreActors();
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
		CurrentSpeedRatio = FMath::Clamp(TotalLiftPower / ItemWeight, 0.1f, 1.0f);

		// TODO: 계산된 SpeedRatio를 각 Carrier에게 GameplayEffect(Modifier)로 적용하여 속도 저하
		UE_LOG(LogTemp, Log, TEXT("택배 운반 인원: %d명, 총 힘: %f, 인당 분배 무게: %f -> 속도 배율: %f"), 
			CurrentCarriers.Num(), TotalLiftPower, WeightPerCarrier, CurrentSpeedRatio);
	}
	else
	{
		CurrentSpeedRatio = 1.0f;
	}
}

void APackageBase::OnPackageHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	// 캐릭터가 밟거나 가볍게 부딪히는 것은 무시 (물리적 낙하/충돌만 취급)
	if (bIsBroken || OtherActor == this || Cast<ACharacterBase>(OtherActor))
	{
		return;
	}

	// 충돌 순간의 물리 속도(충격량)를 구함
	float ImpactSpeed = HitComponent->GetComponentVelocity().Size();

	// 500 이상의 속도로 부딪혔을 때만 대미지 처리 (살짝 내려놓는 것은 100~300 내외)
	if (ImpactSpeed > 500.0f)
	{
		// 충격 속도에 비례하여 대미지 계산 (예: 1000 속도면 10 대미지)
		float CalculatedDamage = (ImpactSpeed - 500.0f) * 0.02f;
		if (CalculatedDamage > 1.0f)
		{
			DamagePackage(CalculatedDamage);
			UE_LOG(LogTemp, Warning, TEXT("[%s] 강한 충돌 감지! 속도: %f -> 대미지: %f"), *GetName(), ImpactSpeed, CalculatedDamage);
		}
	}
}

void APackageBase::DamagePackage(float DamageAmount)
{
	if (bIsBroken || DamageAmount <= 0.0f)
	{
		return;
	}

	// [중요] DamagePackage는 서버에서만 실행해야 함.
	// 클라이언트 물리 Hit 이벤트로 실수로 호출되는 것을 방지.
	if (!HasAuthority())
	{
		return;
	}

	CurrentDurability -= DamageAmount;
	
	// 파손 처리
	if (CurrentDurability <= 0.0f)
	{
		CurrentDurability = 0.0f;
		bIsBroken = true;  // [Replicated] 이 값이 바뀌면 클라이언트의 OnRep_bIsBroken이 자동 호출됨
		
		// 찌그러진 상태로 남되, 택배의 가치를 0원으로 하락시킴
		BaseValue = 0;
		
		UE_LOG(LogTemp, Error, TEXT("[%s] 택배 파손! 가치가 0원이 되었습니다."), *GetName());
		
		// 서버 자신도 연출 실행 (OnRep은 서버에서는 호출 안 되므로 직접 호출)
		OnPackageBroken();
	}
}

void APackageBase::MulticastOnPackageBroken_Implementation()
{
	// 블루프린트 이벤트를 모든 클라이언트에서 실행 (메시 교체, 파티클 등)
	// OnRep_bIsBroken에서도 호출되므로 혹시 모를 이중 호출에 주의
	OnPackageBroken();
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
