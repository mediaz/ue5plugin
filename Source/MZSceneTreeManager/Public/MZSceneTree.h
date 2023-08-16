/*
 * Copyright MediaZ AS. All Rights Reserved.
 */

#pragma once
#include "CoreMinimal.h"
#include "MZActorProperties.h"
#include "MZActorFunctions.h"
#include <vector>
#pragma warning (disable : 4800)
#pragma warning (disable : 4668)

struct ActorNode;
struct FolderNode;
struct SceneComponentNode;

static const FName NAME_Reality_FolderName(TEXT("Reality Actors"));
static const FString HEXCOLOR_Reality_Node(TEXT("0xFE5000"));

struct MZSCENETREEMANAGER_API  TreeNode {

	virtual ActorNode* GetAsActorNode() { return nullptr; };
	virtual FolderNode* GetAsFolderNode() { return nullptr; };
	virtual SceneComponentNode* GetAsSceneComponentNode() { return nullptr; };
	virtual FString GetClassDisplayName() = 0;
	
	FString Name;
	TreeNode* Parent;
	FGuid Id;
	bool NeedsReload = true;
	std::vector<TSharedPtr<TreeNode>> Children;
	TMap<FString, FString> mzMetaData;

	
	virtual flatbuffers::Offset<mz::fb::Node> Serialize(flatbuffers::FlatBufferBuilder& fbb);
	std::vector<flatbuffers::Offset<mz::fb::Node>> SerializeChildren(flatbuffers::FlatBufferBuilder& fbb);
	std::vector<flatbuffers::Offset<mz::fb::MetaDataEntry>> SerializeMetaData(flatbuffers::FlatBufferBuilder& fbb);

	virtual ~TreeNode();
};

struct MZSCENETREEMANAGER_API  ActorNode : TreeNode
{
	MZActorReference actor;
	std::vector<TSharedPtr<MZProperty>> Properties;
	std::vector<TSharedPtr<MZFunction>> Functions;
	virtual FString GetClassDisplayName() override { return actor ? actor->GetClass()->GetFName().ToString() : "Actor"; };
	virtual ActorNode* GetAsActorNode() override { return this; };
	virtual flatbuffers::Offset<mz::fb::Node> Serialize(flatbuffers::FlatBufferBuilder& fbb) override;
	std::vector<flatbuffers::Offset<mz::fb::Pin>> SerializePins(flatbuffers::FlatBufferBuilder& fbb);
	
	virtual ~ActorNode();
};

struct MZSCENETREEMANAGER_API  SceneComponentNode : TreeNode
{
	MZComponentReference sceneComponent;
	std::vector<TSharedPtr<MZProperty>> Properties;
	virtual FString GetClassDisplayName() override { return sceneComponent ? sceneComponent->GetClass()->GetFName().ToString() : FString("ActorComponent"); };
	virtual SceneComponentNode* GetAsSceneComponentNode() override { return this; };
	virtual flatbuffers::Offset<mz::fb::Node> Serialize(flatbuffers::FlatBufferBuilder& fbb) override;
	std::vector<flatbuffers::Offset<mz::fb::Pin>> SerializePins(flatbuffers::FlatBufferBuilder& fbb);

	virtual ~SceneComponentNode();
};

struct MZSCENETREEMANAGER_API  FolderNode : TreeNode
{
	virtual FString GetClassDisplayName() override { return FString("Folder"); };
	virtual FolderNode* GetAsFolderNode() override { return this; };

	virtual ~FolderNode();
};

class MZSCENETREEMANAGER_API MZSceneTree
{
public:
	MZSceneTree();

	TSharedPtr<TreeNode> Root;
	bool IsSorted = false;
	TMap<FGuid, TSet<AActor*>> ChildMap;

	TSharedPtr<FolderNode> FindOrAddChildFolder(TSharedPtr<TreeNode> node, FString name, TSharedPtr<TreeNode>& mostRecentParent);
	TSharedPtr<ActorNode> AddActor(FString folderPath, AActor* actor, FGuid ForcedGuid = {});
	TSharedPtr<ActorNode> AddActor(::FString folderPath, ::AActor* actor, TSharedPtr<TreeNode>& mostRecentParent, FGuid ForcedGuid = {});
	TSharedPtr<ActorNode> AddActor(TreeNode* parent, AActor* actor);
	TSharedPtr<SceneComponentNode> AddSceneComponent(ActorNode* parent, USceneComponent* sceneComponent);
	TSharedPtr<SceneComponentNode> AddSceneComponent(TSharedPtr<SceneComponentNode> parent, USceneComponent* sceneComponent);
	ActorNode* GetNode(AActor* Actor);
	ActorNode* GetNodeFromActorId(FGuid ActorId);
	FGuid      GetNodeIdActorId(FGuid ActorId);
	TreeNode* GetNode(FGuid NodeId);
	void RemoveNode(FGuid NodeId);

	void Clear();

private:
	TMap<FGuid, TSharedPtr<TreeNode>> NodeMap;
	TMap<FGuid, FGuid> ActorIdToNodeId;
	void ClearRecursive(TSharedPtr<TreeNode> node);
};
