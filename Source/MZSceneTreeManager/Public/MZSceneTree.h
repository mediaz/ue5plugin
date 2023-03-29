#pragma once
#include "CoreMinimal.h"
#include "MZContainer.h"
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
	TreeNode(TreeNode* parent) : Parent(parent) {}
	virtual ActorNode* GetAsActorNode() { return nullptr; };
	virtual FolderNode* GetAsFolderNode() { return nullptr; };
	virtual SceneComponentNode* GetAsSceneComponentNode() { return nullptr; };
	virtual FString GetClassDisplayName() = 0;
	virtual void PopulateNode() {}
	FString Name;
	TreeNode* Parent;
	FGuid Id;
	bool NeedsReload = true;
	std::vector<TUniquePtr<TreeNode>> Children;
	TMap<FString, FString> mzMetaData;

	
	virtual flatbuffers::Offset<mz::fb::Node> Serialize(flatbuffers::FlatBufferBuilder& fbb);
	std::vector<flatbuffers::Offset<mz::fb::Node>> SerializeChildren(flatbuffers::FlatBufferBuilder& fbb);

	virtual ~TreeNode() = default;
};

struct MZSCENETREEMANAGER_API  ActorNode : TreeNode
{
	ActorNode(TObjectPtr<AActor> actor, TreeNode* parent) : TreeNode(parent), ActorRef(actor) {}
	MZActorRef ActorRef;
	std::vector<TUniquePtr<MZFProperty>> Properties;
	std::vector<TUniquePtr<MZFunction>> Functions;
	virtual FString GetClassDisplayName() override 
	{
		return ActorRef.IsValid() ? ActorRef->GetClass()->GetFName().ToString() : "Actor"; 
	}
	virtual ActorNode* GetAsActorNode() override { return this; }
	virtual flatbuffers::Offset<mz::fb::Node> Serialize(flatbuffers::FlatBufferBuilder& fbb) override;
	std::vector<flatbuffers::Offset<mz::fb::Pin>> SerializePins(flatbuffers::FlatBufferBuilder& fbb);
	void PopulateNode() override;
};

struct MZSCENETREEMANAGER_API  SceneComponentNode : TreeNode
{
	SceneComponentNode(ActorNode* parent, UActorComponent* actorComponent) 
		: TreeNode(parent), SceneComponentRef(actorComponent, &parent->ActorRef) {}
	SceneComponentNode(SceneComponentNode* parent, UActorComponent* actorComponent) 
		: TreeNode(parent), SceneComponentRef(actorComponent, &parent->SceneComponentRef) {}
	MZComponentRef SceneComponentRef;
	std::vector<TUniquePtr<MZProperty>> Properties;
	virtual FString GetClassDisplayName() override 
	{ 
		return SceneComponentRef ? SceneComponentRef->GetClass()->GetFName().ToString() : FString("ActorComponent"); 
	}
	virtual SceneComponentNode* GetAsSceneComponentNode() override { return this; };
	virtual flatbuffers::Offset<mz::fb::Node> Serialize(flatbuffers::FlatBufferBuilder& fbb) override;
	std::vector<flatbuffers::Offset<mz::fb::Pin>> SerializePins(flatbuffers::FlatBufferBuilder& fbb);

	void PopulateNode() override;
};

struct MZSCENETREEMANAGER_API  FolderNode : TreeNode
{
	FolderNode(TreeNode* parent) : TreeNode(parent) {}
	virtual FString GetClassDisplayName() override { return FString("Folder"); };
	virtual FolderNode* GetAsFolderNode() override { return this; };

};

class MZSCENETREEMANAGER_API MZSceneTree 
{
public:
	MZSceneTree();

	static MZSceneTree& GetInstance();

	TUniquePtr<TreeNode> Root;
	bool IsSorted = false;
	TMap<FGuid, TreeNode*> NodeMap;
	TMap<FGuid, TSet<AActor*>> ChildMap;

	FolderNode* FindOrAddChildFolder(TreeNode* node, FString name, TreeNode* mostRecentParent);
	ActorNode* AddActor(FString folderPath, AActor* actor);
	ActorNode* AddActor(FString folderPath, AActor* actor, TreeNode* mostRecentParent);
	ActorNode* AddActor(TreeNode* parent, AActor* actor);
	SceneComponentNode* AddSceneComponent(ActorNode* parent, USceneComponent* sceneComponent);
	SceneComponentNode* AddSceneComponent(SceneComponentNode* parent, USceneComponent* sceneComponent);
	//FolderNode* AddFolder(FString fullFolderPath);
	static bool PropertyVisible(FProperty* ueproperty);

	void Clear();
private:
	void ClearRecursive(TreeNode* node);
};