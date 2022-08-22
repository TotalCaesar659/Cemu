#pragma once

#include <wx/panel.h>

#include "input/emulated/EmulatedController.h"
#include "input/api/Controller.h"

class ControllerBase;
class wxTextCtrl;
class wxComboBox;

class InputPanel : public wxPanel
{
public:
	const wxColour kKeyColourNormalMode = 0xfafafa;
	const wxColour kKeyColourEditMode = 0x99ccff;
	const wxColour kKeyColourActiveMode = 0xE0E0E0;

	InputPanel(wxWindow* parent);

	ControllerPtr get_active_controller() const;

	virtual void on_timer(const EmulatedControllerPtr& emulated_controller, const ControllerPtr& controller);
	void on_left_click(wxMouseEvent& event);

	void reset_configuration();
	virtual void load_controller(const EmulatedControllerPtr& controller);

	void set_selected_controller(const EmulatedControllerPtr& emulated_controller, const ControllerPtr& controller_base);
	void reset_colours();

protected:
	void bind_hotkey_events(wxTextCtrl* text_ctrl);

	void on_edit_key_focus(wxFocusEvent& event);
	void on_edit_key_kill_focus(wxFocusEvent& event);
	void on_right_click(wxMouseEvent& event);

	bool reset_focused_element();

	bool m_right_down = false;
	int m_focused_element = wxID_NONE;
	std::unordered_map<int, wxColour> m_color_backup;
};

