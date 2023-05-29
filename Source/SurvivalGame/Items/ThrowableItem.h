// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "SurvivalGame/Items/EquippableItem.h"
#include "ThrowableItem.generated.h"

/**
 * 
 */
UCLASS(Blueprintable)
class SURVIVALGAME_API UThrowableItem : public UEquippableItem
{
	GENERATED_BODY()
	
public:

	UThrowableItem();

	//The montage to play when we toss a throwable
	UPROPERTY(EditDefaultsOnly, Category = "Weapons")
	class UAnimMontage* ThrowableTossAnimation;

	//The actor to spawn in when we throw the item.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TSubclassOf<class AThrowableWeapon> ThrowableClass;

};
