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
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 4
	FCoreUObjectDelegates::OnObjectPropertyChangedChain.AddUObject(this, &APrefabActor::OnObjectPropertyChangedChain);
#endif
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
void APrefabActor::PostEditChangeProperty(struct FPropertyChangedEvent& Event)
{
	Super::PostEditChangeProperty(Event);
}

void APrefabActor::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& Event)
{
	Super::PostEditChangeChainProperty(Event);

	FName PropertyName = (Event.Property != NULL) ? Event.Property->GetFName() : NAME_None;

	if (PropertyName == GET_MEMBER_NAME_CHECKED(FPrefabInstancePropertyChange, bIsStaged))
	{
		if(auto PropertyNode = Event.PropertyChain.GetHead())
		{
			if (auto Property = PropertyNode->GetValue())
			{
				int32 PropertyElementIndex = Event.GetArrayIndex(Property->GetName());
				if (PropertyElementIndex != -1)
				{
					FPrefabInstancePropertyChange Change;
					if (Property->GetName() == GET_MEMBER_NAME_CHECKED(APrefabActor, UnstagedChanges))
					{
						if (UnstagedChanges[PropertyElementIndex].bIsStaged)
						{
							Change = UnstagedChanges[PropertyElementIndex];
							StagedChanges.Add(Change);
							UnstagedChanges.RemoveAt(PropertyElementIndex);
						}
					}
					else if (Property->GetName() == GET_MEMBER_NAME_CHECKED(APrefabActor, StagedChanges))
					{
						if (!StagedChanges[PropertyElementIndex].bIsStaged)
						{
							Change = StagedChanges[PropertyElementIndex];
							UnstagedChanges.Add(Change);
							StagedChanges.RemoveAt(PropertyElementIndex);
						}
					}
					if (auto cachedChange = ChangeSet.Find(Change))
					{
						cachedChange->bIsStaged = Change.bIsStaged;
					}
				}
			}
		}
	}
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

/** Callback for object property modifications, called by UObject::PreEditChange with a full property chain */
void APrefabActor::OnObjectPropertyChangedChain(UObject* ObjectBeingModified, FPropertyChangedChainEvent& Event)
{
    if (ObjectBeingModified == this)
        return;

    std::function<bool(AActor*)> ProcessActorFn;
    std::function<bool(AActor*)> ForEachAttachedActorsFn;
    bool bIsObjectBeingModifiedAttached = false;
    TArray<FString> PathStack;

    FString EditObjectPath = "";
    ProcessActorFn = [
        this
		, &ProcessActorFn
		, &ForEachAttachedActorsFn
		, &bIsObjectBeingModifiedAttached
		, &ObjectBeingModified
		, &EditObjectPath
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

    if (!bIsObjectBeingModifiedAttached)
        return;

    FString PropertyPath = "";
    TArray<FProperty*> EditPropertyChain;
    auto& NonConstChain = const_cast<FEditPropertyChain&>(Event.PropertyChain);
    using PropNode = TDoubleLinkedList<FProperty*>::TDoubleLinkedListNode;
    for (PropNode* Node = NonConstChain.GetHead(); Node; Node = Node->GetNextNode())
    {
        auto Property = Node->GetValue();
        EditPropertyChain.Add(Property);
    }

	if (EditPropertyChain.Num() != 0)
	{
		FString PropertyPath = "";
		for (auto it = EditPropertyChain.rbegin(); it != EditPropertyChain.rend(); ++it)
		{
			auto& Property = *it;
			if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
			{
				int32 PropertyElementIndex = Event.GetArrayIndex(Property->GetName());
				if (PropertyElementIndex != -1)
				{
					PrependPropertyPath(PropertyPath, ArrayProperty, PropertyElementIndex);
				}
			}
			else
			{
				PrependPropertyPath(PropertyPath, Property);
			}
		}

		TSoftObjectPtr ObjectBeingModifiedPtr(ObjectBeingModified);
		FPrefabInstancePropertyChange Key(ObjectBeingModifiedPtr, PropertyPath);
		if (!ChangeSet.Contains(Key))
		{			
			UnstagedChanges.Emplace(Key);
			ChangeSet.Add(Key);
		}
	}

	for (auto& change : UnstagedChanges)
	{
		change.UpdateDisplayName();
	}
	for (auto& change : StagedChanges)
	{
		change.UpdateDisplayName();
	}
}

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

