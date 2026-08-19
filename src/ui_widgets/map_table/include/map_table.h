#ifndef MAP_TABLE_H
#define MAP_TABLE_H

#include <QWidget>

#if defined(MAP_TABLE_LIBRARY)
#  define MAP_TABLE_EXPORT Q_DECL_EXPORT
#else
#  define MAP_TABLE_EXPORT Q_DECL_IMPORT
#endif

class ChartMap;

// MapTable：图表映射表编辑器（对应 chart_map 组件）
// 职责：以表格编辑映射表（≤32 槽），支持新增/删除/导入/导出。
// 编辑在本地表格进行，「应用」把整表原子写回 ChartMap，冲突项给出错误提示。
// 内部状态（表格/映射）收敛在 Private（Pimpl）中。
class MAP_TABLE_EXPORT MapTable : public QWidget
{
    Q_OBJECT
public:
    explicit MapTable(QWidget *parent = nullptr);
    ~MapTable();

    void setChartMap(ChartMap *map);   // 绑定映射表（nullptr 则仅编辑不可应用）
    void reload();                     // 从 ChartMap 载入表格
signals:
    void mappingChanged();             // 应用成功后发出（供中转站/图例刷新）

private slots:
    void on_btnLoad_clicked();        // 载入：从 ChartMap 读
    void on_btnApply_clicked();       // 应用：整表写回 ChartMap
    void on_btnAdd_clicked();         // 新增一行
    void on_btnDel_clicked();         // 删除选中行
    void on_btnImport_clicked();      // 导入 JSON
    void on_btnExport_clicked();      // 导出 JSON

private:
    Q_DISABLE_COPY(MapTable)
    class Private;
    Private *pimpl = nullptr;
};

#endif // MAP_TABLE_H
