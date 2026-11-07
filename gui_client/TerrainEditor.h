/*=====================================================================
TerrainEditor.h
---------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include <opengl/OpenGLEngine.h>
#include <graphics/ImageMap.h>
#include <maths/PCG32.h>
#include <utils/RefCounted.h>
#include <utils/Reference.h>
#include <vector>
class TerrainSystem;
struct TerrainSpec;
class ResourceManager;


enum TerrainMaskMapType
{
	TerrainMaskMapType_Mask,     // RGB(A) mask map: R = rock, G = sediment, B = vegetation
	TerrainMaskMapType_TreeMask  // Single-channel tree mask map: >= 0.5 = trees allowed
};


struct TerrainMaskBrush
{
	TerrainMaskBrush() : map_type(TerrainMaskMapType_Mask), channel(2), target_value(1), radius_m(50), strength(0.5f) {}

	TerrainMaskMapType map_type;
	int channel; // Mask map channel to paint.  Ignored for the tree mask map, which is single-channel.
	float target_value; // Painted values move towards this value, in [0, 1].
	float radius_m;
	float strength; // Rate at which painted values move towards target_value, in [0, 1].
};


/*=====================================================================
TerrainEditor
-------------
Realtime painting of the terrain section mask maps (the RGB mask map and the tree mask map).

Edits are local only.  publishEdits() encodes the edited maps to PNG, copies them into the resource dir,
and sets their URLs in a terrain spec, which can then be sent to the server as a world settings update.

Painting is done into editable copies of the section maps.  The terrain system reads the edited copies,
and the edited mask map is uploaded to its own OpenGL texture, which the terrain chunks use.

An editor is tied to a single TerrainSystem.  If the terrain system is reloaded, the editor
(and any unpublished edits) should be discarded.
=====================================================================*/
class TerrainEditor : public RefCounted
{
public:
	TerrainEditor(OpenGLEngine* opengl_engine, const Reference<TerrainSystem>& terrain_system);
	~TerrainEditor();

	void shutdown(); // Removes the brush cursor from the OpenGL engine.

	TerrainSystem* getTerrainSystem() { return terrain_system.ptr(); }

	// Trace the ray against the terrain heightfield, and show the brush cursor at the hit point if hit.  Returns true if the ray hit the terrain.
	// If paint_mode_active, the cursor is coloured by brush.target_value (green = add, red = remove), otherwise it is blue.
	bool updateCursor(const Vec4f& origin, const Vec4f& unit_dir, const TerrainMaskBrush& brush, bool paint_mode_active);
	void hideCursor();

	void startStroke();
	void think(const TerrainMaskBrush& brush, float dt); // Paints at the cursor position, if a stroke is active and the cursor is on the terrain.
	void finishStroke();
	bool isStrokeActive() const { return stroke_active; }

	bool canUndo() const { return !undo_strokes.empty(); }
	void undoStroke();

	bool haveEdits() const;

	// Write the edited maps as PNGs, copy them into the resource dir, and set their URLs in terrain_spec_in_out.  Throws glare::Exception on failure.
	void publishEdits(ResourceManager& resource_manager, const std::string& temp_dir, TerrainSpec& terrain_spec_in_out);

private:
	GLARE_DISABLE_COPY(TerrainEditor);

	bool isSectionInSpec(int section_index) const;
	bool traceRayAgainstHeightfield(const Vec4f& origin, const Vec4f& unit_dir, float max_t, float& hit_t_out) const;
	void paint(const TerrainMaskBrush& brush, float centre_x, float centre_y, float dt);
	ImageMapUInt8* getOrMakeEditableMap(int section_index, TerrainMaskMapType map_type);
	void mapRegionChanged(int section_index, TerrainMaskMapType map_type, int y0, int y1);
	js::AABBox sectionPixelRegionAABBWS(int section_index, const ImageMapUInt8& map, int x0, int y0, int x1, int y1) const;
	void setMaskTexForSectionChunks(int section_index, const OpenGLTextureRef& old_tex, const OpenGLTextureRef& new_tex);
	void invalidateVegetation(const js::AABBox& aabb_ws);
	void rebuildTerrainMeshes();

	OpenGLEngine* opengl_engine;
	Reference<TerrainSystem> terrain_system;

	struct EditedSection
	{
		// Editable copies of the section maps, made when first painted on.  The terrain section maskmap / treemaskmap refer to these when non-null.
		ImageMapUInt8Ref mask_map;
		ImageMapUInt8Ref tree_mask_map;

		// The maps the terrain section referred to before editing.  Kept alive since MakeTerrainChunkTasks on other threads may still be reading them.
		Map2DRef pre_edit_mask_map;
		Map2DRef pre_edit_tree_mask_map;
	};
	std::vector<EditedSection> edited_sections; // Indexed by terrain data section index.

	GLObjectRef cursor_gl_ob;
	bool cursor_inserted;
	bool cursor_on_terrain;
	Vec4f cursor_pos_ws;
	Colour3f cursor_colour; // Non-linear sRGB colour of the cursor material

	// State for the current stroke, for each map painted so far in the stroke.
	struct StrokeMap
	{
		int section_index;
		TerrainMaskMapType map_type;
		ImageMapUInt8Ref before; // Copy of the map at the start of the stroke.
		int x0, y0, x1, y1; // Painted pixel region (inclusive)
	};
	std::vector<StrokeMap> stroke_maps;
	bool stroke_active;
	bool stroke_painted_vegetation; // Was the vegetation channel of a mask map painted in the current stroke?  It affects the terrain height, so requires a terrain mesh rebuild.

	js::AABBox pending_vegetation_invalidate_aabb; // Region painted that the terrain scattering hasn't been told about yet.
	float time_since_vegetation_invalidate;

	struct UndoRegion
	{
		int section_index;
		TerrainMaskMapType map_type;
		int x0, y0; // Region top-left pixel
		ImageMapUInt8Ref pixels; // Pixel values before the stroke
	};
	struct UndoStroke
	{
		std::vector<UndoRegion> regions;
		bool painted_vegetation;
	};
	std::vector<UndoStroke> undo_strokes;

	PCG32 rng;
};
