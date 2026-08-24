#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NPCPerceptionBlueprintLibrary.generated.h"

class UAIPerceptionComponent;

UCLASS()
class TEAMPROJECT_MOU_API UNPCPerceptionBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** AI Perception의 Sight 설정값을 런타임에 갱신한다. */
	UFUNCTION(BlueprintCallable, Category = "NPC|Perception")
	static bool ApplySightConfig(
		UAIPerceptionComponent* PerceptionComponent,
		float SightRadius,
		float LoseSightRadius);
};
