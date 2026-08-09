#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GrabFollowComponent.generated.h"

class ACharacter;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEAMPROJECT_MOU_API UGrabFollowComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGrabFollowComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/*그랩 시작*/
	UFUNCTION(BlueprintCallable, Category = "Grab")
	void StartGrabFollow(ACharacter* NewCarrier, FName NewSocketName = TEXT("GrabSocket"), FVector NewRelativeOffset = FVector::ZeroVector, bool bInheritRotation = true);

	/*그랩 정지*/
	UFUNCTION(BlueprintCallable, Category = "Grab")
	void StopGrabFollow();

	/*땅 상태 체크*/
	UFUNCTION(BlueprintPure, Category = "Grab")
	bool IsGrabbed() const { return CarrierCharacter != nullptr; }

	/*그랩당할 캐릭터 확인*/
	UFUNCTION(BlueprintPure, Category = "Grab")
	ACharacter* GetCarrierCharacter() const { return CarrierCharacter; }

protected:
	/*그랩 상태 캐릭터*/
	UPROPERTY(ReplicatedUsing = OnRep_GrabState, VisibleAnywhere, BlueprintReadOnly, Category = "Grab")
	TObjectPtr<ACharacter> CarrierCharacter;

	/*그랩 소켓 이름*/
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Grab")
	FName GrabSocketName = TEXT("GrabSocket");

	/*그랩 상대 위치*/
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Grab")
	FVector RelativeOffset = FVector::ZeroVector;

	/*그랩 회전 여부*/
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Grab")
	bool bFollowRotation = true;

	/*그랩 상태 변경 시 호출되는 함수*/
	UFUNCTION()
	void OnRep_GrabState();

private:
	/*그랩 위치 동기화*/
	void SyncGrabTransform();
	/*그랩 상태 태그 적용*/
	void ApplyHeldTag(bool bAdd) const;
};
