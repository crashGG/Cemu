#include "gui/input/HotkeySettings.h"
#include <config/ActiveSettings.h>
#include <gui/guiWrapper.h>
#include "input/InputManager.h"
#include "HotkeySettings.h"

#if BOOST_OS_LINUX || BOOST_OS_MACOS
#include "resource/embedded/resources.h"
#endif

extern WindowInfo g_window_info;
const std::unordered_map<sHotkeyCfg*, std::function<void(void)>> HotkeySettings::s_cfgHotkeyToFuncMap{
	{&s_cfgHotkeys.toggleFullscreen, [](void) { s_mainWindow->ShowFullScreen(!s_mainWindow->IsFullScreen()); }},
	{&s_cfgHotkeys.toggleFullscreenAlt, [](void) { s_mainWindow->ShowFullScreen(!s_mainWindow->IsFullScreen()); }},
	{&s_cfgHotkeys.exitFullscreen, [](void) { s_mainWindow->ShowFullScreen(false); }},
	{&s_cfgHotkeys.takeScreenshot, [](void) { g_window_info.has_screenshot_request = true; }},
};

struct HotkeyEntry
{
	enum class InputButtonType : wxWindowID {
		Keyboard,
	};

	std::unique_ptr<wxStaticText> name;
	std::unique_ptr<wxButton> keyInput;
	sHotkeyCfg& hotkey;

	HotkeyEntry(wxStaticText* name, wxButton* keyInput, sHotkeyCfg& hotkey)
		: name(name), keyInput(keyInput), hotkey(hotkey)
	{
		keyInput->SetClientData(&hotkey);
		keyInput->SetId(static_cast<wxWindowID>(InputButtonType::Keyboard));
	}
};

HotkeySettings::HotkeySettings(wxWindow* parent)
	: wxFrame(parent, wxID_ANY, "Hotkey Settings")
{
	SetIcon(wxICON(X_HOTKEY_SETTINGS));

	m_sizer = new wxFlexGridSizer(0, 2, 10, 10);
	m_sizer->AddGrowableCol(1);

	m_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE);
	m_panel->SetSizer(m_sizer);
	Center();

	CreateColumnHeaders();

	/* hotkeys */
	CreateHotkeyRow("Toggle fullscreen", s_cfgHotkeys.toggleFullscreen);
	CreateHotkeyRow("Take screenshot", s_cfgHotkeys.takeScreenshot);

	m_sizer->SetSizeHints(this);
}

HotkeySettings::~HotkeySettings()
{
	if (m_needToSave)
	{
		g_config.Save();
	}
}

void HotkeySettings::Init(wxFrame* mainWindowFrame)
{
	s_keyboardHotkeyToFuncMap.reserve(s_cfgHotkeyToFuncMap.size());
	for (const auto& [cfgHotkey, func] : s_cfgHotkeyToFuncMap)
	{
		auto keyboardHotkey = cfgHotkey->keyboard.raw;
		if (keyboardHotkey > 0)
		{
			s_keyboardHotkeyToFuncMap[keyboardHotkey] = func;
		}
	}
	s_mainWindow = mainWindowFrame;
}

void HotkeySettings::CreateColumnHeaders(void)
{
	auto* emptySpace = new wxStaticText(m_panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);
	auto* keyboard = new wxStaticText(m_panel, wxID_ANY, "Keyboard", wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);

	keyboard->SetMinSize(m_minButtonSize);

	auto flags = wxSizerFlags().Expand();
	m_sizer->Add(emptySpace, flags);
	m_sizer->Add(keyboard, flags);
}

void HotkeySettings::CreateHotkeyRow(const wxString& label, sHotkeyCfg& cfgHotkey)
{
	auto* name = new wxStaticText(m_panel, wxID_ANY, label);
	auto* keyInput = new wxButton(m_panel, wxID_ANY, To_wxString(cfgHotkey.keyboard), wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS | wxBU_EXACTFIT);

	/* for starting input */
	keyInput->Bind(wxEVT_BUTTON, &HotkeySettings::OnKeyboardHotkeyInputLeftClick, this);

	/* for cancelling and clearing input */
	keyInput->Connect(wxEVT_RIGHT_UP, wxMouseEventHandler(HotkeySettings::OnHotkeyInputRightClick), NULL, this);

	keyInput->SetMinSize(m_minButtonSize);

	auto flags = wxSizerFlags().Expand();
	m_sizer->Add(name, flags);
	m_sizer->Add(keyInput, flags);

	m_hotkeys.emplace_back(name, keyInput, controllerInput, cfgHotkey);
}

void HotkeySettings::OnKeyboardHotkeyInputLeftClick(wxCommandEvent& event)
{
	auto* inputButton = static_cast<wxButton*>(event.GetEventObject());
	if (m_activeInputButton)
	{
		/* ignore multiple clicks of the same button */
		if (inputButton == m_activeInputButton) return;
		RestoreInputButton(m_activeInputButton);
	}
	inputButton->Bind(wxEVT_KEY_UP, &HotkeySettings::OnKeyUp, this);
	inputButton->SetLabelText('_');
	m_activeInputButton = inputButton;
}

void HotkeySettings::OnHotkeyInputRightClick(wxMouseEvent& event)
{
	if (m_activeInputButton)
	{
		RestoreInputButton(m_activeInputButton);
		return;
	}
	auto* inputButton = static_cast<wxButton*>(event.GetEventObject());
	auto& cfgHotkey = *static_cast<sHotkeyCfg*>(inputButton->GetClientData());
	using InputButtonType = HotkeyEntry::InputButtonType;
	switch (static_cast<InputButtonType>(inputButton->GetId()))
	{
	case InputButtonType::Keyboard: {
		uKeyboardHotkey newHotkey{};
		if (cfgHotkey.keyboard.raw != newHotkey.raw)
		{
			m_needToSave |= true;
			s_keyboardHotkeyToFuncMap.erase(cfgHotkey.keyboard.raw);
			cfgHotkey.keyboard = newHotkey;
			FinalizeInput<uKeyboardHotkey>(inputButton);
		}
	} break;
	default: break;
	}
}

void HotkeySettings::OnKeyUp(wxKeyEvent& event)
{
	auto* inputButton = static_cast<wxButton*>(event.GetEventObject());
	auto& cfgHotkey = *static_cast<sHotkeyCfg*>(inputButton->GetClientData());
	if (auto keycode = event.GetKeyCode(); IsValidKeycodeUp(keycode))
	{
		auto oldHotkey = cfgHotkey.keyboard;
		uKeyboardHotkey newHotkey{};
		newHotkey.key = keycode;
		newHotkey.alt = event.AltDown();
		newHotkey.ctrl = event.ControlDown();
		newHotkey.shift = event.ShiftDown();
		if ((newHotkey.raw != oldHotkey.raw) &&
			(s_keyboardHotkeyToFuncMap.find(newHotkey.raw) == s_keyboardHotkeyToFuncMap.end()))
		{
			m_needToSave |= true;
			cfgHotkey.keyboard = newHotkey;
			s_keyboardHotkeyToFuncMap.erase(oldHotkey.raw);
			s_keyboardHotkeyToFuncMap[newHotkey.raw] = s_cfgHotkeyToFuncMap.at(&cfgHotkey);
		}
	}
	FinalizeInput<uKeyboardHotkey>(inputButton);
}

template <typename T>
void HotkeySettings::FinalizeInput(wxButton* inputButton)
{
	auto& cfgHotkey = *static_cast<sHotkeyCfg*>(inputButton->GetClientData());
	if constexpr (std::is_same_v<T, uKeyboardHotkey>)
	{
		inputButton->Unbind(wxEVT_KEY_UP, &HotkeySettings::OnKeyUp, this);
		inputButton->SetLabelText(To_wxString(cfgHotkey.keyboard));
	}
	m_activeInputButton = nullptr;
}

void HotkeySettings::RestoreInputButton(wxButton* inputButton)
{
	using InputButtonType = HotkeyEntry::InputButtonType;
	switch (static_cast<InputButtonType>(inputButton->GetId()))
	{
	case InputButtonType::Keyboard: {
		FinalizeInput<uKeyboardHotkey>(inputButton);
	} break;
	default: break;
	}
}

bool HotkeySettings::IsValidKeycodeUp(int keycode)
{
	switch (keycode)
	{
	case WXK_NONE:
	case WXK_ESCAPE:
	case WXK_ALT:
	case WXK_CONTROL:
	case WXK_SHIFT:
		return false;
	default:
		return true;
	}
}

wxString HotkeySettings::To_wxString(uKeyboardHotkey hotkey)
{
	wxString ret{};
	if (hotkey.raw)
	{
		if (hotkey.alt)
		{
			ret.append("ALT + ");
		}
		if (hotkey.ctrl)
		{
			ret.append("CTRL + ");
		}
		if (hotkey.shift)
		{
			ret.append("SHIFT + ");
		}
		ret.append(wxAcceleratorEntry(0, hotkey.key).ToString());
	}
	return ret;
}
