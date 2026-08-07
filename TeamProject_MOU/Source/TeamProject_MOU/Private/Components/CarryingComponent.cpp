#include "Components/CarryingComponent.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Base/ItemBase.h"
#include "Base/PackageBase.h"
#include "Net/UnrealNetwork.h"

UCarryingComponent::UCarryingComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UCarryingComponent::BeginPlay()
{
	Super::BeginPlay();
	
	SetIsReplicated(true);
}

void UCarryingComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UCarryingComponent, CarriedActor);
}

void UCarryingComponent::OnRep_CarriedActor(AActor* OldCarriedActor)
{
	// 서버로부터 CarriedActor 값이 복제되어 클라이언트에 도착했을 때 애니메이션 재생 등을 트리거
	OnCarriedStateChanged.Broadcast(CarriedActor);
}

void UCarryingComponent::GrabOrDrop()
{
	if (!GetOwner()->HasAuthority())
	{
		ServerGrabOrDrop();
		return;
	}

	if (IsCarrying())
	{
		// Drop 로직
		if (AItemBase* Item = Cast<AItemBase>(CarriedActor))
		{
			// 놓을 위치 (캐릭터 앞쪽 바닥 근처)
			FVector DropLoc = GetOwner()->GetActorLocation() + GetOwner()->GetActorForwardVector() * 80.0f;
			Item->Drop(DropLoc);
			
			// 택배인 경우 운반자 리스트에서 본인 제거 (과적/속도 원상복구)
			if (APackageBase* Package = Cast<APackageBase>(Item))
			{
				Package->RemoveCarrier(GetOwner());
			}
		}
		
		CarriedActor = nullptr;
		OnCarriedStateChanged.Broadcast(nullptr);
		UE_LOG(LogTemp, Log, TEXT("물건을 내려놓았습니다."));
	}
	else
	{
		// Grab (줍기) 로직: 전방 구형 트레이스(Sphere Trace)를 통해 아이템 탐색
		FVector Start = GetOwner()->GetActorLocation();
		// 캐릭터 중심에서 전방으로 150, 아래쪽으로 50만큼 향하게 하여 바닥의 물건도 잘 잡히도록 조정
		FVector End = Start + GetOwner()->GetActorForwardVector() * 150.0f - FVector(0, 0, 50.0f);
		
		TArray<AActor*> ActorsToIgnore;
		ActorsToIgnore.Add(GetOwner());
		
		FHitResult HitResult;
		// 반경을 40에서 60으로 늘려 판정을 후하게 만듦
		bool bHit = UKismetSystemLibrary::SphereTraceSingle(
			this, Start, End, 60.0f, UEngineTypes::ConvertToTraceType(ECC_Visibility),
			false, ActorsToIgnore, EDrawDebugTrace::ForDuration, HitResult, true);
			
		if (bHit && HitResult.GetActor())
		{
			// 트레이스에 맞은 대상이 아이템(택배 포함)인지 확인
			if (AItemBase* HitItem = Cast<AItemBase>(HitResult.GetActor()))
			{
				HitItem->PickUp(GetOwner());
				
				bool bShouldAttach = true;

				// 택배인 경우 운반자 리스트에 본인 추가
				if (APackageBase* Package = Cast<APackageBase>(HitItem))
				{
					// [Heavy] 이미 2명이면 들 수 없음 (AddCarrier 내부에서 차단)
					if (Package->PackageType == EPackageType::Heavy && Package->CurrentCarriers.Num() >= 2)
					{
						UE_LOG(LogTemp, Warning, TEXT("무거운 택배는 이미 2명이 들고 있어 들 수 없습니다."));
						return;
					}

					// [Heavy] Heavy 타입은 Tick에서 서버가 위치를 제어하므로 Attach 사용 안 함
					if (Package->PackageType == EPackageType::Heavy)
					{
						bShouldAttach = false;
					}
					// 일반 택배: 이미 누군가 들고 있으면 Attach 안 함
					else if (Package->CurrentCarriers.Num() > 0)
					{
						bShouldAttach = false;
					}

					Package->AddCarrier(GetOwner());
				}
				
				// 일반 아이템 또는 Heavy가 아닌 택배의 첫 번째 운반자만 Attach
				if (bShouldAttach)
				{
					if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
					{
						HitItem->AttachToComponent(Character->GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, CarrySocketName);
						// 일반/깨지기쉬움/상하기쉬움/위험 타입은 중앙 Attach (오프셋 없음)
					}
				}
				
				CarriedActor = HitItem;
				OnCarriedStateChanged.Broadcast(HitItem);
				UE_LOG(LogTemp, Log, TEXT("물건을 집어들었습니다: %s (Attach 여부: %d)"), *HitItem->GetName(), bShouldAttach);
			}
		}
	}
}

void UCarryingComponent::ServerGrabOrDrop_Implementation()
{
	GrabOrDrop();
}

void UCarryingComponent::Throw()
{
	if (!GetOwner()->HasAuthority())
	{
		ServerThrow();
		return;
	}

	if (!IsCarrying())
	{
		return;
	}

	if (AItemBase* Item = Cast<AItemBase>(CarriedActor))
	{
		// 택배라면 운반자에서 제거
		if (APackageBase* Package = Cast<APackageBase>(Item))
		{
			Package->RemoveCarrier(GetOwner());
		}
		
		// 캐릭터 전방과 위쪽(포물선) 방향으로 던지는 힘 계산
		FVector ThrowVel = GetOwner()->GetActorForwardVector() * DefaultThrowForce + FVector(0, 0, DefaultThrowForce * 0.4f);
		Item->Throw(ThrowVel);
	}
	
	CarriedActor = nullptr;
	OnCarriedStateChanged.Broadcast(nullptr);
	UE_LOG(LogTemp, Log, TEXT("물건을 던졌습니다."));
}

void UCarryingComponent::ServerThrow_Implementation()
{
	Throw();
}
