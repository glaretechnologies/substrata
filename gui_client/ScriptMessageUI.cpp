/*=====================================================================
ScriptMessageUI.cpp
-------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "ScriptMessageUI.h"


#include <maths/mathstypes.h>
#include <utils/Clock.h>
#include <utils/UTF8Utils.h>


static const double DISPLAY_TIME = 3.0;
static const double FADE_IN_TIME = 0.15;
static const float BACKGROUND_ALPHA = 0.55f;
static const float RISE_PX = 12.f; // The message rises by this much while fading in.


ScriptMessageUI::ScriptMessageUI(const GLUIRef& gl_ui_)
:	gl_ui(gl_ui_),
	creation_time(0)
{
}


ScriptMessageUI::~ScriptMessageUI()
{
	removeMessage();
}


void ScriptMessageUI::showMessage(const std::string& message)
{
	removeMessage();

	// Dark translucent rounded panel with large white text, word-wrapped to at most 60% of the viewport width.
	// The alphas are set in think() so that the message fades in.
	GLUITextView::CreateArgs args;
	args.background_colour = Colour3f(0.f);
	args.background_alpha = BACKGROUND_ALPHA; // Must be non-zero for the background to be created.
	args.background_corner_radius_px = 10;
	args.text_colour = Colour3f(1.f);
	args.text_alpha = 0.f;
	args.font_size_px = (int)(GLUI::getDefaultFontSizePx() * 1.6f);
	args.padding_px = 14;
	args.max_width = 1.2f;
	args.text_selectable = false;
	text_view = new GLUITextView(*gl_ui, UTF8Utils::sanitiseUTF8String(message), args);
	text_view->setBackgroundAlpha(0.f);
	gl_ui->addWidget(text_view);

	creation_time = Clock::getTimeSinceInit();

	think();
}


void ScriptMessageUI::think()
{
	if(!text_view)
		return;

	const double age = Clock::getTimeSinceInit() - creation_time;
	if(age > DISPLAY_TIME)
	{
		removeMessage();
		return;
	}

	// Fade in while rising slightly, then fade out when it is nearly time to remove the message.
	const float fade_in = Maths::smoothStep(0.f, 1.f, (float)(age / FADE_IN_TIME));
	const float frac = (float)(age / DISPLAY_TIME);
	const float fade_out = myClamp(1.f - Maths::smoothStep(0.8f, 1.2f, frac) * 2, 0.f, 1.f);
	const float alpha = fade_in * fade_out;

	text_view->setPos(Vec2f(
		myMax(-1.f, -text_view->getRect().getWidths().x / 2.f),
		gl_ui->getViewportMinMaxY() * 0.5f - gl_ui->getUIWidthForDevIndepPixelWidth(RISE_PX * (1.f - fade_in))
	));

	text_view->setTextAlpha(alpha);
	text_view->setBackgroundAlpha(alpha * BACKGROUND_ALPHA);
}


void ScriptMessageUI::removeMessage()
{
	checkRemoveAndDeleteWidget(gl_ui, text_view);
}
