// Fill out your copyright notice in the Description page of Project Settings.


#include "SurvivalGame/Player/SurvivalPlayerController.h"
#include "SurvivalCharacter.h"

ASurvivalPlayerController::ASurvivalPlayerController()
{
}

void ASurvivalPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	InputComponent->BindAxis("Turn", this, &ASurvivalPlayerController::Turn);
	InputComponent->BindAxis("LookUp", this, &ASurvivalPlayerController::LookUp);

	//InputComponent->BindAction("OpenInventory", IE_Pressed, this, &ASurvivalPlayerController::OpenInventory);

	//InputComponent->BindAction("Pause", IE_Pressed, this, &ASurvivalPlayerController::PauseGame);

	//InputComponent->BindAction("OpenMap", IE_Pressed, this, &ASurvivalPlayerController::OpenMap);
	//InputComponent->BindAction("OpenMap", IE_Released, this, &ASurvivalPlayerController::CloseMap);

	InputComponent->BindAction("Reload", IE_Pressed, this, &ASurvivalPlayerController::StartReload);
}

void ASurvivalPlayerController::ClientShowNotification_Implementation(const FText& Message)
{
	ShowNotification(Message);
}

void ASurvivalPlayerController::Respawn()
{
	UnPossess();
	ChangeState(NAME_Inactive);

	if (HasAuthority())
	{
		ServerRespawn();
	}
	else
	{
		ServerRestartPlayer();
	}
}

void ASurvivalPlayerController::ServerRespawn_Implementation()
{
	Respawn();
}

bool ASurvivalPlayerController::ServerRespawn_Validate()
{
	return true;
}

void ASurvivalPlayerController::ApplyRecoil(const FVector2D& RecoilAmount, const float RecoilSpeed, const float RecoilResetSpeed)//, TSubclassOf<class UCameraShake> Shake)
{
	if (IsLocalPlayerController())
	{
		if (PlayerCameraManager)
		{
			//PlayerCameraManager->PlayCameraShake(Shake);
			//PlayerCameraManager->ClientStartCameraShake(Shake);
		}

		RecoilBumpAmount += RecoilAmount;
		RecoilResetAmount += -RecoilAmount;

		CurrentRecoilSpeed = RecoilSpeed;
		CurrentRecoilResetSpeed = RecoilResetSpeed;

		LastRecoilTime = GetWorld()->GetTimeSeconds();
	}
}

void ASurvivalPlayerController::Turn(float Rate)
{
	//If the player has moved their camera to compensate for recoil we need this to cancel out the recoil reset effect
	if (!FMath::IsNearlyZero(RecoilResetAmount.X, 0.01f))
	{
		if (RecoilResetAmount.X > 0.f && Rate > 0.f)
		{
			RecoilResetAmount.X = FMath::Max(0.f, RecoilResetAmount.X - Rate);
		}
		else if (RecoilResetAmount.X < 0.f && Rate < 0.f)
		{
			RecoilResetAmount.X = FMath::Min(0.f, RecoilResetAmount.X - Rate);
		}
	}

	//Apply the recoil over several frames
	if (!FMath::IsNearlyZero(RecoilBumpAmount.X, 0.1f))
	{
		FVector2D LastCurrentRecoil = RecoilBumpAmount;
		RecoilBumpAmount.X = FMath::FInterpTo(RecoilBumpAmount.X, 0.f, GetWorld()->DeltaTimeSeconds, CurrentRecoilSpeed);

		AddYawInput(LastCurrentRecoil.X - RecoilBumpAmount.X);
	}

	//Slowly reset back to center after recoil is processed
	FVector2D LastRecoilResetAmount = RecoilResetAmount;
	RecoilResetAmount.X = FMath::FInterpTo(RecoilResetAmount.X, 0.f, GetWorld()->DeltaTimeSeconds, CurrentRecoilResetSpeed);
	AddYawInput(LastRecoilResetAmount.X - RecoilResetAmount.X);

	AddYawInput(Rate);
}

void ASurvivalPlayerController::LookUp(float Rate)
{
	if (!FMath::IsNearlyZero(RecoilResetAmount.Y, 0.01f))
	{
		if (RecoilResetAmount.Y > 0.f && Rate > 0.f)
		{
			RecoilResetAmount.Y = FMath::Max(0.f, RecoilResetAmount.Y - Rate);
		}
		else if (RecoilResetAmount.Y < 0.f && Rate < 0.f)
		{
			RecoilResetAmount.Y = FMath::Min(0.f, RecoilResetAmount.Y - Rate);
		}
	}

	//Apply the recoil over several frames
	if (!FMath::IsNearlyZero(RecoilBumpAmount.Y, 0.01f))
	{
		FVector2D LastCurrentRecoil = RecoilBumpAmount;
		RecoilBumpAmount.Y = FMath::FInterpTo(RecoilBumpAmount.Y, 0.f, GetWorld()->DeltaTimeSeconds, CurrentRecoilSpeed);

		AddPitchInput(LastCurrentRecoil.Y - RecoilBumpAmount.Y);
	}

	//Slowly reset back to center after recoil is processed
	FVector2D LastRecoilResetAmount = RecoilResetAmount;
	RecoilResetAmount.Y = FMath::FInterpTo(RecoilResetAmount.Y, 0.f, GetWorld()->DeltaTimeSeconds, CurrentRecoilResetSpeed);
	AddPitchInput(LastRecoilResetAmount.Y - RecoilResetAmount.Y);

	AddPitchInput(Rate);
}

void ASurvivalPlayerController::StartReload()
{
	if (ASurvivalCharacter* SurvivalCharacter = Cast<ASurvivalCharacter>(GetPawn()))
	{
		if (SurvivalCharacter->IsAlive())
		{
			SurvivalCharacter->StartReload();
		}
		else // R key should respawn the player if dead
		{
			Respawn();
		}
	}
}
