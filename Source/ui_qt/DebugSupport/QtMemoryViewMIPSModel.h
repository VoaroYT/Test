#pragma once

#include <QAbstractTableModel>

#include "MIPS.h"

class CQtMemoryViewMIPSModel : public QAbstractTableModel
{
	Q_OBJECT
public:
	typedef std::string (CQtMemoryViewMIPSModel::*UnitRenderer)(uint32) const;
	struct UNITINFO
	{
		unsigned int bytesPerUnit = 0;
		unsigned int charsPerUnit = 0;
		UnitRenderer renderer = nullptr;
		const char* description = nullptr;
	};

	CQtMemoryViewMIPSModel(QObject*, CMIPS*, int);
	~CQtMemoryViewMIPSModel();

	int rowCount(const QModelIndex& parent = QModelIndex()) const Q_DECL_OVERRIDE;
	int columnCount(const QModelIndex& parent = QModelIndex()) const Q_DECL_OVERRIDE;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const Q_DECL_OVERRIDE;
	void DoubleClicked(const QModelIndex& index, QWidget* parent = nullptr);

	void Redraw();

	unsigned int UnitsForCurrentLine() const;
	unsigned int BytesForCurrentLine() const;
	void SetUnitsForCurrentLine(unsigned int);
	unsigned int CharsPerUnit() const;

	void SetActiveUnit(int);
	int GetActiveUnit();
	int GetBytesPerUnit();

	static std::vector<UNITINFO> g_units;

protected:
	QVariant headerData(int section, Qt::Orientation orientation, int role) const Q_DECL_OVERRIDE;

private:
	uint8 GetByte(uint32) const;
	std::string RenderByteUnit(uint32) const;
	std::string RenderWordUnit(uint32) const;
	std::string RenderSingleUnit(uint32) const;

	CMIPS* m_context;
	int m_activeUnit;
	unsigned int m_size;
	std::atomic<unsigned int> m_unitsForCurrentLine = 0x20;
};