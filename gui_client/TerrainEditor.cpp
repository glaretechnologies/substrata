/*=====================================================================
TerrainEditor.cpp
-----------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "TerrainEditor.h"


#include "TerrainSystem.h"
#include "../shared/WorldSettings.h"
#include "../shared/ResourceManager.h"
#include <graphics/PNGDecoder.h>
#include <graphics/SRGBUtils.h>
#include <opengl/MeshPrimitiveBuilding.h>
#include <maths/mathstypes.h>
#include <utils/StringUtils.h>
#include <utils/Exception.h>
#include <cstring>


static const int MAX_NUM_UNDO_STROKES = 32;
static const int NEW_MASK_MAP_RES = 1024; // Resolution of mask maps made for sections that don't have one yet.  Matches the resolution assumed by the terrain shaders.
static const float VEGETATION_INVALIDATE_PERIOD = 0.25f; // Minimum time between telling terrain scattering about painted regions during a stroke, since rebuilding scattering is not free.


TerrainEditor::TerrainEditor(OpenGLEngine* opengl_engine_, const Reference<TerrainSystem>& terrain_system_)
:	opengl_engine(opengl_engine_),
	terrain_system(terrain_system_),
	cursor_inserted(false),
	cursor_on_terrain(false),
	cursor_pos_ws(0, 0, 0, 1),
	stroke_active(false),
	stroke_painted_vegetation(false),
	pending_vegetation_invalidate_aabb(js::AABBox::emptyAABBox()),
	time_since_vegetation_invalidate(0),
	rng(1)
{
	edited_sections.resize(TerrainSystem::TERRAIN_DATA_SECTION_RES * TerrainSystem::TERRAIN_DATA_SECTION_RES);

	// Brush cursor: a translucent open cylinder, which shows the brush circle where it intersects the terrain.
	cursor_gl_ob = opengl_engine->allocateObject();
	cursor_gl_ob->ob_to_world_matrix = Matrix4f::identity();
	cursor_gl_ob->mesh_data = MeshPrimitiveBuilding::makeCylinderMesh(*opengl_engine->vert_buf_allocator, /*end_caps=*/false);

	cursor_colour = Colour3f(0.3f, 0.8f, 1.0f);

	OpenGLMaterial material;
	material.albedo_linear_rgb = toLinearSRGB(cursor_colour);
	material.transparent = true;
	material.alpha = 0.4f;
	material.simple_double_sided = true;
	cursor_gl_ob->setSingleMaterial(material);
}


TerrainEditor::~TerrainEditor()
{
}


void TerrainEditor::shutdown()
{
	hideCursor();
	cursor_gl_ob = NULL;
}


static void sectionCoordsForIndex(int section_index, int& section_x_out, int& section_y_out)
{
	section_x_out = section_index % TerrainSystem::TERRAIN_DATA_SECTION_RES - TerrainSystem::TERRAIN_SECTION_OFFSET;
	section_y_out = section_index / TerrainSystem::TERRAIN_DATA_SECTION_RES - TerrainSystem::TERRAIN_SECTION_OFFSET;
}


bool TerrainEditor::isSectionInSpec(int section_index) const
{
	int section_x, section_y;
	sectionCoordsForIndex(section_index, section_x, section_y);

	for(size_t i=0; i<terrain_system->spec.section_specs.size(); ++i)
		if(terrain_system->spec.section_specs[i].x == section_x && terrain_system->spec.section_specs[i].y == section_y)
			return true;
	return false;
}


bool TerrainEditor::traceRayAgainstHeightfield(const Vec4f& origin, const Vec4f& unit_dir, float max_t, float& hit_t_out) const
{
	// March along the ray with a step size that grows with distance, then refine the hit with bisection.
	float prev_t = 0;
	float t = 0;
	while(t < max_t)
	{
		t = myMin(max_t, t + 0.5f + t * 0.01f);
		const Vec4f p = origin + unit_dir * t;
		if(p[2] < terrain_system->evalTerrainHeight(p[0], p[1], /*quad_w=*/1.f))
		{
			float lo = prev_t;
			float hi = t;
			for(int i=0; i<16; ++i)
			{
				const float mid = 0.5f * (lo + hi);
				const Vec4f mid_p = origin + unit_dir * mid;
				if(mid_p[2] < terrain_system->evalTerrainHeight(mid_p[0], mid_p[1], /*quad_w=*/1.f))
					hi = mid;
				else
					lo = mid;
			}
			hit_t_out = hi;
			return true;
		}
		prev_t = t;
	}
	return false;
}


bool TerrainEditor::updateCursor(const Vec4f& origin, const Vec4f& unit_dir, const TerrainMaskBrush& brush, bool paint_mode_active)
{
	float hit_t;
	if(!traceRayAgainstHeightfield(origin, unit_dir, /*max_t=*/10000.f, hit_t))
	{
		hideCursor();
		return false;
	}

	cursor_on_terrain = true;
	cursor_pos_ws = origin + unit_dir * hit_t;

	if(cursor_gl_ob)
	{
		// Colour the cursor by paint mode: green when adding, red when removing, blue when not painting.
		const Colour3f colour = !paint_mode_active ? Colour3f(0.3f, 0.8f, 1.0f) : ((brush.target_value >= 0.5f) ? Colour3f(0.1f, 0.9f, 0.2f) : Colour3f(0.9f, 0.1f, 0.1f));
		const bool colour_changed = colour != cursor_colour;
		if(colour_changed)
		{
			cursor_colour = colour;
			cursor_gl_ob->materials[0].albedo_linear_rgb = toLinearSRGB(colour);
		}

		const float r = brush.radius_m;
		const float h = r * 0.5f + 2.f;
		cursor_gl_ob->ob_to_world_matrix = Matrix4f::translationMatrix(cursor_pos_ws[0], cursor_pos_ws[1], cursor_pos_ws[2] - h * 0.5f) * Matrix4f::scaleMatrix(r, r, h);
		if(cursor_inserted)
		{
			opengl_engine->updateObjectTransformData(*cursor_gl_ob);
			if(colour_changed)
				opengl_engine->objectMaterialsUpdated(*cursor_gl_ob);
		}
		else
		{
			opengl_engine->addObject(cursor_gl_ob);
			cursor_inserted = true;
		}
	}
	return true;
}


void TerrainEditor::hideCursor()
{
	if(cursor_inserted && cursor_gl_ob)
		opengl_engine->removeObject(cursor_gl_ob);
	cursor_inserted = false;
	cursor_on_terrain = false;
}


void TerrainEditor::startStroke()
{
	if(stroke_active)
		finishStroke();

	stroke_active = true;
	stroke_painted_vegetation = false;
	stroke_maps.clear();
}


void TerrainEditor::think(const TerrainMaskBrush& brush, float dt)
{
	if(stroke_active && cursor_on_terrain)
		paint(brush, cursor_pos_ws[0], cursor_pos_ws[1], dt);

	time_since_vegetation_invalidate += dt;
	if(time_since_vegetation_invalidate >= VEGETATION_INVALIDATE_PERIOD && !pending_vegetation_invalidate_aabb.isEmpty())
		invalidateVegetation(pending_vegetation_invalidate_aabb);
}


static ImageMapUInt8Ref copyMap(const ImageMapUInt8& map)
{
	ImageMapUInt8Ref copy = new ImageMapUInt8(map.getWidth(), map.getHeight(), map.getN());
	std::memcpy(copy->getData(), map.getData(), map.getDataSizeB());
	return copy;
}


void TerrainEditor::finishStroke()
{
	if(!stroke_active)
		return;
	stroke_active = false;

	if(stroke_maps.empty())
		return;

	// Store the pre-stroke pixel values of the painted regions, for undo.
	UndoStroke undo_stroke;
	undo_stroke.painted_vegetation = stroke_painted_vegetation;
	for(size_t i=0; i<stroke_maps.size(); ++i)
	{
		const StrokeMap& stroke_map = stroke_maps[i];
		const ImageMapUInt8& before = *stroke_map.before;
		const size_t N = before.getN();
		const size_t region_w = stroke_map.x1 - stroke_map.x0 + 1;
		const size_t region_h = stroke_map.y1 - stroke_map.y0 + 1;

		UndoRegion region;
		region.section_index = stroke_map.section_index;
		region.map_type = stroke_map.map_type;
		region.x0 = stroke_map.x0;
		region.y0 = stroke_map.y0;
		region.pixels = new ImageMapUInt8(region_w, region_h, N);
		for(size_t y=0; y<region_h; ++y)
			std::memcpy(region.pixels->getPixel(0, y), before.getPixel(stroke_map.x0, stroke_map.y0 + y), region_w * N);

		undo_stroke.regions.push_back(region);
	}
	stroke_maps.clear();

	undo_strokes.push_back(undo_stroke);
	if((int)undo_strokes.size() > MAX_NUM_UNDO_STROKES)
		undo_strokes.erase(undo_strokes.begin());

	if(!pending_vegetation_invalidate_aabb.isEmpty())
		invalidateVegetation(pending_vegetation_invalidate_aabb);

	if(stroke_painted_vegetation)
		rebuildTerrainMeshes();
}


void TerrainEditor::undoStroke()
{
	if(stroke_active)
		finishStroke();

	if(undo_strokes.empty())
		return;

	const UndoStroke& undo_stroke = undo_strokes.back();
	for(size_t i=0; i<undo_stroke.regions.size(); ++i)
	{
		const UndoRegion& region = undo_stroke.regions[i];
		EditedSection& edited = edited_sections[region.section_index];
		ImageMapUInt8* map = (region.map_type == TerrainMaskMapType_Mask) ? edited.mask_map.ptr() : edited.tree_mask_map.ptr();
		if(!map)
			continue;

		const ImageMapUInt8& pixels = *region.pixels;
		const size_t N = map->getN();
		for(size_t y=0; y<pixels.getHeight(); ++y)
			std::memcpy(map->getPixel(region.x0, region.y0 + y), pixels.getPixel(0, y), pixels.getWidth() * N);

		const int x1 = region.x0 + (int)pixels.getWidth()  - 1;
		const int y1 = region.y0 + (int)pixels.getHeight() - 1;
		mapRegionChanged(region.section_index, region.map_type, region.y0, y1);
		pending_vegetation_invalidate_aabb.enlargeToHoldAABBox(sectionPixelRegionAABBWS(region.section_index, *map, region.x0, region.y0, x1, y1));
	}

	if(!pending_vegetation_invalidate_aabb.isEmpty())
		invalidateVegetation(pending_vegetation_invalidate_aabb);

	if(undo_stroke.painted_vegetation)
		rebuildTerrainMeshes();

	undo_strokes.pop_back();
}


bool TerrainEditor::haveEdits() const
{
	for(size_t i=0; i<edited_sections.size(); ++i)
		if(edited_sections[i].mask_map || edited_sections[i].tree_mask_map)
			return true;
	return false;
}


void TerrainEditor::publishEdits(ResourceManager& resource_manager, const std::string& temp_dir, TerrainSpec& terrain_spec_in_out)
{
	for(size_t i=0; i<edited_sections.size(); ++i)
	{
		const EditedSection& edited = edited_sections[i];
		if(!edited.mask_map && !edited.tree_mask_map)
			continue;

		int section_x, section_y;
		sectionCoordsForIndex((int)i, section_x, section_y);

		TerrainSpecSection* section_spec = NULL;
		for(size_t z=0; z<terrain_spec_in_out.section_specs.size(); ++z)
			if(terrain_spec_in_out.section_specs[z].x == section_x && terrain_spec_in_out.section_specs[z].y == section_y)
				section_spec = &terrain_spec_in_out.section_specs[z];
		if(!section_spec)
			throw glare::Exception("Terrain section (" + toString(section_x) + ", " + toString(section_y) + ") is no longer in the world settings.");

		const std::string coords_suffix = toString(section_x) + "_" + toString(section_y) + ".png";
		if(edited.mask_map)
		{
			const std::string path = temp_dir + "/terrain_mask_map_" + coords_suffix;
			PNGDecoder::write(*edited.mask_map, path);
			section_spec->mask_map_URL = resource_manager.copyLocalFileToResourceDirAndReturnURL(path);
		}
		if(edited.tree_mask_map)
		{
			const std::string path = temp_dir + "/terrain_tree_mask_map_" + coords_suffix;
			PNGDecoder::write(*edited.tree_mask_map, path);
			section_spec->tree_mask_map_URL = resource_manager.copyLocalFileToResourceDirAndReturnURL(path);
		}
	}
}


// Make an 8-bit copy of src with num_channels channels.
static ImageMapUInt8Ref makeEditableCopy(const Map2D& src, size_t num_channels)
{
	const ImageMapUInt8* src_uint8 = dynamic_cast<const ImageMapUInt8*>(&src);
	if(src_uint8 && src_uint8->getN() == num_channels)
		return copyMap(*src_uint8);

	const size_t W = src.getMapWidth();
	const size_t H = src.getMapHeight();
	ImageMapUInt8Ref map = new ImageMapUInt8(W, H, num_channels);
	for(size_t y=0; y<H; ++y)
	for(size_t x=0; x<W; ++x)
	{
		const Colour4f col = src.pixelColour(x, y);
		uint8* pixel = map->getPixel(x, y);
		for(size_t c=0; c<num_channels; ++c)
			pixel[c] = (c < 3) ? (uint8)myClamp((int)(col[c] * 255.f + 0.5f), 0, 255) : 0;
	}
	return map;
}


ImageMapUInt8* TerrainEditor::getOrMakeEditableMap(int section_index, TerrainMaskMapType map_type)
{
	TerrainDataSection& section = terrain_system->terrain_data_sections[section_index];
	EditedSection& edited = edited_sections[section_index];

	if(map_type == TerrainMaskMapType_Mask)
	{
		if(edited.mask_map)
			return edited.mask_map.ptr();

		if(!section.mask_map_path.empty() && section.maskmap.isNull()) // If the section has a mask map that hasn't loaded yet, don't paint on it.
			return NULL;

		ImageMapUInt8Ref map;
		if(section.maskmap)
			map = makeEditableCopy(*section.maskmap, /*num_channels=*/(section.maskmap->numChannels() >= 4) ? 4 : 3);
		else
		{
			// Same value as the default mask texture used for sections without a mask map.
			map = new ImageMapUInt8(NEW_MASK_MAP_RES, NEW_MASK_MAP_RES, 3);
			for(size_t i=0; i<map->numPixels(); ++i)
			{
				map->getPixel(i)[0] = 255;
				map->getPixel(i)[1] = 0;
				map->getPixel(i)[2] = 0;
			}
		}

		edited.pre_edit_mask_map = section.maskmap;
		edited.mask_map = map;
		section.maskmap = map;

		// Make a texture for the edited map, rather than updating the existing texture, which may be shared (e.g. the default mask texture).
		OpenGLTextureRef new_tex = new OpenGLTexture(map->getWidth(), map->getHeight(), opengl_engine, ArrayRef<uint8>(map->getData(), map->getDataSize()),
			(map->getN() == 4) ? Format_RGBA_Linear_Uint8 : Format_RGB_Linear_Uint8, OpenGLTexture::Filtering_Fancy, OpenGLTexture::Wrapping_Clamp, /*has_mipmaps=*/true);
		new_tex->buildMipMaps();

		const OpenGLTextureRef old_tex = section.mask_gl_tex;
		section.mask_gl_tex = new_tex;
		setMaskTexForSectionChunks(section_index, old_tex, new_tex);

		return map.ptr();
	}
	else
	{
		if(edited.tree_mask_map)
			return edited.tree_mask_map.ptr();

		if(!section.tree_mask_map_path.empty() && section.treemaskmap.isNull())
			return NULL;

		ImageMapUInt8Ref map;
		if(section.treemaskmap)
		{
			const ImageMapUInt8* src_uint8 = dynamic_cast<const ImageMapUInt8*>(section.treemaskmap.ptr());
			map = makeEditableCopy(*section.treemaskmap, /*num_channels=*/src_uint8 ? src_uint8->getN() : 1);
		}
		else
		{
			// Trees are allowed everywhere if there is no tree mask map.
			map = new ImageMapUInt8(NEW_MASK_MAP_RES, NEW_MASK_MAP_RES, 1);
			std::memset(map->getData(), 255, map->getDataSizeB());
		}

		edited.pre_edit_tree_mask_map = section.treemaskmap;
		edited.tree_mask_map = map;
		section.treemaskmap = map;

		return map.ptr();
	}
}


void TerrainEditor::paint(const TerrainMaskBrush& brush, float centre_x, float centre_y, float dt)
{
	const float section_w = terrain_system->terrain_section_w;
	const float r = myMax(0.01f, brush.radius_m);

	// Find the range of terrain sections the brush touches.
	const int begin_section_x = Maths::floorToInt((centre_x - r) / section_w + 0.5f);
	const int end_section_x   = Maths::floorToInt((centre_x + r) / section_w + 0.5f);
	const int begin_section_y = Maths::floorToInt((centre_y - r) / section_w + 0.5f);
	const int end_section_y   = Maths::floorToInt((centre_y + r) / section_w + 0.5f);

	const float alpha_max = myClamp(brush.strength * 8.f * dt, 0.f, 1.f);
	const float target = myClamp(brush.target_value, 0.f, 1.f) * 255.f;

	for(int section_y=begin_section_y; section_y<=end_section_y; ++section_y)
	for(int section_x=begin_section_x; section_x<=end_section_x; ++section_x)
	{
		const int index_x = section_x + TerrainSystem::TERRAIN_SECTION_OFFSET;
		const int index_y = section_y + TerrainSystem::TERRAIN_SECTION_OFFSET;
		if(index_x < 0 || index_x >= TerrainSystem::TERRAIN_DATA_SECTION_RES || index_y < 0 || index_y >= TerrainSystem::TERRAIN_DATA_SECTION_RES)
			continue;
		const int section_index = index_x + index_y * TerrainSystem::TERRAIN_DATA_SECTION_RES;
		if(!isSectionInSpec(section_index))
			continue;

		ImageMapUInt8* map = getOrMakeEditableMap(section_index, brush.map_type);
		if(!map)
			continue;

		const int W = (int)map->getWidth();
		const int H = (int)map->getHeight();
		const int N = (int)map->getN();
		const int channel = (brush.map_type == TerrainMaskMapType_Mask) ? brush.channel : 0;
		if(channel < 0 || channel >= N)
			continue;

		// The ground types (R = rock, G = sediment, B = vegetation) are mutually exclusive: adding one removes the others at the same rate.
		const bool remove_other_ground_types = (brush.map_type == TerrainMaskMapType_Mask) && (channel <= 2) && (brush.target_value >= 0.5f);

		// Pixel (x, y) is at section-normalised coordinates (x / W, y / H).  See TerrainSystem::evalTerrainMask().
		const float px_per_m_x = W / section_w;
		const float px_per_m_y = H / section_w;
		const float centre_px = (centre_x / section_w + 0.5f - section_x) * W;
		const float centre_py = (centre_y / section_w + 0.5f - section_y) * H;

		const int x0 = myMax(0,     Maths::floorToInt(centre_px - r * px_per_m_x));
		const int x1 = myMin(W - 1, Maths::floorToInt(centre_px + r * px_per_m_x) + 1);
		const int y0 = myMax(0,     Maths::floorToInt(centre_py - r * px_per_m_y));
		const int y1 = myMin(H - 1, Maths::floorToInt(centre_py + r * px_per_m_y) + 1);
		if(x0 > x1 || y0 > y1)
			continue;

		// Get the state for this map in the current stroke, snapshotting the map if this is the first time the stroke has painted on it.
		StrokeMap* stroke_map = NULL;
		for(size_t i=0; i<stroke_maps.size(); ++i)
			if(stroke_maps[i].section_index == section_index && stroke_maps[i].map_type == brush.map_type)
				stroke_map = &stroke_maps[i];
		if(!stroke_map)
		{
			stroke_maps.push_back(StrokeMap());
			stroke_map = &stroke_maps.back();
			stroke_map->section_index = section_index;
			stroke_map->map_type = brush.map_type;
			stroke_map->before = copyMap(*map);
			stroke_map->x0 = x0;
			stroke_map->y0 = y0;
			stroke_map->x1 = x1;
			stroke_map->y1 = y1;
		}
		else
		{
			stroke_map->x0 = myMin(stroke_map->x0, x0);
			stroke_map->y0 = myMin(stroke_map->y0, y0);
			stroke_map->x1 = myMax(stroke_map->x1, x1);
			stroke_map->y1 = myMax(stroke_map->y1, y1);
		}

		for(int y=y0; y<=y1; ++y)
		for(int x=x0; x<=x1; ++x)
		{
			const float dx = (x - centre_px) / px_per_m_x;
			const float dy = (y - centre_py) / px_per_m_y;
			const float d = std::sqrt(dx*dx + dy*dy);
			if(d >= r)
				continue;

			const float alpha = alpha_max * (1.f - Maths::smoothStep(0.f, 1.f, d / r));

			uint8* const pixel = map->getPixel(x, y);
			const float new_v = pixel[channel] + (target - pixel[channel]) * alpha;
			pixel[channel] = (uint8)myClamp(Maths::floorToInt(new_v + rng.unitRandom()), 0, 255); // Stochastic rounding, so that small changes per frame still accumulate.

			if(remove_other_ground_types)
				for(int c=0; c<3; ++c)
					if(c != channel)
						pixel[c] = (uint8)myClamp(Maths::floorToInt(pixel[c] * (1.f - alpha) + rng.unitRandom()), 0, 255);
		}

		mapRegionChanged(section_index, brush.map_type, y0, y1);
		pending_vegetation_invalidate_aabb.enlargeToHoldAABBox(sectionPixelRegionAABBWS(section_index, *map, x0, y0, x1, y1));

		if(brush.map_type == TerrainMaskMapType_Mask && (channel == 2 || remove_other_ground_types)) // The vegetation channel was changed.
			stroke_painted_vegetation = true;
	}
}


void TerrainEditor::mapRegionChanged(int section_index, TerrainMaskMapType map_type, int y0, int y1)
{
	if(map_type != TerrainMaskMapType_Mask) // Tree mask maps are only used on the CPU.
		return;

	const ImageMapUInt8* map = edited_sections[section_index].mask_map.ptr();
	OpenGLTexture* tex = terrain_system->terrain_data_sections[section_index].mask_gl_tex.ptr();
	if(!map || !tex)
		return;

	// Upload whole rows, so the source data is contiguous.
	const size_t row_stride_B = map->getWidth() * map->getN();
	const size_t region_h = y1 - y0 + 1;
	tex->loadRegionIntoExistingTexture(/*mipmap level=*/0, /*x=*/0, /*y=*/y0, /*z=*/0, /*region_w=*/map->getWidth(), region_h, /*region_d=*/1, row_stride_B,
		ArrayRef<uint8>(map->getPixel(0, y0), row_stride_B * region_h), /*bind_needed=*/true);
	tex->buildMipMaps();
}


js::AABBox TerrainEditor::sectionPixelRegionAABBWS(int section_index, const ImageMapUInt8& map, int x0, int y0, int x1, int y1) const
{
	int section_x, section_y;
	sectionCoordsForIndex(section_index, section_x, section_y);

	const float section_w = terrain_system->terrain_section_w;
	const float W = (float)map.getWidth();
	const float H = (float)map.getHeight();
	return js::AABBox(
		Vec4f((section_x + x0       / W - 0.5f) * section_w, (section_y + y0       / H - 0.5f) * section_w, -1, 1),
		Vec4f((section_x + (x1 + 1) / W - 0.5f) * section_w, (section_y + (y1 + 1) / H - 0.5f) * section_w,  1, 1)
	);
}


void TerrainEditor::setMaskTexForSectionChunks(int section_index, const OpenGLTextureRef& old_tex, const OpenGLTextureRef& new_tex)
{
	const float section_w = terrain_system->terrain_section_w;

	auto updateOb = [&](GLObject* ob)
	{
		if(!ob || ob->materials.empty() || ob->materials[0].albedo_texture != old_tex || !ob->mesh_data)
			return;

		// Work out which section the chunk is in from the middle of the chunk, as in TerrainSystem::handleCompletedMakeChunkTask().
		// Needed since the old texture may be the default mask texture, which is shared by all sections without a mask map.
		const Vec4f chunk_middle = ob->ob_to_world_matrix.getColumn(3) + (ob->mesh_data->aabb_os.min_ + ob->mesh_data->aabb_os.max_) * 0.5f;
		const int index_x = Maths::floorToInt(chunk_middle[0] / section_w + 0.5f) + TerrainSystem::TERRAIN_SECTION_OFFSET;
		const int index_y = Maths::floorToInt(chunk_middle[1] / section_w + 0.5f) + TerrainSystem::TERRAIN_SECTION_OFFSET;
		if(index_x + index_y * TerrainSystem::TERRAIN_DATA_SECTION_RES != section_index)
			return;

		ob->materials[0].albedo_texture = new_tex;
		opengl_engine->objectMaterialsUpdated(*ob);
	};

	for(auto it = terrain_system->id_to_node_map.begin(); it != terrain_system->id_to_node_map.end(); ++it)
	{
		TerrainNode* node = it->second;
		updateOb(node->gl_ob.ptr());
		updateOb(node->pending_gl_ob.ptr());
		for(size_t i=0; i<node->old_subtree_gl_obs.size(); ++i)
			updateOb(node->old_subtree_gl_obs[i].ptr());
	}
}


void TerrainEditor::invalidateVegetation(const js::AABBox& aabb_ws)
{
	terrain_system->invalidateVegetationMap(aabb_ws);

	pending_vegetation_invalidate_aabb = js::AABBox::emptyAABBox();
	time_since_vegetation_invalidate = 0;
}


void TerrainEditor::rebuildTerrainMeshes()
{
	// As in TerrainSystem::handleTextureLoaded(): the existing chunk meshes stay in the scene until the rebuilt subtree is complete.
	TerrainNode* root = terrain_system->root_node.ptr();
	if(root)
		terrain_system->removeSubtree(root, root->old_subtree_gl_obs, root->old_subtree_phys_obs);
}
