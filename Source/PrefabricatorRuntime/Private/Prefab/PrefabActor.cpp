//$ Copyright 2015-23, Code Respawn Technologies Pvt Ltd - All Rights Reserved $//

#include "Prefab/PrefabActor.h"

#include "Asset/PrefabricatorAsset.h"
#include "Asset/PrefabricatorAssetUserData.h"
#include "Prefab/PrefabComponent.h"
#include "Prefab/PrefabTools.h"
#include "Utils/PrefabricatorStats.h"

#include "IPropertyChangeListener.h"
#include "PropertyEditorModule.h"

// Unsupported component (seem to be added by default), that's why this is causing issue
#include "Components/BillboardComponent.h"
#include "Components/ActorComponent.h"

#include "Containers/Array.h"

#include "UObject/Package.h"

#include <functional>

DEFINE_LOG_CATEGORY_STATIC(LogPrefabActor, Log, All);


APrefabActor::APrefabActor(const FObjectInitializer& ObjectInitializer) 
	: Super(ObjectInitializer)
{
	PrefabComponent = ObjectInitializer.CreateDefaultSubobject<UPrefabComponent>(this, "PrefabComponent");
	RootComponent = PrefabComponent;
#if WITH_EDITOR
	FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject(this, &APrefabActor::OnObjectPropertyChanged);
	FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddUObject(this, &APrefabActor::OnPreObjectPropertyChanged);
#endif
}

namespace {
	void DestroyAttachedActorsRecursive(AActor* ActorToDestroy, TSet<AActor*>& Visited) {
		if (!ActorToDestroy || !ActorToDestroy->GetRootComponent()) return;

		if (Visited.Contains(ActorToDestroy)) return;
		Visited.Add(ActorToDestroy);

		UPrefabricatorAssetUserData* PrefabUserData = ActorToDestroy->GetRootComponent()->GetAssetUserData<UPrefabricatorAssetUserData>();
		if (!PrefabUserData) return;

		UWorld* World = ActorToDestroy->GetWorld();
		if (!World) return;

		TArray<AActor*> AttachedActors;
		ActorToDestroy->GetAttachedActors(AttachedActors);
		for (AActor* AttachedActor : AttachedActors) {
			DestroyAttachedActorsRecursive(AttachedActor, Visited);
		}
		ActorToDestroy->Destroy();
	}
}

void APrefabActor::Destroyed()
{
	Super::Destroyed();

	// Destroy all attached actors
	{
		TSet<AActor*> Visited;
		TArray<AActor*> AttachedActors;
		GetAttachedActors(AttachedActors);
		for (AActor* AttachedActor : AttachedActors) {
			DestroyAttachedActorsRecursive(AttachedActor, Visited);
		}
	}
}

void APrefabActor::PostLoad()
{
	Super::PostLoad();

}

void APrefabActor::PostActorCreated()
{
	Super::PostActorCreated();

	LoadPrefab();
}

#if WITH_EDITOR
void APrefabActor::PostEditChangeProperty(struct FPropertyChangedEvent& e)
{
	Super::PostEditChangeProperty(e);


}

void APrefabActor::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	Super::PostDuplicate(DuplicateMode);

	if (DuplicateMode == EDuplicateMode::Normal) {
		FRandomStream Random;
		Random.Initialize(FMath::Rand());
		RandomizeSeed(Random);

		FPrefabLoadSettings LoadSettings;
		LoadSettings.bRandomizeNestedSeed = true;
		LoadSettings.Random = &Random;
		FPrefabTools::LoadStateFromPrefabAsset(this, LoadSettings);
	}
}

FName APrefabActor::GetCustomIconName() const
{
	static const FName PrefabIconName("ClassIcon.PrefabActor");
	return PrefabIconName;
}
//

void PrependPropertyPath(FString& PropertyPath, const FProperty* Property, int32 PropertyElementIndex = -1)
{
	FString NestedPropertyName = Property->GetName();
	PropertyPath = (PropertyElementIndex < 0 ?
		FString::Format(TEXT("|{0}"), { NestedPropertyName }) :
		FString::Format(TEXT("|{0}[{1}]"), { NestedPropertyName, PropertyElementIndex })) + PropertyPath;
}

void GetPropertyPath_ArrayHelper(
	const FArrayProperty* ArrayProperty
	, FString& PropertyPath
	, int32 PropertyElementIndex = -1
)
{
	if (PropertyElementIndex != -1)
	{
		PrependPropertyPath(PropertyPath, ArrayProperty, PropertyElementIndex);
		PrependPropertyPath(PropertyPath, ArrayProperty, -1);
	}
}

/** Callback for object property modifications, called by UObject::PreEditChange with a full property chain */
void APrefabActor::OnPreObjectPropertyChanged(UObject* ObjectBeingModified, const FEditPropertyChain& Chain)
{
	if (ObjectBeingModified == this)
		return;

	std::function<bool(AActor*)> ProcessActorFn;
	std::function<bool(AActor*)> ForEachAttachedActorsFn;
	bool bIsObjectBeingModifiedAttached = false;
	TArray<FString> PathStack;

	ProcessActorFn = [
		this
			, &ProcessActorFn
			, &ForEachAttachedActorsFn
			, &bIsObjectBeingModifiedAttached
			, &ObjectBeingModified
			, &PathStack
	](AActor* AttachedActor)
		{
			if (!AttachedActor)
				return false;

			PathStack.Add(AttachedActor->GetName());
			EditObjectPath = FString::Join(PathStack, TEXT("/"));

			TArray<UActorComponent*> Components;
			AttachedActor->GetComponents(Components, false);
			for (auto& Component : Components)
			{
				if (!Component) continue;
				if (Component->IsA(UPrefabComponent::StaticClass()) || Component->IsA(UBillboardComponent::StaticClass()))
					continue;
				if (Component == ObjectBeingModified)
				{
					bIsObjectBeingModifiedAttached = true;
					UE_LOG(LogTemp, Warning, TEXT("Path: %s"), *EditObjectPath);
					return false;
				}
			}

			if (AttachedActor == ObjectBeingModified)
			{
				bIsObjectBeingModifiedAttached = true;
				UE_LOG(LogTemp, Warning, TEXT("Path: %s"), *EditObjectPath);
				return false;
			}

			if (
				// Recursively look if modified object is a child until next prefab
				// Until then, we have jurisdiction
				AttachedActor != this &&
				AttachedActor->IsA(APrefabActor::StaticClass())
				)
			{
				// Stop recursion here
				return false;
			}

			return true;
		};

		ForEachAttachedActorsFn = [
			this
				, &ProcessActorFn
				, &ForEachAttachedActorsFn
				, &bIsObjectBeingModifiedAttached
				, &ObjectBeingModified
				, &PathStack
		](AActor* AttachedActor)
			{
				if (!ProcessActorFn(AttachedActor))
					return false;

				AttachedActor->ForEachAttachedActors(ForEachAttachedActorsFn);
				PathStack.RemoveAt(PathStack.Num() - 1);
				return true;
			};
			if (ProcessActorFn(this))
			{
				ForEachAttachedActors(ForEachAttachedActorsFn);
			}

			if (bIsObjectBeingModifiedAttached)
			{
				FString PropertyPath = "";		
				auto& NonConstChain = const_cast<FEditPropertyChain&>(Chain);
				using PropNode = TDoubleLinkedList<FProperty*>::TDoubleLinkedListNode;
				for (PropNode* Node = Chain.GetHead(); Node; Node = Node->GetNextNode())
				{
					auto Property = Node->GetValue();
					EditPropertyChain.Add(Property);
				}
			}
}

void APrefabActor::OnObjectPropertyChanged(UObject* ObjectBeingModified, FPropertyChangedEvent& Event)
{
	if (EditPropertyChain.Num() != 0)
	{
		FString PropertyPath = "";
		for (auto& Property : EditPropertyChain)
		{
			if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
			{
				int32 PropertyElementIndex = Event.GetArrayIndex(Property->GetName());
				if (PropertyElementIndex != -1)
				{
					PrependPropertyPath(PropertyPath, ArrayProperty, PropertyElementIndex);
				}
			}

			PrependPropertyPath(PropertyPath, Property);
		}

		FString CombinedPath = PropertyPath + PropertyPath;
		if (!ChangeSet.Contains(CombinedPath))
		{
			if (auto PropertyChange = NewObject<UPrefabInstancePropertyChange>(this))
			{
				PropertyChange->ObjectPath = EditObjectPath;
				PropertyChange->PropertyPath = PropertyPath;
				ChangeSet.Add(CombinedPath, PropertyChange);
				Changes.Add(PropertyChange);
			}
		}
		
		EditObjectPath = "";
		EditPropertyChain.Empty();
	}
}

//void APrefabActor::HandlePropertyChangedEvent(FPropertyChangedEvent& PropertyChangedEvent)
//{
//	// Handle property changed events with this function (called from our OnObjectPropertyChanged delegate) instead of overriding PostEditChangeProperty because replicated
//	// multi-user transactions directly broadcast OnObjectPropertyChanged on the properties that were changed, instead of making PostEditChangeProperty events.
//	// Note that UObject::PostEditChangeProperty ends up broadcasting OnObjectPropertyChanged anyway, so this works just the same as before.
//	// see ConcertClientTransactionBridge.cpp, function ConcertClientTransactionBridgeUtil::ProcessTransactionEvent
//}
#endif // WITH_EDITOR

void APrefabActor::LoadPrefab()
{
	FPrefabTools::LoadStateFromPrefabAsset(this, FPrefabLoadSettings());
}

void APrefabActor::SavePrefab()
{
	FPrefabTools::SaveStateToPrefabAsset(this);
}

bool APrefabActor::IsPrefabOutdated()
{
	UPrefabricatorAsset* PrefabAsset = GetPrefabAsset();
	if (!PrefabAsset) {
		return false;
	}

	return PrefabAsset->LastUpdateID != LastUpdateID;
}

UPrefabricatorAsset* APrefabActor::GetPrefabAsset()
{
	FPrefabAssetSelectionConfig SelectionConfig;
	SelectionConfig.Seed = Seed;
	UPrefabricatorAssetInterface* PrefabAssetInterface = PrefabComponent->PrefabAssetInterface.LoadSynchronous();
	return PrefabAssetInterface ? PrefabAssetInterface->GetPrefabAsset(SelectionConfig) : nullptr;
}

void APrefabActor::RandomizeSeed(const FRandomStream& InRandom, bool bRecursive)
{
	Seed = FPrefabTools::GetRandomSeed(InRandom);
	if (bRecursive) {
		TArray<AActor*> AttachedChildren;
		GetAttachedActors(AttachedChildren);
		for (AActor* AttachedActor : AttachedChildren) {
			if (APrefabActor* ChildPrefab = Cast<APrefabActor>(AttachedActor)) {
				ChildPrefab->RandomizeSeed(InRandom, bRecursive);
			}
		}
	}
}

void APrefabActor::HandleBuildComplete()
{
	UPrefabricatorAssetInterface* PrefabAssetInterface = PrefabComponent->PrefabAssetInterface.LoadSynchronous();
	if (PrefabAssetInterface && PrefabAssetInterface->EventListener) {
		UPrefabricatorEventListener* EventListenerInstance = NewObject<UPrefabricatorEventListener>(GetTransientPackage(), PrefabAssetInterface->EventListener, NAME_None, RF_Transient);
		if (EventListenerInstance) {
			EventListenerInstance->PostSpawn(this);
		}
	}
}

////////////////////////////////// FPrefabBuildSystem //////////////////////////////////
FPrefabBuildSystem::FPrefabBuildSystem(double InTimePerFrame)
	: TimePerFrame(InTimePerFrame)
{
}

void FPrefabBuildSystem::Tick()
{
	double StartTime = FPlatformTime::Seconds();
	
	
	while (BuildStack.Num() > 0) {
		FPrefabBuildSystemCommandPtr Item = BuildStack.Pop();
		Item->Execute(*this);

		if (TimePerFrame > 0) {
			double ElapsedTime = FPlatformTime::Seconds() - StartTime;
			if (ElapsedTime >= TimePerFrame) {
				break;
			}
		}
	}
}

void FPrefabBuildSystem::Reset()
{
	BuildStack.Reset();
}

void FPrefabBuildSystem::PushCommand(FPrefabBuildSystemCommandPtr InCommand)
{
	BuildStack.Push(InCommand);
}

FPrefabBuildSystemCommand_BuildPrefab::FPrefabBuildSystemCommand_BuildPrefab(TWeakObjectPtr<APrefabActor> InPrefab, bool bInRandomizeNestedSeed, FRandomStream* InRandom)
	: Prefab(InPrefab)
	, bRandomizeNestedSeed(bInRandomizeNestedSeed)
	, Random(InRandom)
{
}

void FPrefabBuildSystemCommand_BuildPrefab::Execute(FPrefabBuildSystem& BuildSystem)
{
	if (Prefab.IsValid()) {
		FPrefabLoadSettings LoadSettings;
		LoadSettings.bRandomizeNestedSeed = bRandomizeNestedSeed;
		LoadSettings.Random = Random;

		// Nested prefabs will be recursively build on the stack over multiple frames
		LoadSettings.bSynchronousBuild = false;

		{
			SCOPE_CYCLE_COUNTER(STAT_Randomize_LoadPrefab);
			FPrefabTools::LoadStateFromPrefabAsset(Prefab.Get(), LoadSettings);
		}

		// Push a build complete notification request. Since this is a stack, it will execute after all the children are processed below
		const FPrefabBuildSystemCommandPtr CmdBuildComplete = MakeShareable(new FPrefabBuildSystemCommand_NotifyBuildComplete(Prefab));
		BuildSystem.PushCommand(CmdBuildComplete);
		
		// Add the child prefabs to the stack
		TArray<AActor*> ChildActors;
		{
			SCOPE_CYCLE_COUNTER(STAT_Randomize_GetChildActor);
			Prefab->GetAttachedActors(ChildActors);
		}
		for (AActor* ChildActor : ChildActors) {
			if (APrefabActor* ChildPrefab = Cast<APrefabActor>(ChildActor)) {
				const FPrefabBuildSystemCommandPtr CmdBuildPrefab = MakeShareable(new FPrefabBuildSystemCommand_BuildPrefab(ChildPrefab, bRandomizeNestedSeed, Random));
				BuildSystem.PushCommand(CmdBuildPrefab);
			}
		}
	}
}

/////////////////////////////////////

FPrefabBuildSystemCommand_BuildPrefabSync::FPrefabBuildSystemCommand_BuildPrefabSync(TWeakObjectPtr<APrefabActor> InPrefab, bool bInRandomizeNestedSeed, FRandomStream* InRandom)
	: Prefab(InPrefab)
	, bRandomizeNestedSeed(bInRandomizeNestedSeed)
	, Random(InRandom) 
{
}

void FPrefabBuildSystemCommand_BuildPrefabSync::Execute(FPrefabBuildSystem& BuildSystem)
{
	double StartTime = FPlatformTime::Seconds();
	if (Prefab.IsValid()) {
		Prefab->RandomizeSeed(*Random);

		FPrefabLoadSettings LoadSettings;
		LoadSettings.bRandomizeNestedSeed = true;
		LoadSettings.Random = Random;
		FPrefabTools::LoadStateFromPrefabAsset(Prefab.Get(), LoadSettings);
	}
	double EndTime = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Warning, TEXT("Exec Time: %fs"), (EndTime - StartTime));
}

/////////////////////////////////////

FPrefabBuildSystemCommand_NotifyBuildComplete::FPrefabBuildSystemCommand_NotifyBuildComplete(TWeakObjectPtr<APrefabActor> InPrefab)
	: Prefab(InPrefab)
{
}

void FPrefabBuildSystemCommand_NotifyBuildComplete::Execute(FPrefabBuildSystem& BuildSystem)
{
	if (Prefab.IsValid()) {
		// TODO: Execute Post spawn script
		Prefab->HandleBuildComplete();
	}
}


/////////////////////////////////////

AReplicablePrefabActor::AReplicablePrefabActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void AReplicablePrefabActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

}

void AReplicablePrefabActor::BeginPlay()
{
	if (GetLocalRole() == ROLE_Authority)
	{
		bReplicates = false;
		SetRemoteRoleForBackwardsCompat(ROLE_SimulatedProxy);
		SetReplicates(true);
	}

	Super::BeginPlay();
}

