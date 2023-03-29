#include "MZSceneTree.h"
#include "EditorCategoryUtils.h"
#include "ObjectEditorUtils.h"
#include "MZSceneTreeManager.h"
#include "Kismet2/ComponentEditorUtils.h"

#define LOG(x) UE_LOG(LogMZSceneTreeManager, Display, TEXT(x))
#define LOGF(x, y) UE_LOG(LogMZSceneTreeManager, Display, TEXT(x), y)

MZSceneTree::MZSceneTree()
{
	Root = TUniquePtr<FolderNode>(new FolderNode(nullptr));
	Root->Name = FString("UE5");
	Root->Parent = nullptr;
}

MZSceneTree& MZSceneTree::GetInstance()
{
	static MZSceneTree& sceneTree = FModuleManager::GetModuleChecked<FMZSceneTreeManager>("MZSceneTreeManager").SceneTree;
	return sceneTree;
}

FolderNode* MZSceneTree::FindOrAddChildFolder(TreeNode* node, FString name, TreeNode* mostRecentParent)
{
	for (auto& child : node->Children)
	{
		if (child->Name == name && child->GetAsFolderNode())
		{
			return reinterpret_cast<FolderNode*>(child.Get());
		}
	}
	FolderNode* newChild = new FolderNode(node);
	newChild->Name = name;
	if(name == NAME_Reality_FolderName.ToString())
	{
		newChild->mzMetaData.Add("NodeColor", HEXCOLOR_Reality_Node);	
	}
	newChild->Id = FGuid::NewGuid();
	node->Children.emplace_back(newChild);
	
	NodeMap.Add(newChild->Id, newChild);
	if (!mostRecentParent)
	{
		mostRecentParent = newChild;
	}
	return newChild;
}



void MZSceneTree::Clear()
{
	ClearRecursive(Root.Get());
	NodeMap.Empty();
	NodeMap.Add(Root->Id, Root.Get());
}

void MZSceneTree::ClearRecursive(TreeNode* node)
{
	for (auto& child : node->Children)
	{
		ClearRecursive(child.Get());
	}
	if (node == Root.Get())
	{
		Root->Children.clear();
		return;
	}
}

ActorNode* MZSceneTree::AddActor(FString folderPath, AActor* actor)
{
	return AddActor(folderPath, actor, nullptr);
}

ActorNode* MZSceneTree::AddActor(FString folderPath, AActor* actor, TreeNode* mostRecentParent)
{
	if (!actor)
	{
		return nullptr;
	}

	folderPath.RemoveFromStart(FString("None"));
	folderPath.RemoveFromStart(FString("/"));
	TArray<FString> folders;
	folderPath.ParseIntoArray(folders, TEXT("/"));
	
	TreeNode* ptr = Root.Get();
	for (auto item : folders)
	{
		ptr = FindOrAddChildFolder(ptr, item, mostRecentParent);
	}

	ActorNode* newChild = new ActorNode(actor, ptr);
	//todo fix display names newChild->Name = actor->GetActorLabel();
	newChild->Name = actor->GetFName().ToString();
	newChild->Id = actor->GetActorGuid();
	newChild->NeedsReload = true;
	ptr->Children.emplace_back(newChild);
	NodeMap.Add(newChild->Id, newChild);

	if (actor->GetRootComponent())
	{
		SceneComponentNode* loadingChild(new SceneComponentNode(newChild, nullptr));
		loadingChild->Name = "Loading...";
		loadingChild->Id = FGuid::NewGuid();
		newChild->Children.emplace_back(loadingChild);
	}
	if (!mostRecentParent)
	{
		mostRecentParent = newChild;
	}
	return newChild;
}

ActorNode* MZSceneTree::AddActor(TreeNode* parent, AActor* actor)
{
	if (!actor)
	{
		return nullptr;
	}
	if (!parent)
	{
		parent = Root.Get();
	}

	ActorNode* newChild(new ActorNode(actor, parent));
	newChild->Name = actor->GetActorLabel();
	newChild->Id = actor->GetActorGuid();
	newChild->NeedsReload = true;
	parent->Children.emplace_back(newChild);
	NodeMap.Add(newChild->Id, newChild);

	if (actor->GetRootComponent())
	{
		SceneComponentNode* loadingChild(new SceneComponentNode(newChild, nullptr));
		loadingChild->Name = "Loading...";
		loadingChild->Id = FGuid::NewGuid();
		newChild->Children.emplace_back(loadingChild);
	}

	return newChild;
}

SceneComponentNode* MZSceneTree::AddSceneComponent(SceneComponentNode* parent, USceneComponent* sceneComponent)
{
	SceneComponentNode* newComponentNode(new SceneComponentNode(parent, sceneComponent));
	newComponentNode->Id = FGuid::NewGuid();
	newComponentNode->Name = sceneComponent->GetFName().ToString();
	newComponentNode->NeedsReload = true;
	parent->Children.emplace_back(newComponentNode);
	NodeMap.Add(newComponentNode->Id, newComponentNode);

	SceneComponentNode* loadingChild(new SceneComponentNode(newComponentNode, nullptr));
	loadingChild->Name = "Loading...";
	loadingChild->Id = FGuid::NewGuid();
	newComponentNode->Children.emplace_back(loadingChild);

	return newComponentNode;
}

SceneComponentNode* MZSceneTree::AddSceneComponent(SceneComponentNode* parent, USceneComponent* sceneComponent)
{
	SceneComponentNode* newComponentNode(new SceneComponentNode(parent, sceneComponent));
	newComponentNode->Id = FGuid::NewGuid();
	newComponentNode->Name = sceneComponent->GetFName().ToString();
	newComponentNode->NeedsReload = true;
	parent->Children.emplace_back(newComponentNode);
	NodeMap.Add(newComponentNode->Id, newComponentNode);

	SceneComponentNode* loadingChild(new SceneComponentNode(newComponentNode, nullptr));
	loadingChild->Name = "Loading...";
	loadingChild->Id = FGuid::NewGuid();
	newComponentNode->Children.emplace_back(loadingChild);

	return newComponentNode;
}


flatbuffers::Offset<mz::fb::Node> TreeNode::Serialize(flatbuffers::FlatBufferBuilder& fbb)
{
	std::vector<flatbuffers::Offset<mz::fb::MetaDataEntry>> metadata;
	for (auto [key, value] : mzMetaData)
	{
		metadata.push_back(mz::fb::CreateMetaDataEntryDirect(fbb, TCHAR_TO_UTF8(*key), TCHAR_TO_UTF8(*value)));
	}
	std::vector<flatbuffers::Offset<mz::fb::Node>> childNodes = SerializeChildren(fbb);
	return mz::fb::CreateNodeDirect(fbb, (mz::fb::UUID*)&Id, TCHAR_TO_UTF8(*Name), TCHAR_TO_UTF8(*GetClassDisplayName()), false, true, 0, 0, mz::fb::NodeContents::Graph, mz::fb::CreateGraphDirect(fbb, &childNodes).Union(), "UE5", 0, 0, 0, 0, &metadata);
}

flatbuffers::Offset<mz::fb::Node> ActorNode::Serialize(flatbuffers::FlatBufferBuilder& fbb)
{
	std::vector<flatbuffers::Offset<mz::fb::MetaDataEntry>> metadata;
	for (auto [key, value] : mzMetaData)
	{
		metadata.push_back(mz::fb::CreateMetaDataEntryDirect(fbb, TCHAR_TO_UTF8(*key), TCHAR_TO_UTF8(*value)));
	}

	std::vector<flatbuffers::Offset<mz::fb::Node>> childNodes = SerializeChildren(fbb);
	std::vector<flatbuffers::Offset<mz::fb::Pin>> pins = SerializePins(fbb);
	return mz::fb::CreateNodeDirect(fbb, (mz::fb::UUID*)&Id, TCHAR_TO_UTF8(*Name), TCHAR_TO_UTF8(*GetClassDisplayName()), false, true, &pins, 0, mz::fb::NodeContents::Graph, mz::fb::CreateGraphDirect(fbb, &childNodes).Union(), "UE5", 0, 0, 0, 0, &metadata);
}

std::vector<flatbuffers::Offset<mz::fb::Pin>> ActorNode::SerializePins(flatbuffers::FlatBufferBuilder& fbb)
{
	std::vector<flatbuffers::Offset<mz::fb::Pin>> pins;
	for (auto& mzprop : Properties)
	{
		auto newPins = mzprop->Serialize(fbb);
		pins.insert(pins.begin(), newPins.begin(), newPins.end());
	}
	return pins;
}

void ActorNode::PopulateNode()
{
	if (!ActorRef)
	{
		return;
	}
	bool ColoredChilds = false;
	if (mzMetaData.Contains("NodeColor"))
	{
		ColoredChilds = true;
	}
	auto* ActorClass = ActorRef->GetClass();

	//ITERATE PROPERTIES BEGIN
	for (TFieldIterator<FProperty> propIt(ActorClass); propIt; propIt++) {
		FProperty* AProperty = *propIt;
		FName CategoryName = FObjectEditorUtils::GetCategoryFName(AProperty);

		if (FEditorCategoryUtils::IsCategoryHiddenFromClass(ActorClass, CategoryName.ToString()) 
			|| !MZSceneTree::PropertyVisible(AProperty))
		{
			AProperty = AProperty->PropertyLinkNext;
			continue;
		}
		auto mzprop = FMZPropertyManager::GetInstance().CreateFProperty(AProperty, &ActorRef);
		if (!mzprop)
		{
			continue;
		}
		Properties.push_back(mzprop);
	}

	//ITERATE PROPERTIES END

	//ITERATE FUNCTIONS BEGIN
	auto ActorComponent = ActorRef->GetRootComponent();
	for (TFieldIterator<UFunction> FuncIt(ActorClass, EFieldIteratorFlags::IncludeSuper); FuncIt; ++FuncIt)
	{
		UFunction* UEFunction = *FuncIt;
		if (UEFunction->HasAllFunctionFlags(FUNC_BlueprintCallable | FUNC_Public) &&
			!UEFunction->HasAllFunctionFlags(FUNC_Event))
		{
			auto UEFunctionName = UEFunction->GetFName().ToString();

			if (UEFunctionName.StartsWith("OnChanged_") || UEFunctionName.StartsWith("OnLengthChanged_"))
			{
				continue; // do not export user's changed handler functions
			}

			//auto OwnerClass = UEFunction->GetOwnerClass();
			//if (!OwnerClass || !Cast<UBlueprint>(OwnerClass->ClassGeneratedBy))
			//{
			//	//continue; // export only BP functions //? what we will show in mediaz
			//}

			TUniquePtr<MZFunction> mzfunc(new MZFunction(ActorRef.GetAsActor(), UEFunction));

			// Parse all function parameters.

			for (TFieldIterator<FProperty> PropIt(UEFunction); PropIt && PropIt->HasAnyPropertyFlags(CPF_Parm); ++PropIt)
			{
				//Todo don't give nullptr here
				auto mzprop = FMZPropertyManager::GetInstance().CreateFProperty(*PropIt, nullptr);
				if (mzprop)
				{
					if (PropIt->HasAnyPropertyFlags(CPF_OutParm))
					{
						mzfunc->OutProperties.push_back(mzprop.Get());
					}
					mzfunc->Properties.emplace_back(std::move(mzprop));
				}
			}

			//TODO Add to Registered Functions
			//RegisteredFunctions.Add(mzfunc->Id, mzfunc);
			Functions.emplace_back(std::move(mzfunc));
		}
	}
	//ITERATE FUNCTIONS END

	//ITERATE CHILD COMPONENTS TO SHOW BEGIN
	Children.clear();

	auto& sceneTree = MZSceneTree::GetInstance();

	auto* unattachedChildsPtr = sceneTree.ChildMap.Find(Id);
	TSet<AActor*> unattachedChilds = unattachedChildsPtr ? *unattachedChildsPtr : TSet<AActor*>();
	for (auto child : unattachedChilds)
	{
		sceneTree.AddActor(this, child);
	}

	AActor* ActorContext = ActorRef.GetAsActor();
	TSet<UActorComponent*> ComponentsToAdd(ActorContext->GetComponents());

	const bool bHideConstructionScriptComponentsInDetailsView = false; //GetDefault<UBlueprintEditorSettings>()->bHideConstructionScriptComponentsInDetailsView;
	auto ShouldAddInstancedActorComponent = [bHideConstructionScriptComponentsInDetailsView](UActorComponent* ActorComp, USceneComponent* ParentSceneComp)
	{
		// Exclude nested DSOs attached to BP-constructed instances, which are not mutable.
		return (ActorComp != nullptr
			&& (!ActorComp->IsVisualizationComponent())
			&& (ActorComp->CreationMethod != EComponentCreationMethod::UserConstructionScript || !bHideConstructionScriptComponentsInDetailsView)
			&& (ParentSceneComp == nullptr || !ParentSceneComp->IsCreatedByConstructionScript() || !ActorComp->HasAnyFlags(RF_DefaultSubObject)))
			&& (ActorComp->CreationMethod != EComponentCreationMethod::Native || FComponentEditorUtils::GetPropertyForEditableNativeComponent(ActorComp));
	};

	// Filter the components by their visibility
	for (TSet<UActorComponent*>::TIterator It(ComponentsToAdd.CreateIterator()); It; ++It)
	{
		UActorComponent* ActorComp = *It;
		USceneComponent* SceneComp = Cast<USceneComponent>(ActorComp);
		USceneComponent* ParentSceneComp = SceneComp != nullptr ? SceneComp->GetAttachParent() : nullptr;
		if (!ShouldAddInstancedActorComponent(ActorComp, ParentSceneComp))
		{
			It.RemoveCurrent();
		}
	}

	TArray<SceneComponentNode*> OutArray;

	TFunction<void(USceneComponent*, TreeNode*)> AddInstancedComponentsRecursive 
		= [&](USceneComponent* Component, TreeNode* ParentHandle)
	{
		if (Component != nullptr)
		{
			for (USceneComponent* ChildComponent : Component->GetAttachChildren())
			{
				if (ComponentsToAdd.Contains(ChildComponent) && ChildComponent->GetOwner() == Component->GetOwner())
				{
					ComponentsToAdd.Remove(ChildComponent);
					SceneComponentNode* NewParentHandle = nullptr;
					if (auto ParentAsActorNode = ParentHandle->GetAsActorNode())
					{
						NewParentHandle = sceneTree.AddSceneComponent(ParentAsActorNode, ChildComponent);
					}
					else if (auto ParentAsSceneComponentNode = ParentHandle->GetAsSceneComponentNode())
					{
						NewParentHandle = sceneTree.AddSceneComponent(ParentAsSceneComponentNode, ChildComponent);
					}


					if (!NewParentHandle)
					{
						LOG("A Child node other than actor or component is present!");
						continue;
					}
					if (ColoredChilds)
					{
						NewParentHandle->mzMetaData.Add("NodeColor", HEXCOLOR_Reality_Node);
					}
					NewParentHandle->Children.clear();
					OutArray.Add(NewParentHandle);

					AddInstancedComponentsRecursive(ChildComponent, NewParentHandle);
				}
			}
		}
	};

	USceneComponent* RootComponent = ActorContext->GetRootComponent();

	// Add the root component first
	if (RootComponent != nullptr)
	{
		// We want this to be first every time, so remove it from the set of components that will be added later
		ComponentsToAdd.Remove(RootComponent);

		// Add the root component first
		auto RootHandle = sceneTree.AddSceneComponent(this, RootComponent);
		if (RootHandle)
		{
			if (ColoredChilds)
			{
				RootHandle->mzMetaData.Add("NodeColor", HEXCOLOR_Reality_Node);
			}
			// Clear the loading child
			RootHandle->Children.clear();
			OutArray.Add(RootHandle);
		}
		// Recursively add
		AddInstancedComponentsRecursive(RootComponent, RootHandle);
	}

	// Sort components by type (always put scene components first in the tree)
	ComponentsToAdd.Sort([](const UActorComponent& A, const UActorComponent& /* B */)
		{
			return A.IsA<USceneComponent>();
		});

	// Now add any remaining instanced owned components not already added above. This will first add any
	// unattached scene components followed by any instanced non-scene components owned by the Actor instance.
	for (UActorComponent* ActorComp : ComponentsToAdd)
	{
		// Create new subobject data with the original data as their parent.
		//OutArray.Add(SceneTree.AddSceneComponent(componentNode, ActorComp)); //TODO scene tree add actor components
	}
	//ITERATE CHILD COMPONENTS TO SHOW END

}

flatbuffers::Offset<mz::fb::Node> SceneComponentNode::Serialize(flatbuffers::FlatBufferBuilder& fbb)
{
	std::vector<flatbuffers::Offset<mz::fb::MetaDataEntry>> metadata;
	for (auto [key, value] : mzMetaData)
	{
		metadata.push_back(mz::fb::CreateMetaDataEntryDirect(fbb, TCHAR_TO_UTF8(*key), TCHAR_TO_UTF8(*value)));
	}
	std::vector<flatbuffers::Offset<mz::fb::Node>> childNodes = SerializeChildren(fbb);
	std::vector<flatbuffers::Offset<mz::fb::Pin>> pins = SerializePins(fbb);
	return mz::fb::CreateNodeDirect(fbb, (mz::fb::UUID*)&Id, TCHAR_TO_UTF8(*Name), TCHAR_TO_UTF8(*GetClassDisplayName()), false, true, &pins, 0, mz::fb::NodeContents::Graph, mz::fb::CreateGraphDirect(fbb, &childNodes).Union(), "UE5", 0, 0, 0, 0, &metadata);
}

std::vector<flatbuffers::Offset<mz::fb::Pin>> SceneComponentNode::SerializePins(flatbuffers::FlatBufferBuilder& fbb)
{
	std::vector<flatbuffers::Offset<mz::fb::Pin>> pins;
	for (auto& mzprop : Properties)
	{
		auto newPins = mzprop->Serialize(fbb);
		pins.insert(pins.begin(), newPins.begin(), newPins.end());
	}
	return pins;
}

void SceneComponentNode::PopulateNode()
{
	for (TFieldIterator<FProperty> propIt(SceneComponentRef->GetClass()); propIt; propIt++) {
		auto* prop = *propIt;
		FName CategoryName = FObjectEditorUtils::GetCategoryFName(prop);
		UClass* Class = SceneComponentRef.GetOwnerActor()->GetAsActor()->StaticClass();

		if (FEditorCategoryUtils::IsCategoryHiddenFromClass(Class, CategoryName.ToString()) || !MZSceneTree::PropertyVisible(prop))
		{
			continue;
		}

		auto mzprop = FMZPropertyManager::GetInstance().CreateFProperty(prop, &SceneComponentRef);

		if (mzprop)
			Properties.emplace_back(std::move(mzprop));
	}


}

std::vector<flatbuffers::Offset<mz::fb::Node>> TreeNode::SerializeChildren(flatbuffers::FlatBufferBuilder& fbb)
{
	std::vector<flatbuffers::Offset<mz::fb::Node>> childNodes;

	if (Children.empty())
	{
		return childNodes;
	}

	for (auto& child : Children)
	{
		childNodes.push_back(child->Serialize(fbb));
	}

	return childNodes;
}

bool MZSceneTree::PropertyVisible(FProperty* ueproperty)
{
	return !ueproperty->HasAllPropertyFlags(CPF_DisableEditOnInstance) &&
		!ueproperty->HasAllPropertyFlags(CPF_Deprecated) &&
		//!ueproperty->HasAllPropertyFlags(CPF_EditorOnly) && //? dont know what this flag does but it hides more than necessary
		ueproperty->HasAllPropertyFlags(CPF_Edit) &&
		//ueproperty->HasAllPropertyFlags(CPF_BlueprintVisible) && //? dont know what this flag does but it hides more than necessary
		ueproperty->HasAllFlags(RF_Public);
}