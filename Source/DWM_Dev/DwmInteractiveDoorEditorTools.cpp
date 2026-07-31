#include "DwmInteractiveDoorEditorTools.h"

#if WITH_EDITOR

#include "DwmInteractiveDoor.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/StaticMeshActor.h"
#include "Misc/MessageDialog.h"
#include "ScopedTransaction.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "DwmInteractiveDoorEditorTools"

namespace
{
	const FName DwmDoorToolsMenuOwner(TEXT("DwmInteractiveDoorEditorTools"));

	TArray<AStaticMeshActor*> GetSelectedStaticMeshActors()
	{
		TArray<AStaticMeshActor*> Result;

		if (!GEditor)
		{
			return Result;
		}

		USelection* SelectedActors = GEditor->GetSelectedActors();
		for (FSelectionIterator It(*SelectedActors); It; ++It)
		{
			if (AStaticMeshActor* StaticMeshActor = Cast<AStaticMeshActor>(*It))
			{
				if (IsValid(StaticMeshActor) && StaticMeshActor->GetStaticMeshComponent()->GetStaticMesh())
				{
					Result.Add(StaticMeshActor);
				}
			}
		}

		return Result;
	}

	bool CanConvertSelectedStaticMeshActors()
	{
		return !GetSelectedStaticMeshActors().IsEmpty();
	}

	void ConvertSelectedStaticMeshActors()
	{
		const TArray<AStaticMeshActor*> SourceActors = GetSelectedStaticMeshActors();
		if (SourceActors.IsEmpty())
		{
			FMessageDialog::Open(EAppMsgType::Ok,
				LOCTEXT("NoStaticMeshActors", "Select one or more Static Mesh Actors with a mesh before converting them to interactive doors."));
			return;
		}

		const FScopedTransaction Transaction(LOCTEXT("ConvertStaticMeshDoors", "Convert Static Mesh Actors to Interactive Doors"));
		GEditor->SelectNone(false, true);

		int32 ConvertedCount = 0;
		for (AStaticMeshActor* SourceActor : SourceActors)
		{
			if (!IsValid(SourceActor))
			{
				continue;
			}

			UStaticMeshComponent* SourceMesh = SourceActor->GetStaticMeshComponent();
			UWorld* World = SourceActor->GetWorld();
			if (!IsValid(SourceMesh) || !SourceMesh->GetStaticMesh() || !IsValid(World))
			{
				continue;
			}

			SourceActor->Modify();

			FActorSpawnParameters SpawnParameters;
			SpawnParameters.OverrideLevel = SourceActor->GetLevel();
			SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			ADwmInteractiveDoor* NewDoor = World->SpawnActor<ADwmInteractiveDoor>(
				ADwmInteractiveDoor::StaticClass(), SourceActor->GetActorTransform(), SpawnParameters);
			if (!IsValid(NewDoor))
			{
				continue;
			}

			NewDoor->Modify();
			NewDoor->DoorMesh->SetStaticMesh(SourceMesh->GetStaticMesh());
			NewDoor->DoorMesh->SetCollisionProfileName(SourceMesh->GetCollisionProfileName());
			NewDoor->DoorMesh->SetCollisionEnabled(SourceMesh->GetCollisionEnabled());

			for (int32 MaterialIndex = 0; MaterialIndex < SourceMesh->GetNumMaterials(); ++MaterialIndex)
			{
				NewDoor->DoorMesh->SetMaterial(MaterialIndex, SourceMesh->GetMaterial(MaterialIndex));
			}

			const FString SourceLabel = SourceActor->GetActorLabel();
			NewDoor->SetActorLabel(FString::Printf(TEXT("Door_%s"), *SourceLabel));
			SourceActor->SetActorLabel(FString::Printf(TEXT("%s_Retired"), *SourceLabel));

			// Keep the original actor in the map as a reversible reference, but prevent it from
			// rendering or leaving the invisible collision which caused the Pizza Napoli doorway bug.
			SourceActor->SetActorHiddenInGame(true);
			SourceMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			SourceActor->SetIsTemporarilyHiddenInEditor(true);

			NewDoor->MarkPackageDirty();
			SourceActor->MarkPackageDirty();
			GEditor->SelectActor(NewDoor, true, false);
			++ConvertedCount;
		}

		GEditor->NoteSelectionChange();

		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
			LOCTEXT("ConvertedStaticMeshDoors",
				"Converted {0} static mesh actor(s) to interactive doors.\n\n"
				"The source actors were retained, hidden in game, and set to No Collision.\n"
				"Before PIE, verify the hinge side and set Open Yaw Degrees to 90 or -90."),
			FText::AsNumber(ConvertedCount)));
	}

	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(DwmDoorToolsMenuOwner);
		UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
		FToolMenuSection& Section = ToolsMenu->FindOrAddSection(TEXT("DwmTools"));
		Section.Label = LOCTEXT("DwmToolsSection", "Dream World Maker");

		Section.AddMenuEntry(
			TEXT("Dwm.ConvertSelectedStaticMeshesToInteractiveDoors"),
			LOCTEXT("ConvertSelectedStaticMeshesLabel", "Convert Selected Static Meshes to Interactive Doors"),
			LOCTEXT("ConvertSelectedStaticMeshesTooltip",
				"Creates an interactive door for each selected static mesh actor, preserving its mesh, materials, and transform. "
				"The original is retained but hidden in game and made non-blocking."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateStatic(&ConvertSelectedStaticMeshActors),
				FCanExecuteAction::CreateStatic(&CanConvertSelectedStaticMeshActors)));
	}
}

void FDwmInteractiveDoorEditorTools::Register()
{
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateStatic(&RegisterMenus));
}

void FDwmInteractiveDoorEditorTools::Unregister()
{
	UToolMenus::UnregisterOwner(DwmDoorToolsMenuOwner);
}

#undef LOCTEXT_NAMESPACE

#else

void FDwmInteractiveDoorEditorTools::Register()
{
}

void FDwmInteractiveDoorEditorTools::Unregister()
{
}

#endif
