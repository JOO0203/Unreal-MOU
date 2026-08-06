// Fill out your copyright notice in the Description page of Project Settings.

#include "Base/CharacterBase.h"

#include "Base/BaseAttributeSet.h"
#include "Components/StatusComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

ACharacterBase::ACharacterBase()
{
	PrimaryActorTick.bCanEverTick = true;

	// GAS 어빌리티 시스템 컴포넌트 생성 및 네트워크 리플리케이션 설정
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(AscReplicationMode);

	// 플레이어 및 방해 NPC 공통 상태 관리 컴포넌트 생성
	StatusComponent = CreateDefaultSubobject<UStatusComponent>(TEXT("StatusComponent"));

	// 캡슐 콜리전 기본 크기 설정
	GetCapsuleComponent()->InitCapsuleSize(35.0f, 90.0f);

	// 컨트롤러 회전 사용 안 함 (캐릭터 이동 방향으로 자동 회전)
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

	// 이동 물리 및 점프 관련 기본값 설정
	GetCharacterMovement()->JumpZVelocity = 500.0f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 500.0f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.0f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.0f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	// 기본 AttributeSet 생성 및 등록
	BaseAttribute = CreateDefaultSubobject<UBaseAttributeSet>(TEXT("AttributeSet"));
	BaseAttributeSet.Add(BaseAttribute);
}

void ACharacterBase::BeginPlay()
{
	Super::BeginPlay();

	// Attribute 변경 감지 델리게이트 바인딩
	BindAttributeChangeDelegates();
}

void ACharacterBase::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	if (AbilitySystemComponent)
	{
		// 서버 환경: GAS 어빌리티 정보 초기화 및 초기 스킬 부여
		AbilitySystemComponent->InitAbilityActorInfo(this, this);
		InitializeAbilityMulti(InitalAbilities, 1);
		BindAttributeChangeDelegates();
	}
}

void ACharacterBase::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	if (AbilitySystemComponent)
	{
		// 클라이언트 환경: GAS 어빌리티 정보 초기화
		AbilitySystemComponent->InitAbilityActorInfo(this, this);
		BindAttributeChangeDelegates();
	}
}

void ACharacterBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void ACharacterBase::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
}

UAbilitySystemComponent* ACharacterBase::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

bool ACharacterBase::CanMove() const
{
	// StatusComponent를 이용해 이동 가능 상태(기절/잡힘/넉백 없음) 확인
	if (StatusComponent)
	{
		return StatusComponent->CanMove();
	}

	return true;
}

bool ACharacterBase::CanAct() const
{
	// StatusComponent를 이용해 행동 가능 상태(기절/잡힘 없음) 확인
	if (StatusComponent)
	{
		return StatusComponent->CanAct();
	}

	return true;
}

FGameplayAbilitySpecHandle ACharacterBase::InitializeAbility(TSubclassOf<UGameplayAbility> AbilityToGet, int32 AbilityLevel)
{
	if (HasAuthority() && AbilitySystemComponent && AbilityToGet)
	{
		return AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(AbilityToGet, AbilityLevel));
	}

	return FGameplayAbilitySpecHandle();
}

void ACharacterBase::InitializeAbilityMulti(TArray<TSubclassOf<UGameplayAbility>> AbilityToAcquire, int32 AbilityLevel)
{
	if (!HasAuthority())
	{
		return;
	}

	for (const TSubclassOf<UGameplayAbility>& AbilityItem : AbilityToAcquire)
	{
		if (AbilityItem)
		{
			InitializeAbility(AbilityItem, AbilityLevel);
		}
	}
}

void ACharacterBase::BindAttributeChangeDelegates()
{
	if (AttributeDelegatesBound || !AbilitySystemComponent || !BaseAttribute)
	{
		return;
	}

	AttributeDelegatesBound = true;

	// 체력(Health) 변경 델리게이트 바인딩
	AbilitySystemComponent
		->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetHealthAttribute())
		.AddUObject(this, &ACharacterBase::HandleHealthChanged);

	AbilitySystemComponent
		->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetMaxHealthAttribute())
		.AddUObject(this, &ACharacterBase::HandleMaxHealthChanged);

	// 스태미나(Stemina) 변경 델리게이트 바인딩
	AbilitySystemComponent
		->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetSteminaAttribute())
		.AddUObject(this, &ACharacterBase::HandleSteminaChanged);

	AbilitySystemComponent
		->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetMaxSteminaAttribute())
		.AddUObject(this, &ACharacterBase::HandleMaxSteminaChanged);

	// 이동속도(MoveSpeed) 변경 델리게이트 바인딩
	AbilitySystemComponent
		->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetMoveSpeedAttribute())
		.AddUObject(this, &ACharacterBase::HandleMoveSpeedChanged);

	AbilitySystemComponent
		->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetMaxMoveSpeedAttribute())
		.AddUObject(this, &ACharacterBase::HandleMaxMoveSpeedChanged);
}

void ACharacterBase::HandleHealthChanged(const FOnAttributeChangeData& Data)
{
	if (!BaseAttribute)
	{
		return;
	}

	// 체력 변경 시 Blueprint 이벤트 호출 (UI 업데이트)
	OnHealthUpdated(BaseAttribute->GetHealth(), BaseAttribute->GetMaxHealth());
}

void ACharacterBase::HandleMaxHealthChanged(const FOnAttributeChangeData& Data)
{
	if (!BaseAttribute)
	{
		return;
	}

	OnHealthUpdated(BaseAttribute->GetHealth(), BaseAttribute->GetMaxHealth());
}

void ACharacterBase::HandleSteminaChanged(const FOnAttributeChangeData& Data)
{
	if (!BaseAttribute)
	{
		return;
	}

	// 스태미나 변경 시 Blueprint 이벤트 호출
	OnSteminaupdated(BaseAttribute->GetStemina(), BaseAttribute->GetMaxStemina());
}

void ACharacterBase::HandleMaxSteminaChanged(const FOnAttributeChangeData& Data)
{
	if (!BaseAttribute)
	{
		return;
	}

	OnSteminaupdated(BaseAttribute->GetStemina(), BaseAttribute->GetMaxStemina());
}

void ACharacterBase::HandleMoveSpeedChanged(const FOnAttributeChangeData& Data)
{
	if (!BaseAttribute)
	{
		return;
	}

	// 이동속도 속성 변경 시 CharacterMovement의 MaxWalkSpeed 동기화
	GetCharacterMovement()->MaxWalkSpeed = Data.NewValue;
	OnSpeedUpdated(BaseAttribute->GetMoveSpeed(), BaseAttribute->GetMaxMoveSpeed());
}

void ACharacterBase::HandleMaxMoveSpeedChanged(const FOnAttributeChangeData& Data)
{
	if (!BaseAttribute)
	{
		return;
	}

	GetCharacterMovement()->MaxWalkSpeed = Data.NewValue;
	OnSpeedUpdated(BaseAttribute->GetMoveSpeed(), BaseAttribute->GetMaxMoveSpeed());
}