/*=====================================================================
PlayerPhysicsInput.h
---------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


/*=====================================================================
PlayerPhysicsInput
-------------

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
		SHIFT_down = CTRL_down = A_down = W_down = S_down = D_down = space_down = C_down = left_down = right_down = up_down = down_down = false;
	}


	bool SHIFT_down, CTRL_down, A_down, W_down, S_down, D_down, space_down, C_down, left_down, right_down, up_down, down_down;
};
