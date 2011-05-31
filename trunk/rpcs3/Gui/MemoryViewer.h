#pragma once

#include <wx/listctrl.h>

class MemoryViewerPanel : public wxFrame
{
	static const uint LINE_COUNT = 50;
	static const uint COL_COUNT = 10;

	uint m_PC;
	wxListView* hex_wind;
	uint m_colsize;

public:
	MemoryViewerPanel(wxWindow* parent);
	virtual void OnResize(wxSizeEvent& event);

	virtual void Next(wxCommandEvent& event);
	virtual void Prev(wxCommandEvent& event);
	virtual void fNext(wxCommandEvent& event);
	virtual void fPrev(wxCommandEvent& event);

	virtual void ShowPC();

	void SetPC(const uint pc) { m_PC = pc; }
};