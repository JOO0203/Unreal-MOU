#include "AI/NPCPerceptionBlueprintLibrary.h"

#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISenseConfig.h"
#include "Perception/AISenseConfig_Sight.h"

bool UNPCPerceptionBlueprintLibrary::ApplySightConfig(
	UAIPerceptionComponent* PerceptionComponent,
	float SightRadius,
	float LoseSightRadius)
{
	if (!PerceptionComponent)
	{
		return false;
	}

	UAISenseConfig* SenseConfig = PerceptionComponent->GetSenseConfig(UAISense::GetSenseID<UAISense_Sight>());
	UAISenseConfig_Sight* SightConfig = Cast<UAISenseConfig_Sight>(SenseConfig);
	if (!SightConfig)
	{
		return false;
	}

	SightConfig->SightRadius = SightRadius;
	SightConfig->LoseSightRadius = FMath::Max(LoseSightRadius, SightRadius);

	PerceptionComponent->ConfigureSense(*SightConfig);
	PerceptionComponent->SetDominantSense(UAISense_Sight::StaticClass());
	PerceptionComponent->RequestStimuliListenerUpdate();

	return true;
}
