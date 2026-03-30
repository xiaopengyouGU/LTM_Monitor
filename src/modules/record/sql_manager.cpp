#include "sql_manager.h"

bool SqlManager::setDB(const QString& fileName)
{
    DB = QSqlDatabase::addDatabase("QSQLITE");      //添加SQLITE数据库驱动
    DB.setDatabaseName(fileName);                   //设置数据库文件
    return DB.open();                               //返回结果。
}