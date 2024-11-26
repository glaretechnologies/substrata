/*=====================================================================
PlayerPhysicsInput.h
---------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include <utils/Platform.h>


/*=====================================================================
PlayerPhysicsInput
------------------

=====================================================================*/
class PlayerPhysicsInput
{
public:
	PlayerPhysicsInput()
	{
		clear();
	}

	void clear()
	{
		SHIFT_down = CTRL_down = A_down = W_down = S_down = D_down = space_down = B_down = C_down = left_down = right_down = up_down = down_down = false;
		left_trigger = right_trigger = axis_left_x = axis_left_y = 0;
	}

	uint32 toBitFlags() const
	{
		return 
			((SHIFT_down ? 1 : 0) <<  0) | 
			((CTRL_down  ? 1 : 0) <<  1) | 
			((A_down     ? 1 : 0) <<  2) | 
			((W_down     ? 1 : 0) <<  3) | 
			((S_down     ? 1 : 0) <<  4) | 
			((D_down     ? 1 : 0) <<  5) | 
			((space_down ? 1 : 0) <<  6) | 
			((C_down     ? 1 : 0) <<  7) | 
			((left_down  ? 1 : 0) <<  8) | 
			((right_down ? 1 : 0) <<  9) | 
			((up_down    ? 1 : 0) << 10) |
			((down_down  ? 1 : 0) << 11) | 
			((B_down     ? 1 : 0) << 12);
	}

	void setFromBitFlags(uint32 bitmask)
	{
		SHIFT_down = ((bitmask >>  0) & 0x1) != 0;
		CTRL_down  = ((bitmask >>  1) & 0x1) != 0;
		A_down     = ((bitmask >>  2) & 0x1) != 0;
		W_down     = ((bitmask >>  3) & 0x1) != 0;
		S_down     = ((bitmask >>  4) & 0x1) != 0;
		D_down     = ((bitmask >>  5) & 0x1) != 0;
		space_down = ((bitmask >>  6) & 0x1) != 0;
		C_down     = ((bitmask >>  7) & 0x1) != 0;
		left_down  = ((bitmask >>  8) & 0x1) != 0;
		right_down = ((bitmask >>  9) & 0x1) != 0;
		up_down    = ((bitmask >> 10) & 0x1) != 0;
		down_down  = ((bitmask >> 11) & 0x1) != 0;
		B_down     = ((bitmask >> 12) & 0x1) != 0;
	}


	bool SHIFT_down, CTRL_down, A_down, W_down, S_down, D_down, space_down, C_down, left_down, right_down, up_down, down_down, B_down;
	float left_trigger, right_trigger, axis_left_x, axis_left_y;
};
