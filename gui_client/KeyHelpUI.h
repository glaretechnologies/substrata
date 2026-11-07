/*=====================================================================
KeyHelpUI.h
-----------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include <opengl/ui/GLUI.h>
#include <opengl/ui/GLUIGridContainer.h>
#include <utils/ThreadSafeRefCounted.h>
#include <utils/Reference.h>
#include <string>
#include <vector>


struct KeyHelpEntry
{
	KeyHelpEntry() {}
	KeyHelpEntry(const std::string& keys_, const std::string& action_) : keys(keys_), action(action_) {}

	std::string keys; // e.g. "Ctrl + Left drag"
	std::string action; // e.g. "Paint"
};


/*=====================================================================
KeyHelpUI
---------
On-screen help text listing keys / mouse controls and the actions they do.

Help is grouped into sections, each with an id.  Code that wants to show help
for some mode (e.g. terrain painting) calls setHelp() with its own id when the mode is entered,
and removeHelp() with the same id when the mode is left.  All current sections are shown.
=====================================================================*/
class KeyHelpUI : public ThreadSafeRefCounted
{
public:
	KeyHelpUI(GLUIRef gl_ui_);
	~KeyHelpUI();

	// Adds the section with the given id, or replaces it if it already exists.
	void setHelp(const std::string& id, const std::string& title, const std::vector<KeyHelpEntry>& entries);
	void removeHelp(const std::string& id);

	void setVisible(bool visible);

	void viewportResized(int w, int h);

private:
	void rebuild();
	void updateWidgetPositions();

	struct HelpSection
	{
		std::string id;
		std::string title;
		std::vector<KeyHelpEntry> entries;
	};
	std::vector<HelpSection> sections;

	GLUIRef gl_ui;
	GLUIGridContainerRef grid_container;
	bool visible;
};


typedef Reference<KeyHelpUI> KeyHelpUIRef;
