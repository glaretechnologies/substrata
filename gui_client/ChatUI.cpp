/*=====================================================================
ChatUI.cpp
----------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "ChatUI.h"


#include "GUIClient.h"
#include <graphics/SRGBUtils.h>
#include <opengl/MeshPrimitiveBuilding.h>


ChatUI::ChatUI()
:	gui_client(NULL)
{}


ChatUI::~ChatUI()
{}


static const float widget_w = 0.6f;
static const float corner_radius_px = 8;
static const int font_size_px = 12;


void ChatUI::create(Reference<OpenGLEngine>& opengl_engine_, GUIClient* gui_client_, GLUIRef gl_ui_)
{
	opengl_engine = opengl_engine_;
	gui_client = gui_client_;
	gl_ui = gl_ui_;

	// Create background quad to go behind text
	background_overlay_ob = new OverlayObject();
	background_overlay_ob->material.albedo_linear_rgb = Colour3f(0);
	background_overlay_ob->material.alpha = 0.8f;
	// Init with dummy data, will be updated in updateWidgetTransforms()
	background_overlay_ob->ob_to_world_matrix = Matrix4f::identity();
	background_overlay_ob->mesh_data = opengl_engine->getUnitQuadMeshData(); // Dummy
	this->last_viewport_dims = Vec2i(0); 
	opengl_engine->addOverlayObject(background_overlay_ob);


	chat_line_edit = new GLUILineEdit();
	GLUILineEdit::GLUILineEditCreateArgs create_args;
	create_args.width = widget_w;
	create_args.background_colour = Colour3f(0.0f);
	create_args.background_alpha = 0.8f;
	create_args.font_size_px = font_size_px;
	chat_line_edit->create(*gl_ui, opengl_engine, /*dummy botleft=*/Vec2f(0.f), create_args);

	GLUILineEdit* chat_line_edit_ptr = chat_line_edit.ptr();
	chat_line_edit->on_enter_pressed = [chat_line_edit_ptr, gui_client_]()
		{
			if(!chat_line_edit_ptr->getText().empty())
			{
				gui_client_->sendChatMessage(chat_line_edit_ptr->getText());

				chat_line_edit_ptr->clear(); 
			}
		};

	gl_ui->addWidget(chat_line_edit);

	updateWidgetTransforms();
}


void ChatUI::destroy()
{
	if(background_overlay_ob.nonNull())
		opengl_engine->removeOverlayObject(background_overlay_ob);
	background_overlay_ob = NULL;

	if(chat_line_edit.nonNull())
		gl_ui->removeWidget(chat_line_edit);
	chat_line_edit = NULL;

	gl_ui = NULL;
	opengl_engine = NULL;
}


void ChatUI::think()
{
}


void ChatUI::appendMessage(const std::string& avatar_name, const Colour3f& avatar_colour, const std::string& msg)
{
	ChatMessage chatmessage;
	GLUIText::GLUITextCreateArgs name_args;
	name_args.colour = avatar_colour;
	name_args.font_size_px = font_size_px;
	chatmessage.name_text = new GLUIText(*gl_ui, opengl_engine, avatar_name, Vec2f(0.f), name_args);

	GLUIText::GLUITextCreateArgs msg_args;
	msg_args.font_size_px = font_size_px;
	chatmessage.msg_text = new GLUIText(*gl_ui, opengl_engine, msg, Vec2f(0.f), msg_args);

	messages.push_back(chatmessage);

	updateWidgetTransforms();
}


void ChatUI::viewportResized(int w, int h)
{
	if(gl_ui.nonNull())
	{
		updateWidgetTransforms();
	}
}


void ChatUI::updateWidgetTransforms()
{
	//---------------------------- Update chat_line_edit ----------------------------

	const float chat_line_edit_y = -gl_ui->getViewportMinMaxY() + /*0.15f*/gl_ui->getUIWidthForDevIndepPixelWidth(20);
	chat_line_edit->setPos(/*botleft=*/Vec2f(-1.f + gl_ui->getUIWidthForDevIndepPixelWidth(20), chat_line_edit_y));


	//---------------------------- Update background_overlay_ob ----------------------------

	const float y_scale = opengl_engine->getViewPortAspectRatio(); // scale from GL UI to opengl coords
	const float z = 0.1f;
	const float msgs_background_ob_y = chat_line_edit_y + gl_ui->getUIWidthForDevIndepPixelWidth(50);
	Vec2f background_pos(-1.f + gl_ui->getUIWidthForDevIndepPixelWidth(20), msgs_background_ob_y);
	//background_overlay_ob->ob_to_world_matrix = Matrix4f::translationMatrix(background_pos.x, background_pos.y * y_scale, z) * Matrix4f::scaleMatrix(background_w, background_h * y_scale, 1);
	background_overlay_ob->ob_to_world_matrix = Matrix4f::translationMatrix(background_pos.x, background_pos.y * y_scale, z) * Matrix4f::scaleMatrix(1, y_scale, 1);

	const float background_w = widget_w;
	const float background_h = 0.5f;

	if(this->last_viewport_dims != opengl_engine->getViewportDims())
	{
		// Viewport has changed, recreate rounded-corner rect.
		conPrint("ChatUI: Viewport has changed, recreate rounded-corner rect.");
		background_overlay_ob->mesh_data = MeshPrimitiveBuilding::makeRoundedCornerRect(*opengl_engine->vert_buf_allocator, /*i=*/Vec4f(1,0,0,0), /*j=*/Vec4f(0,1,0,0), /*w=*/background_w, /*h=*/background_h, 
			/*corner radius=*/gl_ui->getUIWidthForDevIndepPixelWidth(corner_radius_px), /*tris_per_corner=*/8);

		//rect = Rect2f(botleft, botleft + Vec2f(args.width, background_h));
		this->last_viewport_dims = opengl_engine->getViewportDims();
	}


	//---------------------------- Update chat message text ----------------------------

	const float vert_spacing = gl_ui->getUIWidthForDevIndepPixelWidth(10);

	const int msgs_padding_w_px = 8;
	const float clip_padding_w = gl_ui->getUIWidthForDevIndepPixelWidth(msgs_padding_w_px);
	
	// Just use zero for bottom clip padding so as not to clip of descenders.
	const Rect2f clip_region(gl_ui->OpenGLCoordsForUICoords(background_pos) + Vec2f(clip_padding_w, 0.f), gl_ui->OpenGLCoordsForUICoords(background_pos + Vec2f(background_w, background_h) - Vec2f(clip_padding_w)));

	float y = msgs_background_ob_y + gl_ui->getUIWidthForDevIndepPixelWidth(msgs_padding_w_px);
	for(auto it = messages.rbegin(); it != messages.rend(); ++it)
	{
		ChatMessage& msg = *it;

		msg.name_text->setPos(/*botleft=*/Vec2f(-1.f + gl_ui->getUIWidthForDevIndepPixelWidth(20 + msgs_padding_w_px), y));
		msg.name_text->overlay_ob->clip_region = clip_region;

		msg.msg_text->setPos(/*botleft=*/Vec2f(msg.name_text->getRect().getMax().x/* + gl_ui->getUIWidthForDevIndepPixelWidth(20)*/, y));
		msg.msg_text->overlay_ob->clip_region = clip_region;

		const float max_msg_y = myMax(msg.name_text->getRect().getMax().y, msg.msg_text->getRect().getMax().y);

		y = max_msg_y + vert_spacing;
	}
}


void ChatUI::eventOccurred(GLUICallbackEvent& event)
{
}
