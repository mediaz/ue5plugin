// Copyright MediaZ AS. All Rights Reserved.

#include "MZSceneTree.h"

#include "Chaos/AABB.h"
#include "Chaos/AABB.h"
#include "Chaos/AABB.h"
#include "Chaos/AABB.h"
#include "Chaos/AABB.h"
#include "Chaos/AABB.h"
#include "Chaos/AABB.h"
#include "Chaos/AABB.h"

MZSceneTree::MZSceneTree()
{
	Root = TSharedPtr<FolderNode>(new FolderNode);
	Root->mzMetaData.Add("PinnedCategories", "Control");
	Root->Name = FMZClient::AppKey;
	Root->Parent = nullptr;
}

TSharedPtr<FolderNode> MZSceneTree::FindOrAddChildFolder(TSharedPtr<TreeNode> node, FString name, TSharedPtr<TreeNode>& mostRecentParent)
{
	for (auto child : node->Children)
	{
		if (child->Name == name && child->GetAsFolderNode())
		{
			return StaticCastSharedPtr<FolderNode>(child);
		}
	}
	TSharedPtr<FolderNode> newChild(new FolderNode);
	newChild->Parent = node.Get();
	newChild->Name = name;
	if(name == NAME_Reality_FolderName.ToString())
	{
		newChild->mzMetaData.Add("NodeColor", HEXCOLOR_Reality_Node);	
	}
	newChild->Id = FGuid::NewGuid();
	node->Children.push_back(newChild);
	NodeMap.Add(newChild->Id, newChild);
	if (!mostRecentParent)
	{
		mostRecentParent = newChild;
	}
	return newChild;
}



void MZSceneTree::Clear()
{
	ClearRecursive(Root);
	NodeMap.Empty();
	ActorIdToNodeId.Empty();
	NodeMap.Add(Root->Id, Root);
}

void MZSceneTree::ClearRecursive(TSharedPtr<TreeNode> node)
{
	for (auto child : node->Children)
	{
		ClearRecursive(child);
	}
	if (node == Root)
	{
		Root->Children.clear();
		return;
	}
}

TSharedPtr<ActorNode> MZSceneTree::AddActor(FString folderPath, AActor* actor, FGuid ForcedGuid)
{
	TSharedPtr<TreeNode> mostRecentParent;
	return AddActor(folderPath, actor, mostRecentParent, ForcedGuid);
}

TSharedPtr<ActorNode> MZSceneTree::AddActor(FString folderPath, AActor* actor, TSharedPtr<TreeNode>& mostRecentParent, FGuid ForcedGuid)
{
	if (!actor)
	{
		return nullptr;
	}

	folderPath.RemoveFromStart(FString("None"));
	folderPath.RemoveFromStart(FString("/"));
	TArray<FString> folders;
	folderPath.ParseIntoArray(folders, TEXT("/"));
	
	TSharedPtr<TreeNode> ptr = Root;
	for (auto item : folders)
	{
		ptr = FindOrAddChildFolder(ptr, item, mostRecentParent);
	}

	TSharedPtr<ActorNode> newChild(new ActorNode);
	newChild->Parent = ptr.Get();
	//todo fix display names newChild->Name = actor->GetActorLabel();
	newChild->Name = actor->GetFName().ToString();
	newChild->Id = ForcedGuid.IsValid() ? ForcedGuid : FGuid::NewGuid();
	newChild->actor = MZActorReference(actor);
	newChild->NeedsReload = true;
	ptr->Children.push_back(newChild);
	NodeMap.Add(newChild->Id, newChild);
	ActorIdToNodeId.Add(actor->GetActorGuid(), newChild->Id);

	if (actor->GetRootComponent())
	{
		TSharedPtr<SceneComponentNode> loadingChild(new SceneComponentNode);
		loadingChild->Name = "Loading...";
		loadingChild->Id = FGuid::NewGuid();
		loadingChild->Parent = newChild.Get();
		newChild->Children.push_back(loadingChild);
	}
	if (!mostRecentParent)
	{
		mostRecentParent = newChild;
	}
	return newChild;
}

TSharedPtr<ActorNode> MZSceneTree::AddActor(TreeNode* parent, AActor* actor)
{
	if (!actor)
	{
		return nullptr;
	}
	if (!parent)
	{
		parent = Root.Get();
	}

	TSharedPtr<ActorNode> newChild(new ActorNode);
	newChild->Parent = parent;
	newChild->Name = actor->GetActorLabel();
	newChild->Id = FGuid::NewGuid();
	newChild->actor = MZActorReference(actor);
	newChild->NeedsReload = true;
	parent->Children.push_back(newChild);
	NodeMap.Add(newChild->Id, newChild);
	ActorIdToNodeId.Add(actor->GetActorGuid(), newChild->Id);
	
	if (actor->GetRootComponent())
	{
		TSharedPtr<SceneComponentNode> loadingChild(new SceneComponentNode);
		loadingChild->Name = "Loading...";
		loadingChild->Id = FGuid::NewGuid();
		loadingChild->Parent = newChild.Get();
		newChild->Children.push_back(loadingChild);
	}

	return newChild;
}

TSharedPtr<SceneComponentNode> MZSceneTree::AddSceneComponent(ActorNode* parent, USceneComponent* sceneComponent)
{
	TSharedPtr<SceneComponentNode>newComponentNode(new SceneComponentNode);
	newComponentNode->mzMetaData.Add("PinnedCategories", "Transform");
	newComponentNode->sceneComponent = MZComponentReference(sceneComponent);
	newComponentNode->Id = FGuid::NewGuid();
	newComponentNode->Name = sceneComponent->GetFName().ToString();
	newComponentNode->Parent = parent;
	newComponentNode->NeedsReload = true;
	parent->Children.push_back(newComponentNode);
	NodeMap.Add(newComponentNode->Id, newComponentNode);

	TSharedPtr<SceneComponentNode> loadingChild(new SceneComponentNode);
	loadingChild->Name = "Loading...";
	loadingChild->Id = FGuid::NewGuid();
	loadingChild->Parent = newComponentNode.Get();
	newComponentNode->Children.push_back(loadingChild);

	return newComponentNode;
}

TSharedPtr<SceneComponentNode> MZSceneTree::AddSceneComponent(TSharedPtr<SceneComponentNode> parent, USceneComponent* sceneComponent)
{
	TSharedPtr<SceneComponentNode> newComponentNode(new SceneComponentNode);
	newComponentNode->mzMetaData.Add("PinnedCategories", "Transform");
	newComponentNode->sceneComponent = MZComponentReference(sceneComponent);
	newComponentNode->Id = FGuid::NewGuid();
	newComponentNode->Name = sceneComponent->GetFName().ToString();
	newComponentNode->Parent = parent.Get();
	newComponentNode->NeedsReload = true;
	parent->Children.push_back(newComponentNode);
	NodeMap.Add(newComponentNode->Id, newComponentNode);

	TSharedPtr<SceneComponentNode> loadingChild(new SceneComponentNode);
	loadingChild->Name = "Loading...";
	loadingChild->Id = FGuid::NewGuid();
	loadingChild->Parent = newComponentNode.Get();
	newComponentNode->Children.push_back(loadingChild);

	return newComponentNode;
}

ActorNode* MZSceneTree::GetNode(AActor* Actor)
{
	if(!Actor)
		return  nullptr;
	return GetNodeFromActorId(Actor->GetActorGuid());
}

ActorNode* MZSceneTree::GetNodeFromActorId(FGuid ActorId)
{
	if(!ActorIdToNodeId.Contains(ActorId))
		return  nullptr;
	
	auto NodeId = ActorIdToNodeId.FindRef(ActorId);
	if(!NodeMap.Contains(NodeId))
		return nullptr;
	
	auto Node = NodeMap.FindRef(NodeId);
	return Node->GetAsActorNode();
}

FGuid MZSceneTree::GetNodeIdActorId(FGuid ActorId)
{
	if(!ActorIdToNodeId.Contains(ActorId))
		return {};
	
	return ActorIdToNodeId.FindRef(ActorId);
}

TreeNode* MZSceneTree::GetNode(FGuid NodeId)
{
	if(!NodeMap.Contains(NodeId))
		return nullptr;
	
	auto Node = NodeMap.FindRef(NodeId);
	return Node.Get();
}

void MZSceneTree::RemoveNode(FGuid NodeId)
{
	NodeMap.Remove(NodeId);
}

flatbuffers::Offset<mz::fb::Node> TreeNode::Serialize(flatbuffers::FlatBufferBuilder& fbb)
{
	std::vector<flatbuffers::Offset<mz::fb::MetaDataEntry>> metadata = SerializeMetaData(fbb);
	std::vector<flatbuffers::Offset<mz::fb::Node>> childNodes = SerializeChildren(fbb);
	return mz::fb::CreateNodeDirect(fbb, (mz::fb::UUID*)&Id, TCHAR_TO_UTF8(*Name), TCHAR_TO_UTF8(*GetClassDisplayName()), false, true, 0, 0, mz::fb::NodeContents::Graph, mz::fb::CreateGraphDirect(fbb, &childNodes).Union(), TCHAR_TO_ANSI(*FMZClient::AppKey), 0, 0, 0, 0, &metadata);
}

flatbuffers::Offset<mz::fb::Node> ActorNode::Serialize(flatbuffers::FlatBufferBuilder& fbb)
{
	std::vector<flatbuffers::Offset<mz::fb::MetaDataEntry>> metadata = SerializeMetaData(fbb);
	std::vector<flatbuffers::Offset<mz::fb::Node>> childNodes = SerializeChildren(fbb);
	std::vector<flatbuffers::Offset<mz::fb::Pin>> pins = SerializePins(fbb);
	return mz::fb::CreateNodeDirect(fbb, (mz::fb::UUID*)&Id, TCHAR_TO_UTF8(*Name), TCHAR_TO_UTF8(*GetClassDisplayName()), false, true, &pins, 0, mz::fb::NodeContents::Graph, mz::fb::CreateGraphDirect(fbb, &childNodes).Union(), TCHAR_TO_ANSI(*FMZClient::AppKey), 0, 0, 0, 0, &metadata, 0, 0, TCHAR_TO_UTF8(*actor->GetActorLabel()));
}

std::vector<flatbuffers::Offset<mz::fb::Pin>> ActorNode::SerializePins(flatbuffers::FlatBufferBuilder& fbb)
{
	std::vector<flatbuffers::Offset<mz::fb::Pin>> pins;
	for (auto mzprop : Properties)
	{
		pins.push_back(mzprop->Serialize(fbb));
	}
	return pins;
}

ActorNode::~ActorNode()
{
}

flatbuffers::Offset<mz::fb::Node> SceneComponentNode::Serialize(flatbuffers::FlatBufferBuilder& fbb)
{
	std::vector<flatbuffers::Offset<mz::fb::MetaDataEntry>> metadata = SerializeMetaData(fbb);
	std::vector<flatbuffers::Offset<mz::fb::Node>> childNodes = SerializeChildren(fbb);
	std::vector<flatbuffers::Offset<mz::fb::Pin>> pins = SerializePins(fbb);
	return mz::fb::CreateNodeDirect(fbb, (mz::fb::UUID*)&Id, TCHAR_TO_UTF8(*Name), TCHAR_TO_UTF8(*GetClassDisplayName()), false, true, &pins, 0, mz::fb::NodeContents::Graph, mz::fb::CreateGraphDirect(fbb, &childNodes).Union(), TCHAR_TO_ANSI(*FMZClient::AppKey), 0, 0, 0, 0, &metadata);
}

std::vector<flatbuffers::Offset<mz::fb::Pin>> SceneComponentNode::SerializePins(flatbuffers::FlatBufferBuilder& fbb)
{
	std::vector<flatbuffers::Offset<mz::fb::Pin>> pins;
	for (auto mzprop : Properties)
	{
		pins.push_back(mzprop->Serialize(fbb));
	}
	return pins;
}

std::vector<flatbuffers::Offset<mz::fb::Node>> TreeNode::SerializeChildren(flatbuffers::FlatBufferBuilder& fbb)
{
	std::vector<flatbuffers::Offset<mz::fb::Node>> childNodes;

	if (Children.empty())
	{
		return childNodes;
	}

	for (auto child : Children)
	{
		childNodes.push_back(child->Serialize(fbb));
	}

	return childNodes;
}

std::vector<flatbuffers::Offset<mz::fb::MetaDataEntry>> TreeNode::SerializeMetaData(flatbuffers::FlatBufferBuilder& fbb)
{
	std::vector<flatbuffers::Offset<mz::fb::MetaDataEntry>> metadata;
	for (auto [key, value] : mzMetaData)
	{
		metadata.push_back(mz::fb::CreateMetaDataEntryDirect(fbb, TCHAR_TO_UTF8(*key), TCHAR_TO_UTF8(*value)));
	}
	return metadata;
}

TreeNode::~TreeNode()
{
}


SceneComponentNode::~SceneComponentNode()
{
}

FolderNode::~FolderNode()
{
}


