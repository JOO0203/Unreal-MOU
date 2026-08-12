#include "Data/NPCData.h"

#include "GameplayTagsManager.h"


FGameplayTag UNPCData::PatrolTag() const
{
	return UGameplayTagsManager::Get().RequestGameplayTag(TEXT("State.NPC.Patrol"));
}

FGameplayTag UNPCData::TrackingTag() const
{
	return UGameplayTagsManager::Get().RequestGameplayTag(TEXT("State.NPC.Tracking"));
}

FGameplayTag UNPCData::StayTag() const
{
	return UGameplayTagsManager::Get().RequestGameplayTag(TEXT("State.NPC.Stay"));
}

UNPCData::UNPCData()
	: StartState(ENPCStartState::Patrol)
	, UsePatrol(true)
	, AfterActionPolicy(ENPCAfterActionPolicy::RepeatWhileTargetVisible)
	, LostTargetPolicy(ENPCLostTargetPolicy::ReturnToPatrol)
	, ActionRange(200.0f)
	, ActionInterval(1.0f)
	, SightRadius(1500.0f)
	, LoseSightRadius(2000.0f)
{
}

FGameplayTag UNPCData::GetStartStateTag() const
{
	switch (StartState)
	{
	case ENPCStartState::Stay:
		return StayTag();
	case ENPCStartState::Patrol:
	default:
		return PatrolTag();
	}
}

bool UNPCData::ShouldRepeatActionWhileTargetVisible() const
{
	return AfterActionPolicy == ENPCAfterActionPolicy::RepeatWhileTargetVisible;
}

FGameplayTag UNPCData::GetOneShotAfterActionStateTag() const
{
	switch (AfterActionPolicy)
	{
	case ENPCAfterActionPolicy::OneShotThenStay:
		return StayTag();
	case ENPCAfterActionPolicy::OneShotThenTracking:
		return TrackingTag();
	case ENPCAfterActionPolicy::OneShotThenPatrol:
		return PatrolTag();
	case ENPCAfterActionPolicy::RepeatWhileTargetVisible:
	default:
		return TrackingTag();
	}
}

bool UNPCData::ShouldReturnHomeOnLostTarget() const
{
	return LostTargetPolicy == ENPCLostTargetPolicy::ReturnHome;
}

FGameplayTag UNPCData::GetLostTargetStateTag() const
{
	switch (LostTargetPolicy)
	{
	case ENPCLostTargetPolicy::ReturnToStay:
		return StayTag();
	case ENPCLostTargetPolicy::ReturnHome:
		return UsePatrol ? PatrolTag() : StayTag();
	case ENPCLostTargetPolicy::ReturnToPatrol:
	default:
		return PatrolTag();
	}
}
