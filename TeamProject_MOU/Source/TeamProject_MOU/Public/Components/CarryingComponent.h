#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CarryingComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCarriedStateChanged, AActor*, CarriedActor);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEAMPROJECT_MOU_API UCarryingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCarryingComponent();

protected:
	virtual void BeginPlay() override;

public:
	UFUNCTION(BlueprintCallable, Category = "Carrying")
	void GrabOrDrop();

	UFUNCTION(BlueprintCallable, Category = "Carrying")
	void Throw();

	UFUNCTION(Server, Reliable)
	void ServerGrabOrDrop();

	UFUNCTION(Server, Reliable)
	void ServerThrow();

	UFUNCTION(BlueprintCallable, Category = "Carrying")
	bool IsCarrying() const { return CarriedActor != nullptr; }

	UFUNCTION(BlueprintCallable, Category = "Carrying")
	AActor* GetCarriedActor() const { return CarriedActor; }

	UPROPERTY(BlueprintAssignable, Category = "Carrying")
	FOnCarriedStateChanged OnCarriedStateChanged;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrying")
	float DefaultThrowForce = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrying")
	FName CarrySocketName = TEXT("CarrySocket");

private:
	UPROPERTY(ReplicatedUsing = OnRep_CarriedActor)
	TObjectPtr<AActor> CarriedActor;

	UFUNCTION()
	void OnRep_CarriedActor(AActor* OldCarriedActor);
};
