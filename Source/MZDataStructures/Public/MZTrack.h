#pragma once
//#if WITH_EDITORONLY_DATA
#include "MZTrack.generated.h"
/** Track data used for connecting with mediaZ */
USTRUCT(BlueprintType)
struct MZDATASTRUCTURES_API FMZTrack 
{
	GENERATED_BODY()
public:
	/** Please add a variable description */
	UPROPERTY(EditAnywhere, meta = (DisplayName = "Location", Category = "", MakeStructureDefaultValue = "0.000000,0.000000,0.000000"))
		FVector location = FVector::ZeroVector;

	/** Please add a variable description */
	UPROPERTY(EditAnywhere, meta = (DisplayName = "Rotation", Category = "", MakeStructureDefaultValue = "0.000000,0.000000,0.000000"))
		FVector rotation = FVector::ZeroVector;

	/** Please add a variable description */
	UPROPERTY(EditAnywhere, meta = (DisplayName = "FOV", Category = "", MakeStructureDefaultValue = "60.000000"))
		double fov = 60.0f;

	/** Please add a variable description */
	UPROPERTY(EditAnywhere, meta = (DisplayName = "Focus Distance", Category = "", MakeStructureDefaultValue = "0.000000"))
		double focus_distance = 0.0f;

	/** Please add a variable description */
	UPROPERTY(EditAnywhere, meta = (DisplayName = "Center Shift", Category = "", MakeStructureDefaultValue = "(X=0.000000,Y=0.000000)"))
		FVector2D center_shift = FVector2D::ZeroVector;

	/** Please add a variable description */
	UPROPERTY(EditAnywhere, meta = (DisplayName = "k1", Category = "", MakeStructureDefaultValue = "0.000000"))
		double zoom = 0.0f;

	/** Please add a variable description */
	UPROPERTY(EditAnywhere, meta = (DisplayName = "k1", Category = "", MakeStructureDefaultValue = "0.000000"))
		float k1 = 0.0f;

	/** Please add a variable description */
	UPROPERTY(EditAnywhere, meta = (DisplayName = "k2", Category = "", MakeStructureDefaultValue = "0.000000"))
		float k2 = 0.0f;

	/** Please add a variable description */
	UPROPERTY(EditAnywhere, meta = (DisplayName = "Render Ratio", Category = "", MakeStructureDefaultValue = "1.000000"))
		double render_ratio = 1.0f;

	/** Please add a variable description */
	UPROPERTY(EditAnywhere, meta = (DisplayName = "Distortion Scale", Category = "", MakeStructureDefaultValue = "0.000000"))
		float distortion_scale = 1.0f;

	/** Please add a variable description */
	UPROPERTY(EditAnywhere, meta = (DisplayName = "Sensor Size", Category = "", MakeStructureDefaultValue = "(X=0.000000,Y=0.000000)"))
		FVector2D sensor_size = FVector2D(9.590f, 5.394f);

	/** Please add a variable description */
	UPROPERTY(EditAnywhere, meta = (DisplayName = "pixel_aspect_ratio", Category = "", MakeStructureDefaultValue = "1.000000"))
		double pixel_aspect_ratio = 1.0f;

	/** Please add a variable description */
	UPROPERTY(EditAnywhere, meta = (DisplayName = "nodal_offset", Category = "", MakeStructureDefaultValue = "0.000000"))
		double nodal_offset = 0.0f;
};
