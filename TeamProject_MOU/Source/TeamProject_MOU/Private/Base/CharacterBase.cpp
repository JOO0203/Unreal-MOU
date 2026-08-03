// Fill out your copyright notice in the Description page of Project Settings.

#include "Base/CharacterBase.h"

#include "Base/BaseAttributeSet.h"
#include "Abilities/GameplayAbility.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

ACharacterBase::ACharacterBase()
{
	PrimaryActorTick.bCanEverTick = true;

	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(AscReplicationMode);

	GetCapsuleComponent()->InitCapsuleSize(35.0f, 90.0f);

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

	GetCharacterMovement()->JumpZVelocity = 500.0f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 500.0f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.0f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.0f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	BaseAttribute = CreateDefaultSubobject<UBaseAttributeSet>(TEXT("AttributeSet"));
	BaseAttributeSet.Add(BaseAttribute);
}

void ACharacterBase::BeginPlay()
{
	Super::BeginPlay();

	// 바인딩 호출
	BindAttributeChangeDelegates();
}

void ACharacterBase::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	if (AbilitySystemComponent)
	{
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

	AbilitySystemComponent
		->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetHealthAttribute())
		.AddUObject(this, &ACharacterBase::HandleHealthChanged);

	AbilitySystemComponent
		->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetMaxHealthAttribute())
		.AddUObject(this, &ACharacterBase::HandleMaxHealthChanged);
}

// Health Attribute가 변경되었을 때 ASC 델리게이트에 의해 호출
// 현재 Health와 MaxHealth를 Blueprint 이벤트로 넘겨 UI를 갱신
void ACharacterBase::HandleHealthChanged(const FOnAttributeChangeData& Data)
{
	if (!BaseAttribute)
	{
		return;
	}

	OnHealthUpdated(BaseAttribute->GetHealth(), BaseAttribute->GetMaxHealth());
}

// MaxHealth가 변경되었을 때 호출
// 최대 체력이 바뀌면 HP 비율도 달라질 수 있으므로 HP UI를 다시 갱신
void ACharacterBase::HandleMaxHealthChanged(const FOnAttributeChangeData& Data)
{
	if (!BaseAttribute)
	{
		return;
	}

	OnHealthUpdated(BaseAttribute->GetHealth(), BaseAttribute->GetMaxHealth());
}

// Stemina Attribute가 변경되었을 때 ASC 델리게이트에 의해 호출
void ACharacterBase::HandleSteminaChanged(const FOnAttributeChangeData& Data)
{
	if (!BaseAttribute)
	{
		return;
	}

	OnSteminaupdated(BaseAttribute->GetStemina(), BaseAttribute->GetMaxStemina());
}

// MaxStemina Attribute가 변경되었을 때 ASC 델리게이트에 의해 호출
void ACharacterBase::HandleMaxSteminaChanged(const FOnAttributeChangeData& Data)
{
	if (!BaseAttribute)
	{
		return;
	}

	OnSteminaupdated(BaseAttribute->GetStemina(), BaseAttribute->GetMaxStemina());
}

// Speed Attribute가 변경되었을 때 ASC 델리게이트에 의해 호출
void ACharacterBase::HandleMoveSpeedChanged(const FOnAttributeChangeData& Data)
{
	if (!BaseAttribute)
	{
		return;
	}

	GetCharacterMovement()->MaxWalkSpeed = Data.NewValue;
	OnSpeedUpdated(BaseAttribute->GetMoveSpeed(), BaseAttribute->GetMaxMoveSpeed());
}

// MaxSpeed Attribute가 변경되었을 때 ASC 델리게이트에 의해 호출
void ACharacterBase::HandleMaxMoveSpeedChanged(const FOnAttributeChangeData& Data)
{
	if (!BaseAttribute)
	{
		return;
	}

	GetCharacterMovement()->MaxWalkSpeed = Data.NewValue;
	OnSpeedUpdated(BaseAttribute->GetMoveSpeed(), BaseAttribute->GetMaxMoveSpeed());
}