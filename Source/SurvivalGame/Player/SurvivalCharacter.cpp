// Fill out your copyright notice in the Description page of Project Settings.


#include "SurvivalCharacter.h"
#include "Net/UnrealNetwork.h"
#include "Camera/CameraComponent.h"
#include "SurvivalGame/Player/SurvivalPlayerController.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "SurvivalGame/Components/InventoryComponent.h"
#include "SurvivalGame/Components/InteractionComponent.h"
#include "SurvivalGame/Items/EquippableItem.h"
#include "SurvivalGame/Items/GearItem.h"
#include "SurvivalGame/Items/WeaponItem.h"
#include "SurvivalGame/Items/ThrowableItem.h"
#include "Materials/MaterialInstance.h"
#include "SurvivalGame/World/Pickup.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/DamageType.h"
#include "SurvivalGame/Weapons/MeleeDamage.h"
#include "SurvivalGame/Weapons/ThrowableWeapon.h"
#include "Kismet/GameplayStatics.h"
#include "SurvivalGame/SurvivalGame.h"
#include "SurvivalGame/Weapons/Weapon.h"
#include "Runtime/Engine/Classes/Engine/World.h"


#define LOCTEXT_NAMESPACE "SurvivalCharacter"

static FName NAME_AimDownSightsSocket("ADSSocket");

// Sets default values
ASurvivalCharacter::ASurvivalCharacter()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	SpringArmComponent = CreateDefaultSubobject<USpringArmComponent>("SpringArmComponent");
	SpringArmComponent->SetupAttachment(GetMesh(), FName("CameraSocket"));
	SpringArmComponent->TargetArmLength = 0.f;

	CameraComponent = CreateDefaultSubobject<UCameraComponent>("CameraComponent");
	CameraComponent->SetupAttachment(SpringArmComponent);
	CameraComponent->bUsePawnControlRotation = true;

	HelmetMesh = PlayerMeshes.Add(EEquippableSlot::EIS_Helmet, CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("HelmetMesh")));
	ChestMesh = PlayerMeshes.Add(EEquippableSlot::EIS_Chest, CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("ChestMesh")));
	LegsMesh = PlayerMeshes.Add(EEquippableSlot::EIS_Legs, CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("LegsMesh")));
	FeetMesh = PlayerMeshes.Add(EEquippableSlot::EIS_Feet, CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FeetMesh")));
	VestMesh = PlayerMeshes.Add(EEquippableSlot::EIS_Vest, CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("VestMesh")));
	HandsMesh = PlayerMeshes.Add(EEquippableSlot::EIS_Hands, CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("HandsMesh")));
	BackpackMesh = PlayerMeshes.Add(EEquippableSlot::EIS_Backpack, CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("BackpackMesh")));

	//Tell all the body meshes to use the head mesh for animation
	for (auto& Kvp : PlayerMeshes)
	{
		USkeletalMeshComponent* MeshComponent = Kvp.Value;
		MeshComponent->SetupAttachment(GetMesh());
		MeshComponent->SetLeaderPoseComponent(GetMesh());
	}

	PlayerMeshes.Add(EEquippableSlot::EIS_Head, GetMesh());
	GetMesh()->SetOwnerNoSee(true);

	//Give the player an inventory with 20 slots, and an 80kg capacity
	PlayerInventory = CreateDefaultSubobject<UInventoryComponent>("PlayerInventory");
	PlayerInventory->SetCapacity(20);
	PlayerInventory->SetWeightCapacity(80.f);

	LootPlayerInteraction = CreateDefaultSubobject<UInteractionComponent>("PlayerInteraction");
	LootPlayerInteraction->InteractableActionText = LOCTEXT("LootPlayerText", "Loot");
	LootPlayerInteraction->InteractableNameText = LOCTEXT("LootPlayerName", "Player");
	LootPlayerInteraction->SetupAttachment(GetRootComponent());
	LootPlayerInteraction->SetActive(false, true);
	LootPlayerInteraction->bAutoActivate = false;

	InteractionCheckFrequency = 0.f;
	InteractionCheckDistance = 1000.f;

	MaxHealth = 100.f;
	Health = MaxHealth;

	MeleeAttackDistance = 150.f;
	MeleeAttackDamage = 20.f;

	SprintSpeed = GetCharacterMovement()->MaxWalkSpeed * 1.3f;
	WalkSpeed = GetCharacterMovement()->MaxWalkSpeed;

	bIsAiming = false;

	GetMesh()->SetOwnerNoSee(true);

	GetCharacterMovement()->NavAgentProps.bCanCrouch = true;
}

// Called when the game starts or when spawned
void ASurvivalCharacter::BeginPlay()
{
	Super::BeginPlay();
	
	LootPlayerInteraction->OnInteract.AddDynamic(this, &ASurvivalCharacter::BeginLootingPlayer);

	//Try to display the players platform name on their loot card
	if (APlayerState* PS = GetPlayerState())
	{
		LootPlayerInteraction->SetInteractableNameText(FText::FromString(PS->GetPlayerName()));
	}

	//When the player spawns in they have no items equipped, so cache these items (That way, if a player unequips an item we can set the mesh back to the naked character)
	for (auto& PlayerMesh : PlayerMeshes)
	{
		NakedMeshes.Add(PlayerMesh.Key, PlayerMesh.Value->SkeletalMesh);
	}
}

void ASurvivalCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ASurvivalCharacter, bSprinting);
	DOREPLIFETIME(ASurvivalCharacter, LootSource);
	DOREPLIFETIME(ASurvivalCharacter, EquippedWeapon);
	DOREPLIFETIME(ASurvivalCharacter, Killer);

	DOREPLIFETIME_CONDITION(ASurvivalCharacter, Health, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ASurvivalCharacter, bIsAiming, COND_SkipOwner);
}

bool ASurvivalCharacter::IsInteracting() const
{
	return GetWorldTimerManager().IsTimerActive(TimerHandle_Interact);
}

float ASurvivalCharacter::GetRemainingInteractTime() const
{
	return GetWorldTimerManager().GetTimerRemaining(TimerHandle_Interact);
}

void ASurvivalCharacter::UseItem(UItem* Item)
{
	if (GetLocalRole() < ROLE_Authority && Item)
	{
		ServerUseItem(Item);
	}

	if (HasAuthority())
	{
		if (PlayerInventory && !PlayerInventory->FindItem(Item))
		{
			return;
		}
	}

	if (Item)
	{
		Item->OnUse(this);
		Item->Use(this);
	}
}

void ASurvivalCharacter::DropItem(UItem* Item, const int32 Quantity)
{
	if (PlayerInventory && Item && PlayerInventory->FindItem(Item))
	{
		if (GetLocalRole() < ROLE_Authority)
		{
			ServerDropItem(Item, Quantity);
			return;
		}

		if (HasAuthority())
		{
			const int32 ItemQuantity = Item->GetQuantity();
			const int32 DroppedQuantity = PlayerInventory->ConsumeItem(Item, Quantity);

			FActorSpawnParameters SpawnParams;
			SpawnParams.Owner = this;
			SpawnParams.bNoFail = true;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

			FVector SpawnLocation = GetActorLocation();
			SpawnLocation.Z -= GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

			FTransform SpawnTransform(GetActorRotation(), SpawnLocation);

			ensure(PickupClass);

			if (APickup* Pickup = GetWorld()->SpawnActor<APickup>(PickupClass, SpawnTransform, SpawnParams))
			{
				Pickup->InitializePickup(Item->GetClass(), DroppedQuantity);
			}
		}
	}
}

bool ASurvivalCharacter::EquipItem(class UEquippableItem* Item)
{
	EquippedItems.Add(Item->Slot, Item);
	OnEquippedItemsChanged.Broadcast(Item->Slot, Item);
	return true;
}

bool ASurvivalCharacter::UnEquipItem(class UEquippableItem* Item)
{
	if (Item)
	{
		if (EquippedItems.Contains(Item->Slot))
		{
			if (Item == *EquippedItems.Find(Item->Slot))
			{
				EquippedItems.Remove(Item->Slot);
				OnEquippedItemsChanged.Broadcast(Item->Slot, nullptr);
				return true;
			}
		}
	}
	return false;
}

void ASurvivalCharacter::EquipGear(class UGearItem* Gear)
{
	if (USkeletalMeshComponent* GearMesh = GetSlotSkeletalMeshComponent(Gear->Slot))
	{
		GearMesh->SetSkeletalMesh(Gear->Mesh);
		GearMesh->SetMaterial(GearMesh->GetMaterials().Num() - 1, Gear->MaterialInstance);
	}
}

void ASurvivalCharacter::UnEquipGear(const EEquippableSlot Slot)
{
	if (USkeletalMeshComponent* EquippableMesh = GetSlotSkeletalMeshComponent(Slot))
	{
		if (USkeletalMesh* BodyMesh = *NakedMeshes.Find(Slot))
		{
			EquippableMesh->SetSkeletalMesh(BodyMesh);

			//Put the materials back on the body mesh (Since gear may have applied a different material)
			for (int32 i = 0; i < BodyMesh->GetMaterials().Num(); ++i)
			{
				if (BodyMesh->GetMaterials().IsValidIndex(i))
				{
					EquippableMesh->SetMaterial(i, BodyMesh->GetMaterials()[i].MaterialInterface);
				}
			}
		}
		else
		{
			//For some gear like backpacks, there is no naked mesh
			EquippableMesh->SetSkeletalMesh(nullptr);
		}
	}
}

void ASurvivalCharacter::EquipWeapon(UWeaponItem* WeaponItem)
{
	if (WeaponItem && WeaponItem->WeaponClass && HasAuthority())
	{
		if (EquippedWeapon)
		{
			UnEquipWeapon();
		}

		//Spawn the weapon in
		FActorSpawnParameters SpawnParams;
		SpawnParams.bNoFail = true;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		SpawnParams.Owner = SpawnParams.Instigator = this;

		if (AWeapon* Weapon = GetWorld()->SpawnActor<AWeapon>(WeaponItem->WeaponClass, SpawnParams))
		{
			Weapon->Item = WeaponItem;

			EquippedWeapon = Weapon;
			OnRep_EquippedWeapon();

			Weapon->OnEquip();
		}
	}
}

void ASurvivalCharacter::UnEquipWeapon()
{
	if (HasAuthority() && EquippedWeapon)
	{
		EquippedWeapon->OnUnEquip();
		EquippedWeapon->Destroy();
		EquippedWeapon = nullptr;
		OnRep_EquippedWeapon();
	}
}

USkeletalMeshComponent* ASurvivalCharacter::GetSlotSkeletalMeshComponent(const EEquippableSlot Slot)
{
	if (PlayerMeshes.Contains(Slot))
	{
		return *PlayerMeshes.Find(Slot);
	}
	return nullptr;
}

UThrowableItem* ASurvivalCharacter::GetThrowable() const
{
	UThrowableItem* EquippedThrowable = nullptr;

	if (EquippedItems.Contains(EEquippableSlot::EIS_Throwable))
	{
		EquippedThrowable = Cast<UThrowableItem>(*EquippedItems.Find(EEquippableSlot::EIS_Throwable));
	}

	return EquippedThrowable;
}

void ASurvivalCharacter::UseThrowable()
{
	if (CanUseThrowable())
	{
		if (UThrowableItem* Throwable = GetThrowable())
		{
			if (HasAuthority())
			{
				SpawnThrowable();

				if (PlayerInventory)
				{
					PlayerInventory->ConsumeItem(Throwable, 1);
				}
			}
			else
			{
				if (Throwable->GetQuantity() <= 1)
				{
					EquippedItems.Remove(EEquippableSlot::EIS_Throwable);
					OnEquippedItemsChanged.Broadcast(EEquippableSlot::EIS_Throwable, nullptr);
				}

				//Locally play grenade throw instantly - by the time server spawns the grenade in the throw animation should roughly sync up with the spawning of the grenade
				PlayAnimMontage(Throwable->ThrowableTossAnimation);
				ServerUseThrowable();
			}
		}
	}
}

void ASurvivalCharacter::SpawnThrowable()
{
	if (HasAuthority())
	{
		if (UThrowableItem* CurrentThrowable = GetThrowable())
		{
			if (CurrentThrowable->ThrowableClass)
			{
				FActorSpawnParameters SpawnParams;
				SpawnParams.Owner = SpawnParams.Instigator = this;
				SpawnParams.bNoFail = true;
				SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

				FVector EyesLoc;
				FRotator EyesRot;

				GetController()->GetPlayerViewPoint(EyesLoc, EyesRot);

				//Spawn throwable slightly in front of our face so it doesnt collide with our player
				EyesLoc = (EyesRot.Vector() * 20.f) + EyesLoc;

				if (AThrowableWeapon* ThrowableWeapon = GetWorld()->SpawnActor<AThrowableWeapon>(CurrentThrowable->ThrowableClass, FTransform(EyesRot, EyesLoc), SpawnParams))
				{
					MulticastPlayThrowableTossFX(CurrentThrowable->ThrowableTossAnimation);
				}
			}
		}
	}
}

bool ASurvivalCharacter::CanUseThrowable() const
{
	return GetThrowable() != nullptr && GetThrowable()->ThrowableClass != nullptr;
}

void ASurvivalCharacter::MulticastPlayThrowableTossFX_Implementation(UAnimMontage* MontageToPlay)
{
	//Local player already instantly played grenade throw anim
	if (GetNetMode() != NM_DedicatedServer && !IsLocallyControlled())
	{
		PlayAnimMontage(MontageToPlay);
	}
}

void ASurvivalCharacter::ServerUseThrowable_Implementation()
{
	UseThrowable();
}

void ASurvivalCharacter::ServerDropItem_Implementation(class UItem* Item, const int32 Quantity)
{
	DropItem(Item, Quantity);
}

bool ASurvivalCharacter::ServerDropItem_Validate(class UItem* Item, const int32 Quantity)
{
	return true;
}

void ASurvivalCharacter::ServerUseItem_Implementation(class UItem* Item)
{
	UseItem(Item);
}

bool ASurvivalCharacter::ServerUseItem_Validate(class UItem* Item)
{
	return true;
}

float ASurvivalCharacter::ModifyHealth(const float Delta)
{
	const float OldHealth = Health;

	Health = FMath::Clamp<float>(Health + Delta, 0.f, MaxHealth);

	return Health - OldHealth;
}

void ASurvivalCharacter::OnRep_Health(float OldHealth)
{
	OnHealthModified(Health - OldHealth);
}

void ASurvivalCharacter::OnRep_EquippedWeapon()
{
	if (EquippedWeapon)
	{
		EquippedWeapon->OnEquip();
	}
}

void ASurvivalCharacter::StartFire()
{
	if (EquippedWeapon)
	{
		EquippedWeapon->StartFire();
	}
	else
	{
		BeginMeleeAttack();
	}
}

void ASurvivalCharacter::StopFire()
{
	if (EquippedWeapon)
	{
		EquippedWeapon->StopFire();
	}
}

void ASurvivalCharacter::BeginMeleeAttack()
{
	if (GetWorld()->TimeSince(LastMeleeAttackTime) > MeleeAttackMontage->GetPlayLength())
	{
		FHitResult Hit;
		FCollisionShape Shape = FCollisionShape::MakeSphere(15.f);


		FVector StartTrace = CameraComponent->GetComponentLocation();
		FVector EndTrace = (CameraComponent->GetComponentRotation().Vector() * MeleeAttackDistance) + StartTrace;

		FCollisionQueryParams QueryParams = FCollisionQueryParams("MeleeSweep", false, this);

		PlayAnimMontage(MeleeAttackMontage);

		if (GetWorld()->SweepSingleByChannel(Hit, StartTrace, EndTrace, FQuat(), COLLISION_WEAPON, Shape, QueryParams))
		{
			UE_LOG(LogTemp, Warning, TEXT("hit something"))

			if (ASurvivalCharacter* HitPlayer = Cast<ASurvivalCharacter>(Hit.GetActor()))
			{
				if (ASurvivalPlayerController* PC = Cast<ASurvivalPlayerController>(GetController()))
				{
					PC->OnHitPlayer();
				}
			}
		}

		ServerProcessMeleeHit(Hit);

		LastMeleeAttackTime = GetWorld()->GetTimeSeconds();
	}
}

void ASurvivalCharacter::MulticastPlayMeleeFX_Implementation()
{
	if (!IsLocallyControlled())
	{
		PlayAnimMontage(MeleeAttackMontage);
	}
}

void ASurvivalCharacter::ServerProcessMeleeHit_Implementation(const FHitResult& MeleeHit)
{
	if (GetWorld()->TimeSince(LastMeleeAttackTime) > MeleeAttackMontage->GetPlayLength() && (GetActorLocation() - MeleeHit.ImpactPoint).Size() <= MeleeAttackDistance)
	{	
		MulticastPlayMeleeFX();

		UGameplayStatics::ApplyPointDamage(MeleeHit.GetActor(), MeleeAttackDamage, (MeleeHit.TraceStart - MeleeHit.TraceEnd).GetSafeNormal(), MeleeHit, GetController(), this, UMeleeDamage::StaticClass());
	}
	LastMeleeAttackTime = GetWorld()->GetTimeSeconds();
}

void ASurvivalCharacter::Suicide(FDamageEvent const& DamageEvent, const AActor* DamageCauser)
{
	Killer = this;
	OnRep_Killer();
}

void ASurvivalCharacter::KilledByPlayer(FDamageEvent const& DamageEvent, ASurvivalCharacter* Character, const AActor* DamageCauser)
{
	Killer = Character;
	OnRep_Killer();
}

void ASurvivalCharacter::OnRep_Killer()
{
	SetLifeSpan(20.0f);

	GetMesh()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GetMesh()->SetSimulatePhysics(true);
	GetMesh()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	GetMesh()->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	GetMesh()->SetOwnerNoSee(false);
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
	//bReplicateMovement = false;

	LootPlayerInteraction->Activate();

	TArray<UEquippableItem*> EquippedInvItems;
	EquippedItems.GenerateValueArray(EquippedInvItems);

	for (auto& Equippable : EquippedInvItems)
	{
		Equippable->SetEquipped(false);
	}

	if (IsLocallyControlled())
	{
		SpringArmComponent->TargetArmLength = 500.f;
		SpringArmComponent->AttachToComponent(GetCapsuleComponent(), FAttachmentTransformRules::SnapToTargetIncludingScale);
		bUseControllerRotationPitch = true;

		if (ASurvivalPlayerController* PC = Cast<ASurvivalPlayerController>(GetController()))
		{
			PC->ShowDeathScreen(Killer);
		}
	}
}

bool ASurvivalCharacter::CanSprint() const
{
	return !IsAiming();
}

void ASurvivalCharacter::StartSprinting()
{
	SetSprinting(true);
}

void ASurvivalCharacter::StopSprinting()
{
	SetSprinting(false);
}

void ASurvivalCharacter::SetSprinting(const bool bNewSprinting)
{
	if ((bNewSprinting && !CanSprint()) || bNewSprinting == bSprinting)
	{
		return;
	}

	if (!HasAuthority())
	{
		ServerSetSprinting(bNewSprinting);
	}

	bSprinting = bNewSprinting;

	GetCharacterMovement()->MaxWalkSpeed = bSprinting ? SprintSpeed : WalkSpeed;
}

void ASurvivalCharacter::ServerSetSprinting_Implementation(const bool bNewSprinting)
{
	SetSprinting(bNewSprinting);
}

bool ASurvivalCharacter::ServerSetSprinting_Validate(const bool bNewSprinting)
{
	return true;
}

void ASurvivalCharacter::StartCrouching()
{
	Crouch();
}

void ASurvivalCharacter::StopCrouching()
{
	UnCrouch();
}

void ASurvivalCharacter::MoveForward(float Val)
{
	if (Val != 0.f)
	{
		AddMovementInput(GetActorForwardVector(), Val);
	}
}

void ASurvivalCharacter::MoveRight(float Val)
{
	if (Val != 0.f)
	{
		AddMovementInput(GetActorRightVector(), Val);
	}
}

void ASurvivalCharacter::LookUp(float Val)
{
	if (Val != 0.f)
	{
		AddControllerPitchInput(Val);
	}
}

void ASurvivalCharacter::Turn(float Val)
{
	if (Val != 0.f)
	{
		AddControllerYawInput(Val);
	}
}

bool ASurvivalCharacter::CanAim() const
{
	return EquippedWeapon != nullptr;
}

void ASurvivalCharacter::StartAiming()
{
	if (CanAim())
	{
		SetAiming(true);
	}
}

void ASurvivalCharacter::StopAiming()
{
	SetAiming(false);
}

void ASurvivalCharacter::SetAiming(const bool bNewAiming)
{
	if ((bNewAiming && !CanAim()) || bNewAiming == bIsAiming)
	{
		return;
	}

	if (HasAuthority())
	{
		ServerSetAiming(bNewAiming);
	}

	bIsAiming = bNewAiming;
}

void ASurvivalCharacter::ServerSetAiming_Implementation(const bool bNewAiming)
{
	SetAiming(bNewAiming);
}

// Called every frame
void ASurvivalCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	//const bool bIsInteractingOnServer = (HasAuthority() && IsInteracting());

	if (GetWorld()->TimeSince(InteractionData.LastInteractionCheckTime) > InteractionCheckFrequency)
	{
		PerformInteractionCheck();
	}

	if (IsLocallyControlled())
	{
		const float DesiredFOV = IsAiming() ? 70.f : 100.f;
		CameraComponent->SetFieldOfView(FMath::FInterpTo(CameraComponent->FieldOfView, DesiredFOV, DeltaTime, 10.f));

		if (EquippedWeapon)
		{
			const FVector ADSLocation = EquippedWeapon->GetWeaponMesh()->GetSocketLocation(NAME_AimDownSightsSocket);
			const FVector DefaultCameraLocation = GetMesh()->GetSocketLocation(FName("CameraSocket"));

			const FVector CameraLoc = bIsAiming ? ADSLocation : DefaultCameraLocation;

			const float InterpSpeed = FVector::Dist(ADSLocation, DefaultCameraLocation) / EquippedWeapon->ADSTime;
			CameraComponent->SetWorldLocation(FMath::VInterpTo(CameraComponent->GetComponentLocation(), CameraLoc, DeltaTime, InterpSpeed));
		}
	}
}

void ASurvivalCharacter::Restart()
{
	Super::Restart();

	if (ASurvivalPlayerController* PC = Cast<ASurvivalPlayerController>(GetController()))
	{
		PC->ShowIngameUI();
	}
}

float ASurvivalCharacter::TakeDamage(float Damage, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);

	const float DamageDealt = ModifyHealth(-Damage);

	if (Health <= 0.f)
	{
		if (ASurvivalCharacter* KillerCharacter = Cast<ASurvivalCharacter>(DamageCauser->GetOwner()))
		{
			KilledByPlayer(DamageEvent, KillerCharacter, DamageCauser);
		}
		else
		{
			Suicide(DamageEvent, DamageCauser);
		}
	}

	return DamageDealt;
}

void ASurvivalCharacter::SetLootSource(UInventoryComponent* NewLootSource)
{
	/**If the thing we're looting gets destroyed, we need to tell the client to remove their Loot screen*/
	if (NewLootSource && NewLootSource->GetOwner())
	{
		NewLootSource->GetOwner()->OnDestroyed.AddUniqueDynamic(this, &ASurvivalCharacter::OnLootSourceOwnerDestroyed);
	}

	if (HasAuthority())
	{
		if (NewLootSource)
		{
			//Looting a player keeps their body alive for an extra 2 minutes to provide enough time to loot their items
			if (ASurvivalCharacter* Character = Cast<ASurvivalCharacter>(NewLootSource->GetOwner()))
			{
				Character->SetLifeSpan(120.f);
			}
		}

		LootSource = NewLootSource;
		OnRep_LootSource();
	}
	else
	{
		ServerSetLootSource(NewLootSource);
	}
}

bool ASurvivalCharacter::IsLooting() const
{
	return LootSource != nullptr;
}

void ASurvivalCharacter::BeginLootingPlayer(ASurvivalCharacter* Character)
{
	if (Character)
	{
		Character->SetLootSource(PlayerInventory);
	}
}

void ASurvivalCharacter::OnLootSourceOwnerDestroyed(AActor* DestroyedActor)
{
	//Remove loot source 
	if (HasAuthority() && LootSource && DestroyedActor == LootSource->GetOwner())
	{
		ServerSetLootSource(nullptr);
	}
}

void ASurvivalCharacter::OnRep_LootSource()
{
	//Bring up or remove the looting menu 
	if (ASurvivalPlayerController* PC = Cast<ASurvivalPlayerController>(GetController()))
	{
		if (PC->IsLocalController())
		{
			if (LootSource)
			{
				PC->ShowLootMenu(LootSource);
			}
			else
			{
				PC->HideLootMenu();
			}
		}
	}
}

void ASurvivalCharacter::ServerSetLootSource_Implementation(class UInventoryComponent* NewLootSource)
{
	SetLootSource(NewLootSource);
}

bool ASurvivalCharacter::ServerSetLootSource_Validate(class UInventoryComponent* NewLootSource)
{
	return true;
}

void ASurvivalCharacter::LootItem(UItem* ItemToGive)
{
	if (HasAuthority())
	{
		if (PlayerInventory && LootSource && ItemToGive && LootSource->HasItem(ItemToGive->GetClass(), ItemToGive->GetQuantity()))
		{
			const FItemAddResult AddResult = PlayerInventory->TryAddItem(ItemToGive);

			if (AddResult.AmountGiven > 0)
			{
				LootSource->ConsumeItem(ItemToGive, AddResult.AmountGiven);
			}
			else
			{
				//Tell player why they couldn't loot the item
				if (ASurvivalPlayerController* PC = Cast<ASurvivalPlayerController>(GetController()))
				{
					PC->ClientShowNotification(AddResult.ErrorText);
				}
			}
		}
	}
	else
	{
		ServerLootItem(ItemToGive);
	}
}

void ASurvivalCharacter::ServerLootItem_Implementation(class UItem* ItemToLoot)
{
	LootItem(ItemToLoot);
}

bool ASurvivalCharacter::ServerLootItem_Validate(class UItem* ItemToLoot)
{
	return true;
}

void ASurvivalCharacter::PerformInteractionCheck()
{

	if (GetController() == nullptr)
	{
		return;
	}

	InteractionData.LastInteractionCheckTime = GetWorld()->GetTimeSeconds();

	FVector EyesLoc;
	FRotator EyesRot;

	GetController()->GetPlayerViewPoint(EyesLoc, EyesRot);

	FVector TraceStart = EyesLoc;
	FVector TraceEnd = (EyesRot.Vector() * InteractionCheckDistance) + TraceStart;
	FHitResult TraceHit;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	if (GetWorld()->LineTraceSingleByChannel(TraceHit, TraceStart, TraceEnd, ECC_Visibility, QueryParams))
	{
		//Check if we hit an interactable object
		if (TraceHit.GetActor())
		{
			if (UInteractionComponent* InteractionComponent = Cast<UInteractionComponent>(TraceHit.GetActor()->GetComponentByClass(UInteractionComponent::StaticClass())))
			{
				float Distance = (TraceStart - TraceHit.ImpactPoint).Size();
				if (InteractionComponent != GetInteractable() && Distance <= InteractionComponent->InteractionDistance)
				{
					FoundNewInteractable(InteractionComponent);
				}
				else if (Distance > InteractionComponent->InteractionDistance && GetInteractable())
				{
					CouldntFindInteractable();
				}

				return;
			}
		}
	}

	CouldntFindInteractable();

}

void ASurvivalCharacter::CouldntFindInteractable()
{
	//We've lost focus on an interactable. Clear the timer.
	if (GetWorldTimerManager().IsTimerActive(TimerHandle_Interact))
	{
		GetWorldTimerManager().ClearTimer(TimerHandle_Interact);
	}

	//Tell the interactable we've stopped focusing on it, and clear the current interactable
	if (UInteractionComponent* Interactable = GetInteractable())
	{
		Interactable->EndFocus(this);

		if (InteractionData.bInteractHeld)
		{
			EndInteract();
		}
	}

	InteractionData.ViewedInteractionComponent = nullptr;
}

void ASurvivalCharacter::FoundNewInteractable(UInteractionComponent* Interactable)
{
	EndInteract();

	if (UInteractionComponent* OldInteractable = GetInteractable())
	{
		OldInteractable->EndFocus(this);
	}

	InteractionData.ViewedInteractionComponent = Interactable;
	Interactable->BeginFocus(this);

}

void ASurvivalCharacter::BeginInteract()
{
	if (!HasAuthority())
	{
		ServerBeginInteract();
	}

	/**As an optimization, the server only checks that we're looking at an item once we begin interacting with it.
	This saves the server doing a check every tick for an interactable Item. The exception is a non-instant interact.
	In this case, the server will check every tick for the duration of the interact*/
	if (HasAuthority())
	{
		PerformInteractionCheck();
	}

	InteractionData.bInteractHeld = true;

	if (UInteractionComponent* Interactable = GetInteractable())
	{
		Interactable->BeginInteract(this);

		if (FMath::IsNearlyZero(Interactable->InteractionTime))
		{
			Interact();
		}
		else
		{
			GetWorldTimerManager().SetTimer(TimerHandle_Interact, this, &ASurvivalCharacter::Interact, Interactable->InteractionTime, false);
		}
	}
}

void ASurvivalCharacter::EndInteract()
{
	if (!HasAuthority())
	{
		ServerEndInteract();
	}

	InteractionData.bInteractHeld = false;

	GetWorldTimerManager().ClearTimer(TimerHandle_Interact);

	if (UInteractionComponent* Interactable = GetInteractable())
	{
		Interactable->EndInteract(this);
	}
}

void ASurvivalCharacter::Interact()
{
	GetWorldTimerManager().ClearTimer(TimerHandle_Interact);

	if (UInteractionComponent* Interactable = GetInteractable())
	{
		Interactable->Interact(this);
	}
}

void ASurvivalCharacter::ServerBeginInteract_Implementation()
{
	BeginInteract();
}

bool ASurvivalCharacter::ServerBeginInteract_Validate()
{
	return true;
}

void ASurvivalCharacter::ServerEndInteract_Implementation()
{
	EndInteract();
}

bool ASurvivalCharacter::ServerEndInteract_Validate()
{
	return true;
}

void ASurvivalCharacter::StartReload()
{
	if (EquippedWeapon)
	{
		EquippedWeapon->StartReload();
	}
}

// Called to bind functionality to input
void ASurvivalCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	PlayerInputComponent->BindAction("Fire", IE_Pressed, this, &ASurvivalCharacter::StartFire);
	PlayerInputComponent->BindAction("Fire", IE_Released, this, &ASurvivalCharacter::StopFire);
	PlayerInputComponent->BindAction("Aim", IE_Pressed, this, &ASurvivalCharacter::StartAiming);
	PlayerInputComponent->BindAction("Aim", IE_Released, this, &ASurvivalCharacter::StopAiming);
	PlayerInputComponent->BindAction("Throw", IE_Pressed, this, &ASurvivalCharacter::UseThrowable);

	PlayerInputComponent->BindAction("Jump", IE_Pressed, this, &ACharacter::Jump);
	PlayerInputComponent->BindAction("Jump", IE_Released, this, &ACharacter::StopJumping);

	PlayerInputComponent->BindAction("Interact", IE_Pressed, this, &ASurvivalCharacter::BeginInteract);
	PlayerInputComponent->BindAction("Interact", IE_Released, this, &ASurvivalCharacter::EndInteract);

	PlayerInputComponent->BindAction("Crouch", IE_Pressed, this, &ASurvivalCharacter::StartCrouching);
	PlayerInputComponent->BindAction("Crouch", IE_Released, this, &ASurvivalCharacter::StopCrouching);
	PlayerInputComponent->BindAction("Sprint", IE_Pressed, this, &ASurvivalCharacter::StartSprinting);
	PlayerInputComponent->BindAction("Sprint", IE_Released, this, &ASurvivalCharacter::StopSprinting);

	PlayerInputComponent->BindAxis("MoveForward", this, &ASurvivalCharacter::MoveForward);
	PlayerInputComponent->BindAxis("MoveRight", this, &ASurvivalCharacter::MoveRight);
}

#undef LOCTEXT_NAMESPACE