#include "map_table.h"
#include "ui_map_table.h"

#include "chart_map.h"

#include <QComboBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QTableWidgetItem>
#include <algorithm>

// ============================================================
// 私有实现（Pimpl）：UI 与映射状态收敛于此
// ============================================================
class MapTable::Private
{
public:
    explicit Private(MapTable *w) : w(w) {}

    void setup();                       // 表格初始配置
    void reload();                      // 从 ChartMap 载入表格
    void appendRow(const ChartMapEntry &e);
    bool entryFromRow(int row, ChartMapEntry &e, QString *err) const;
    void onLoad();
    void onApply();
    void onAdd();
    void onDel();
    void onImport();
    void onExport();

    enum Col { Col_Source = 0, Col_Key, Col_Index, Col_Name, Col_Channel, Col_Count };

    MapTable     *w = nullptr;
    Ui::MapTable *ui = nullptr;
    ChartMap     *m_map = nullptr;
};

void MapTable::Private::setup()
{
    ui = new Ui::MapTable;
    ui->setupUi(w);

    // 表格列宽策略：源键/信号名自适应，其余按内容
    ui->tableMap->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->tableMap->horizontalHeader()->setStretchLastSection(true);
    ui->tableMap->verticalHeader()->setVisible(false);
}

void MapTable::Private::reload()
{
    ui->tableMap->clearContents();
    ui->tableMap->setRowCount(0);
    if (!m_map)
        return;
    const QList<ChartMapEntry> list = m_map->entries();
    for (const ChartMapEntry &e : list)
        appendRow(e);
}

void MapTable::Private::appendRow(const ChartMapEntry &e)
{
    const int row = ui->tableMap->rowCount();
    ui->tableMap->insertRow(row);

    // 数据源（下拉）
    QComboBox *src = new QComboBox(ui->tableMap);
    src->addItems({QString("CAN-FD"), QString("Modbus"), QString("LTM")});
    src->setCurrentIndex(e.sourceType <= ChartMap_LTM ? e.sourceType : ChartMap_CANFD);
    ui->tableMap->setCellWidget(row, Col_Source, src);

    // 源键（hex）
    QTableWidgetItem *key = new QTableWidgetItem(QString::number(e.sourceKey, 16).toUpper());
    ui->tableMap->setItem(row, Col_Key, key);

    // 信号序号
    QTableWidgetItem *idx = new QTableWidgetItem(QString::number(e.signalIndex));
    ui->tableMap->setItem(row, Col_Index, idx);

    // 信号名
    QTableWidgetItem *name = new QTableWidgetItem(e.signalName);
    ui->tableMap->setItem(row, Col_Name, name);

    // 通道（0~5）
    QComboBox *ch = new QComboBox(ui->tableMap);
    ch->addItem(QString("不映射"));
    for (int i = 1; i <= ChartMap::ChannelCount; i++)
        ch->addItem(QString("CH%1").arg(i));
    ch->setCurrentIndex(e.channel <= ChartMap::ChannelCount ? e.channel : 0);
    ui->tableMap->setCellWidget(row, Col_Channel, ch);

}

bool MapTable::Private::entryFromRow(int row, ChartMapEntry &e, QString *err) const
{
    const QComboBox *src = qobject_cast<QComboBox *>(ui->tableMap->cellWidget(row, Col_Source));
    const QComboBox *ch  = qobject_cast<QComboBox *>(ui->tableMap->cellWidget(row, Col_Channel));
    if (!src || !ch || !ui->tableMap->item(row, Col_Key)
        || !ui->tableMap->item(row, Col_Index) || !ui->tableMap->item(row, Col_Name)) {
        if (err) *err = QString("第%1行控件不完整").arg(row + 1);
        return false;
    }

    e.sourceType = (quint8)src->currentIndex();
    bool ok = false;
    e.sourceKey = ui->tableMap->item(row, Col_Key)->text().trimmed().toULongLong(&ok, 16);
    if (!ok) {
        if (err) *err = QString("第%1行源键不是合法 hex").arg(row + 1);
        return false;
    }
    e.signalIndex = (quint16)ui->tableMap->item(row, Col_Index)->text().toUInt(&ok);
    if (!ok) {
        if (err) *err = QString("第%1行信号序号非法").arg(row + 1);
        return false;
    }
    e.signalName = ui->tableMap->item(row, Col_Name)->text();
    e.channel    = (quint8)ch->currentIndex();
    return true;
}

void MapTable::Private::onLoad()
{
    reload();
}

void MapTable::Private::onApply()
{
    if (!m_map)
        return;

    QList<ChartMapEntry> list;
    for (int r = 0; r < ui->tableMap->rowCount(); r++) {
        ChartMapEntry e;
        QString err;
        if (!entryFromRow(r, e, &err)) {
            QMessageBox::warning(w, "映射表", err);
            return;
        }
        list.append(e);
    }

    QString err;
    if (!m_map->setEntries(list, &err)) {
        QMessageBox::warning(w, "映射表", err);
        return;
    }
    emit w->mappingChanged();
}

void MapTable::Private::onAdd()
{
    appendRow(ChartMapEntry());
}

void MapTable::Private::onDel()
{
    const QList<QModelIndex> rows = ui->tableMap->selectionModel()->selectedRows();
    if (rows.isEmpty())
        return;
    QList<int> sorted;
    for (const QModelIndex &idx : rows)
        sorted.append(idx.row());
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    for (int r : sorted)
        ui->tableMap->removeRow(r);
}

void MapTable::Private::onImport()
{
    if (!m_map)
        return;
    const QString path = QFileDialog::getOpenFileName(w, "导入映射表", "", "JSON 文件(*.json)");
    if (path.isEmpty())
        return;
    QString err;
    if (!m_map->loadJson(path, &err))
        QMessageBox::warning(w, "映射表", err);
    else
        reload();
}

void MapTable::Private::onExport()
{
    if (!m_map)
        return;
    const QString path = QFileDialog::getSaveFileName(w, "导出映射表", "chart_map.json", "JSON 文件(*.json)");
    if (path.isEmpty())
        return;
    QString err;
    if (!m_map->saveJson(path, &err))
        QMessageBox::warning(w, "映射表", err);
}

// ============================================================
// 公共接口：委托给私有实现
// ============================================================
MapTable::MapTable(QWidget *parent)
    : QWidget(parent)
    , pimpl(new Private(this))
{
    pimpl->setup();
}

MapTable::~MapTable()
{
    delete pimpl;
}

void MapTable::setChartMap(ChartMap *map)
{
    pimpl->m_map = map;
    pimpl->reload();
}

void MapTable::reload()
{
    pimpl->reload();
}

void MapTable::on_btnLoad_clicked()   { pimpl->onLoad(); }
void MapTable::on_btnApply_clicked()  { pimpl->onApply(); }
void MapTable::on_btnAdd_clicked()    { pimpl->onAdd(); }
void MapTable::on_btnDel_clicked()    { pimpl->onDel(); }
void MapTable::on_btnImport_clicked() { pimpl->onImport(); }
void MapTable::on_btnExport_clicked() { pimpl->onExport(); }
