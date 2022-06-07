/*=====================================================================
UndoBuffer.h
-------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include "../shared/Avatar.h"
#include "../shared/WorldObject.h"
#include "../shared/Parcel.h"
#include <ThreadSafeRefCounted.h>
#include <Mutex.h>
#include <map>
#include <unordered_set>


/*=====================================================================
UndoBuffer
----------


  start            end   start            end  start             end
|       edit 0          |       edit 1       |        edit 2         |
-------------------------------------------------------------------
                                             ^
                                          index (=2)

If user selects undo, then edit 1 will be undone, with the state being restored to edit 1 start (= edit 0 end in this case)
If user selects redo, then edit 2 will be reapplied, with the state being set to edit 2 end.
=====================================================================*/
class UndoBufferEdit
{
public:
	// State at start and end of edit:
	std::vector<unsigned char> start;
	std::vector<unsigned char> end;
};


class UndoBuffer
{
public:
	UndoBuffer();
	~UndoBuffer();

	void startWorldObjectEdit(const WorldObject& ob);
	void finishWorldObjectEdit(const WorldObject& ob);

	void replaceFinishWorldObjectEdit(const WorldObject& ob);

	WorldObjectRef getUndoWorldObject();
	WorldObjectRef getRedoWorldObject();

private:
	UndoBufferEdit current_edit;

	std::vector<UndoBufferEdit> chunks;
	int index; // Chunks < index will be restored on Undo, chunks >= index will be restored on redo.
};
