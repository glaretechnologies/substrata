/*=====================================================================
VehiclesShared.h
----------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include <utils/string_view.h>


/*=====================================================================
VehiclesShared
--------------
=====================================================================*/
class VehiclesShared
{
public:
	static string_view bikeModelURL()     { return "optimized_dressed_fix7_offset4_glb_4474648345850208925.bmesh"; }
	static string_view hovercarModelURL() { return "peugot_closed_glb_2887717763908023194.bmesh"; }
	static string_view boatModelURL()     { return "poweryacht3_2_glb_17116251394697619807.bmesh"; }
	static string_view carModelURL()      { return "deLorean2_0_glb_5923323464955550713.bmesh"; }
	static string_view jetSkiModelURL()   { return "Jet_Ski_obj_3200017390617214853.bmesh"; }
};
