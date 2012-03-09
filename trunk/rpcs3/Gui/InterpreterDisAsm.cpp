#include "stdafx.h"
#include "InterpreterDisAsm.h"

static const int show_lines = 40;

u32 FixPc(const u32 pc)
{
	return pc - ((show_lines/2)*4);
}

InterpreterDisAsmFrame::InterpreterDisAsmFrame(const wxString& title, PPCThread* cpu)
	: FrameBase(NULL, wxID_ANY, title, "InterpreterDisAsmFrame", wxSize(500, 700))
	, m_main_panel(*new wxPanel(this))
	, CPU(*cpu)
	, PC(0)
	, exit(false)
{
	if(CPU.IsSPU())
	{
		SPU_DisAsm& dis_asm = *new SPU_DisAsm(CPU, InterpreterMode);
		decoder = new SPU_Decoder(dis_asm);
		disasm = &dis_asm;
	}
	else
	{
		PPU_DisAsm& dis_asm = *new PPU_DisAsm(CPU, InterpreterMode);
		decoder = new PPU_Decoder(dis_asm);
		disasm = &dis_asm;
	}
	wxBoxSizer& s_p_main = *new wxBoxSizer(wxVERTICAL);
	wxBoxSizer& s_b_main = *new wxBoxSizer(wxHORIZONTAL);

	m_list = new wxListView(&m_main_panel);
	wxButton& b_show_Val = *new wxButton(&m_main_panel, wxID_ANY, "Show value");
	wxButton& b_show_PC = *new wxButton(&m_main_panel, wxID_ANY, "Show PC");
	wxButton& b_next_opcode = *new wxButton(&m_main_panel, wxID_ANY, "Do next opcode");
	s_b_main.Add(&b_show_Val);
	s_b_main.AddSpacer(5);
	s_b_main.Add(&b_show_PC);
	s_b_main.AddSpacer(5);
	s_b_main.Add(&b_next_opcode);

	m_regs = new wxTextCtrl(&m_main_panel, wxID_ANY, wxEmptyString,
		wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE|wxNO_BORDER|wxTE_RICH2);
	m_regs->SetMinSize(wxSize(495, 100));
	m_regs->SetEditable(false);

	wxBoxSizer& s_w_list = *new wxBoxSizer(wxHORIZONTAL);
	s_w_list.Add(m_list);
	s_w_list.AddSpacer(5);
	s_w_list.Add(m_regs);

	s_p_main.AddSpacer(5);
	s_p_main.Add(&s_b_main);
	s_p_main.Add(&s_w_list);
	s_p_main.AddSpacer(5);

	wxBoxSizer& s_p_main_h = *new wxBoxSizer(wxVERTICAL);
	s_p_main_h.AddSpacer(5);
	s_p_main_h.Add(&s_p_main);
	s_p_main_h.AddSpacer(5);
	m_main_panel.SetSizerAndFit(&s_p_main_h);

	wxBoxSizer& s_main = *new wxBoxSizer(wxVERTICAL);
	s_main.Add(&m_main_panel);
	SetSizerAndFit(&s_main);

	m_list->InsertColumn(0, "ASM");

	for(uint i=0; i<show_lines; ++i)
	{
		m_list->InsertItem(m_list->GetItemCount(), wxEmptyString);
	}

	Connect(m_regs->GetId(), wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler(InterpreterDisAsmFrame::OnUpdate));
	Connect(b_show_Val.GetId(), wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(InterpreterDisAsmFrame::Show_Val));
	Connect(b_show_PC.GetId(), wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(InterpreterDisAsmFrame::Show_PC));
	Connect(b_next_opcode.GetId(), wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(InterpreterDisAsmFrame::DoOpcode));
	Connect(wxEVT_SIZE, wxSizeEventHandler(InterpreterDisAsmFrame::OnResize));
	wxGetApp().Connect(m_list->GetId(), wxEVT_MOUSEWHEEL, wxMouseEventHandler(InterpreterDisAsmFrame::MouseWheel), (wxObject*)0, this);
	wxGetApp().Connect(wxEVT_KEY_DOWN, wxKeyEventHandler(InterpreterDisAsmFrame::OnKeyDown), (wxObject*)0, this);

	//ShowPc(Loader.entry);
	WriteRegs();
}

void InterpreterDisAsmFrame::Close(bool force)
{
	exit = true;
	wxFrame::Close(force);
}

void InterpreterDisAsmFrame::OnKeyDown(wxKeyEvent& event)
{
	if(wxGetActiveWindow() != this)
	{
		event.Skip();
		return;
	}

	if(event.GetKeyCode() == WXK_SPACE && event.ControlDown())
	{
		DoOpcode(wxCommandEvent());
		return;
	}

	//event.Skip();
}

void InterpreterDisAsmFrame::DoUpdate()
{
	Show_PC(wxCommandEvent());
	WriteRegs();
}

void InterpreterDisAsmFrame::ShowPc(const int pc)
{
	PC = pc;
	for(uint i=0; i<show_lines; ++i, PC += 4)
	{
		if(!Memory.IsGoodAddr(PC, 4))
		{
			m_list->SetItem(i, 0, wxString::Format("[%08x] illegal address", PC));
			continue;
		}

		disasm->dump_pc = PC;
		decoder->Decode(Memory.Read32(PC));
		m_list->SetItem(i, 0, disasm->last_opcode);

		m_list->SetItemBackgroundColour( i, PC == CPU.PC ? wxColour("Green") : wxColour("White") );
	}
}

void InterpreterDisAsmFrame::WriteRegs()
{
	m_regs->Clear();
	m_regs->WriteText(CPU.RegsToString());
}

void InterpreterDisAsmFrame::OnUpdate(wxCommandEvent& event)
{
	//WriteRegs();
}

void InterpreterDisAsmFrame::Show_Val(wxCommandEvent& WXUNUSED(event))
{
	wxDialog* diag = new wxDialog(this, wxID_ANY, "Set value", wxDefaultPosition);

	wxBoxSizer* s_panel(new wxBoxSizer(wxVERTICAL));
	wxBoxSizer* s_b_panel(new wxBoxSizer(wxHORIZONTAL));
	wxTextCtrl* p_pc(new wxTextCtrl(diag, wxID_ANY));

	s_panel->Add(p_pc);
	s_panel->AddSpacer(8);
	s_panel->Add(s_b_panel);

	s_b_panel->Add(new wxButton(diag, wxID_OK), wxLEFT, 0, 5);
	s_b_panel->AddSpacer(5);
	s_b_panel->Add(new wxButton(diag, wxID_CANCEL), wxRIGHT, 0, 5);

	diag->SetSizerAndFit( s_panel );

	p_pc->SetLabel(wxString::Format("%x", CPU.PC));

	if(diag->ShowModal() == wxID_OK)
	{
		u64 pc = CPU.PC;
		sscanf(p_pc->GetLabel(), "%x", &pc);
		ShowPc(FixPc(pc));
	}
}

void InterpreterDisAsmFrame::Show_PC(wxCommandEvent& WXUNUSED(event))
{
	ShowPc(FixPc(CPU.PC));
}

void InterpreterDisAsmFrame::DoOpcode(wxCommandEvent& WXUNUSED(event))
{
#if 1
	static bool founded = false;
	if(!founded)
	{
		while(CPU.PC != 0x1875c) CPU.Exec();
		founded = true;
	}
	else
#endif
	CPU.Exec();
	DoUpdate();
}
	
void InterpreterDisAsmFrame::OnResize(wxSizeEvent& event)
{
	const wxSize& size( GetClientSize() );

	wxTextCtrl& regs = *m_regs;
	wxListView& list = *m_list;

	const int list_size = size.GetWidth() * 0.75;
	const int regs_size = size.GetWidth() * 0.25 - 5;
	list.SetMinSize(wxSize(list_size,  size.GetHeight()-55));
	regs.SetMinSize(wxSize(regs_size,  size.GetHeight()-55));
	m_main_panel.SetSize( size );
	m_main_panel.GetSizer()->RecalcSizes();

	list.SetColumnWidth(0, list_size-8);
}

void InterpreterDisAsmFrame::MouseWheel(wxMouseEvent& event)
{
	const int value = (event.m_wheelRotation / event.m_wheelDelta);
	ShowPc( PC - (show_lines + value) * 4 );

	event.Skip();
}
