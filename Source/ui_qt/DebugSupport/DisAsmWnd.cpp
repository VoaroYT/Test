#include <QAction>
#include <QMenu>
#include <QString>
#include <QInputDialog>
#include <QMessageBox>
#include <QHeaderView>

#include "DisAsmWnd.h"
#include "ee/VuAnalysis.h"
#include "QtDisAsmTableModel.h"
#include "QtDisAsmVuTableModel.h"

#include "countof.h"
#include "string_cast.h"
#include "string_format.h"
#include "lexical_cast_ex.h"

#include "DebugExpressionEvaluator.h"

#define WNDSTYLE (WS_CLIPCHILDREN | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_CHILD | WS_MAXIMIZEBOX)

CDisAsmWnd::CDisAsmWnd(QMdiArea* parent, CVirtualMachine& virtualMachine, CMIPS* ctx, const char* name, CQtDisAsmTableModel::DISASM_TYPE disAsmType)
    : QMdiSubWindow(parent)
    , m_virtualMachine(virtualMachine)
    , m_ctx(ctx)
    , m_disAsmType(disAsmType)
{

	resize(320, 240);

	parent->addSubWindow(this);
	std::string title("Disassembly ");
	title += name;
	setWindowTitle(title.c_str());

	switch(disAsmType)
	{
	case CQtDisAsmTableModel::DISASM_STANDARD:
		m_model = new CQtDisAsmTableModel(this, virtualMachine, ctx);
		m_instructionSize = 4;
		break;
	case CQtDisAsmTableModel::DISASM_VU:
		m_model = new CQtDisAsmVuTableModel(this, virtualMachine, ctx);
		m_instructionSize = 8;
		break;
	default:
		assert(0);
		break;
	}
	m_tableView = new QTableView(this);
	setWidget(m_tableView);
	m_tableView->setModel(m_model);

	auto header = m_tableView->horizontalHeader();
	header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
	header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
	header->setSectionResizeMode(4, QHeaderView::ResizeToContents);
	header->setSectionResizeMode(5, QHeaderView::ResizeToContents);
	if(disAsmType == CQtDisAsmTableModel::DISASM_STANDARD)
	{
		header->setSectionResizeMode(6, QHeaderView::Stretch);
	}
	else
	{
		header->setSectionResizeMode(6, QHeaderView::ResizeToContents);
		header->setSectionResizeMode(7, QHeaderView::ResizeToContents);
		header->setSectionResizeMode(8, QHeaderView::Stretch);
	}

	m_tableView->verticalHeader()->hide();
	m_tableView->resizeColumnsToContents();

	m_tableView->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_tableView, &QTableView::customContextMenuRequested, this, &CDisAsmWnd::showMenu);
	// RefreshLayout();
}

CDisAsmWnd::~CDisAsmWnd()
{
}

void CDisAsmWnd::showMenu(const QPoint &pos)
{
	QMenu *rightClickMenu = new QMenu(this);

	QAction *goToPcAction = new QAction(this);
	goToPcAction->setText("GoTo PC");
	connect(goToPcAction, &QAction::triggered, std::bind(&CDisAsmWnd::GotoPC, this));
	rightClickMenu->addAction(goToPcAction);

	QAction *goToAddrAction = new QAction(this);
	goToAddrAction->setText("Goto Address...");
	connect(goToAddrAction, &QAction::triggered, std::bind(&CDisAsmWnd::GotoAddress, this));
	rightClickMenu->addAction(goToAddrAction);

	QAction *editCommentAction = new QAction(this);
	editCommentAction->setText("Edit Comment...");
	connect(editCommentAction, &QAction::triggered, std::bind(&CDisAsmWnd::EditComment, this));
	rightClickMenu->addAction(editCommentAction);

	QAction *findCallerAction = new QAction(this);
	findCallerAction->setText("Find Callers");
	connect(findCallerAction, &QAction::triggered, std::bind(&CDisAsmWnd::FindCallers, this));
	rightClickMenu->addAction(findCallerAction);

	auto index = m_tableView->currentIndex();
	if(index.isValid())
	{
		auto selected = index.row() * m_instructionSize;
		if(selected != MIPS_INVALID_PC)
		{
			uint32 nOpcode = GetInstruction(m_selected);
			if(m_ctx->m_pArch->IsInstructionBranch(m_ctx, m_selected, nOpcode) == MIPS_BRANCH_NORMAL)
			{
				char sTemp[256];
				uint32 nAddress = m_ctx->m_pArch->GetInstructionEffectiveAddress(m_ctx, m_selected, nOpcode);
				snprintf(sTemp, countof(sTemp), ("Go to 0x%08X"), nAddress);
				QAction *goToEaAction = new QAction(this);
				goToEaAction->setText(sTemp);
				connect(goToEaAction, &QAction::triggered, std::bind(&CDisAsmWnd::GotoEA, this));
				rightClickMenu->addAction(goToEaAction);
			}
		}
	}

	if(HistoryHasPrevious())
	{
		char sTemp[256];
		snprintf(sTemp, countof(sTemp), ("Go back (0x%08X)"), HistoryGetPrevious());
		QAction *goToEaAction = new QAction(this);
		goToEaAction->setText(sTemp);
		connect(goToEaAction, &QAction::triggered, std::bind(&CDisAsmWnd::HistoryGoBack, this));
		rightClickMenu->addAction(goToEaAction);
	}

	if(HistoryHasNext())
	{
		char sTemp[256];
		snprintf(sTemp, countof(sTemp), ("Go forward (0x%08X)"), HistoryGetNext());
		QAction *goToEaAction = new QAction(this);
		goToEaAction->setText(sTemp);
		connect(goToEaAction, &QAction::triggered, std::bind(&CDisAsmWnd::HistoryGoForward, this));
		rightClickMenu->addAction(goToEaAction);
	}

	if(m_disAsmType == CQtDisAsmTableModel::DISASM_VU)
	{
		QAction *analyseVuction = new QAction(this);
		analyseVuction->setText("Analyse Microprogram");
		connect(analyseVuction, &QAction::triggered,
		[&]()
		{
			CVuAnalysis::Analyse(m_ctx, 0, 0x4000);
			m_model->Redraw();
		}
		);
		rightClickMenu->addAction(analyseVuction);

	}

	rightClickMenu->popup(m_tableView->viewport()->mapToGlobal(pos));

}

void CDisAsmWnd::SetAddress(uint32 address)
{
	m_tableView->scrollTo(m_model->index(address / m_instructionSize, 0), QAbstractItemView::PositionAtTop);
	m_address = address;
}

void CDisAsmWnd::SetCenterAtAddress(uint32 address)
{
	m_tableView->scrollTo(m_model->index(m_address / m_instructionSize, 0), QAbstractItemView::PositionAtCenter);
}

void CDisAsmWnd::SetSelectedAddress(uint32 address)
{
	m_selectionEnd = -1;
	m_selected = address;
	auto index = m_model->index(address / m_instructionSize, 0);
	m_tableView->scrollTo(index, QAbstractItemView::PositionAtTop);
	m_tableView->setCurrentIndex(index);
}

void CDisAsmWnd::HandleMachineStateChange()
{
	m_model->Redraw();
}

void CDisAsmWnd::HandleRunningStateChange(CVirtualMachine::STATUS newState)
{
	if(newState == CVirtualMachine::STATUS::PAUSED)
	{
		//Recenter view if we just got into paused state
		m_address = m_ctx->m_State.nPC & ~(m_instructionSize - 1);
		SetCenterAtAddress(m_address);
	}
	m_model->Redraw();
}

void CDisAsmWnd::GotoAddress()
{
	if(m_virtualMachine.GetStatus() == CVirtualMachine::RUNNING)
	{
		// MessageBeep(-1);
		return;
	}

	bool ok;
	QString sValue = QInputDialog::getText(this, ("Goto Address"),
										("Enter new address:"), QLineEdit::Normal,
										((("0x") + lexical_cast_hex<std::string>(m_address, 8)).c_str()), &ok);
	if (!ok  || sValue.isEmpty())
		return;

	{
		try
		{
			uint32 nAddress = CDebugExpressionEvaluator::Evaluate(sValue.toStdString().c_str(), m_ctx);
			if(nAddress & (m_instructionSize - 1))
			{
				// MessageBox(m_hWnd, ("Invalid address"), NULL, 16);
				return;
			}

			if(m_address != nAddress)
			{
				HistorySave(m_address);
			}

			m_address = nAddress;
			SetAddress(nAddress);
		}
		catch(const std::exception& exception)
		{
			std::string message = std::string("Error evaluating expression: ") + exception.what();
			// MessageBox(m_hWnd, message.c_str(), NULL, 16);
		}
	}
}

void CDisAsmWnd::GotoPC()
{
	if(m_virtualMachine.GetStatus() == CVirtualMachine::RUNNING)
	{
		// MessageBeep(-1);
		return;
	}

	m_address = m_ctx->m_State.nPC;
	SetAddress(m_ctx->m_State.nPC);
}

void CDisAsmWnd::GotoEA()
{
	if(m_virtualMachine.GetStatus() == CVirtualMachine::RUNNING)
	{
		// MessageBeep(-1);
		return;
	}
	uint32 nOpcode = GetInstruction(m_selected);
	if(m_ctx->m_pArch->IsInstructionBranch(m_ctx, m_selected, nOpcode) == MIPS_BRANCH_NORMAL)
	{
		uint32 nAddress = m_ctx->m_pArch->GetInstructionEffectiveAddress(m_ctx, m_selected, nOpcode);

		if(m_address != nAddress)
		{
			HistorySave(m_address);
		}

		m_address = nAddress;
		SetAddress(nAddress);
	}
}

void CDisAsmWnd::EditComment()
{
	if(m_virtualMachine.GetStatus() == CVirtualMachine::RUNNING)
	{
		// MessageBeep(-1);
		return;
	}

	const char* comment = m_ctx->m_Comments.Find(m_selected);
	std::string commentConv;

	if(comment != nullptr)
	{
		commentConv = comment;
	}
	else
	{
		commentConv = ("");
	}

	bool ok;
	QString value = QInputDialog::getText(this, ("Edit Comment"),
										("Enter new comment:"), QLineEdit::Normal,
										(commentConv.c_str()), &ok);
	if (!ok  || value.isEmpty())
		return;

	// if(value != nullptr)
	{
		m_ctx->m_Comments.InsertTag(m_selected, value.toStdString().c_str());
		m_model->Redraw(m_selected);
	}
}

void CDisAsmWnd::FindCallers()
{
	if(m_virtualMachine.GetStatus() == CVirtualMachine::RUNNING)
	{
		// MessageBeep(-1);
		return;
	}

	FindCallersRequested(m_selected);
}

CDisAsmWnd::SelectionRangeType CDisAsmWnd::GetSelectionRange()
{
	if(m_selectionEnd == -1)
	{
		return SelectionRangeType(m_selected, m_selected);
	}

	if(m_selectionEnd > m_selected)
	{
		return SelectionRangeType(m_selected, m_selectionEnd);
	}
	else
	{
		return SelectionRangeType(m_selectionEnd, m_selected);
	}
}

void CDisAsmWnd::HistoryReset()
{
	m_historyPosition = -1;
	m_historySize = 0;
	memset(m_history, 0, sizeof(uint32) * HISTORY_STACK_MAX);
}

void CDisAsmWnd::HistorySave(uint32 nAddress)
{
	if(m_historySize == HISTORY_STACK_MAX)
	{
		memmove(m_history + 1, m_history, HISTORY_STACK_MAX - 1);
		m_historySize--;
	}

	m_history[m_historySize] = nAddress;
	m_historyPosition = m_historySize;
	m_historySize++;
}

void CDisAsmWnd::HistoryGoBack()
{
	if(m_historyPosition == -1) return;

	uint32 address = HistoryGetPrevious();
	m_history[m_historyPosition] = m_address;
	m_address = address;

	m_historyPosition--;
	SetAddress(address);

}

void CDisAsmWnd::HistoryGoForward()
{
	if(m_historyPosition == m_historySize) return;

	uint32 address = HistoryGetNext();
	m_historyPosition++;
	m_history[m_historyPosition] = m_address;
	m_address = address;
	SetAddress(address);
}

uint32 CDisAsmWnd::HistoryGetPrevious()
{
	return m_history[m_historyPosition];
}

uint32 CDisAsmWnd::HistoryGetNext()
{
	if(m_historyPosition == m_historySize) return 0;
	return m_history[m_historyPosition + 1];
}

bool CDisAsmWnd::HistoryHasPrevious()
{
	return (m_historySize != 0) && (m_historyPosition != -1);
}

bool CDisAsmWnd::HistoryHasNext()
{
	return (m_historySize != 0) && (m_historyPosition != (m_historySize - 1));
}

// uint32 CDisAsmWnd::GetAddressAtPosition(unsigned int nX, unsigned int nY)
// {
// 	uint32 address = nY / (m_renderMetrics.fontSizeY + m_renderMetrics.yspace);
// 	address = (m_address + (address * m_instructionSize));
// 	return address;
// }

uint32 CDisAsmWnd::GetInstruction(uint32 address)
{
	//Address translation perhaps?
	return m_ctx->m_pMemoryMap->GetInstruction(address);
}

void CDisAsmWnd::ToggleBreakpoint(uint32 address)
{
	if(m_virtualMachine.GetStatus() == CVirtualMachine::RUNNING)
	{
		// MessageBeep(-1);
		return;
	}
	m_ctx->ToggleBreakpoint(address);
	m_model->Redraw(address);
}

void CDisAsmWnd::UpdatePosition(int delta)
{
	m_address += delta;
	SetAddress(m_address);
}