// Fill out your copyright notice in the Description page of Project Settings.


#include "SurvivalGame/Items/FoodItem.h"
#include "SurvivalGame/Player/SurvivalCharacter.h"
#include "SurvivalGame/Player/SurvivalPlayerController.h"
#include "SurvivalGame/Components/InventoryComponent.h"

#define LOCTEXT_NAMESPACE "FoodItem"

UFoodItem::UFoodItem()
{
	HealAmount = 20.f;
	UseActionText = LOCTEXT("ItemUseActionText", "Consume");
}

void UFoodItem::Use(class ASurvivalCharacter* Character)
{
	if (Character)
	{
		const float ActualHealedAmount = Character->ModifyHealth(HealAmount);
		const bool bUsedFood = !FMath::IsNearlyZero(ActualHealedAmount);

		if (!Character->HasAuthority())
		{
			if (ASurvivalPlayerController* PC = Cast<ASurvivalPlayerController>(Character->GetController()))
			{
				if (bUsedFood)
				{
					PC->ClientShowNotification(FText::Format(LOCTEXT("AteFoodText", "Ate {FoodName}, healed {HealAmount} health."), DisplayName, ActualHealedAmount));
				}
				else
				{
					PC->ClientShowNotification(FText::Format(LOCTEXT("FullHealthText", "No need to eat {FoodName}, health is already full."), DisplayName, HealAmount));
				}
			}
		}

		if (bUsedFood)
		{
			if (UInventoryComponent* Inventory = Character->PlayerInventory)
			{
				Inventory->ConsumeItem(this, 1);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE