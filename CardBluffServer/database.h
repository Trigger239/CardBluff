#ifndef DATABASE_H
#define DATABASE_H

#include <string>
#include <vector>
#include "sqlite/sqlite3.h"
#include "common.h"

int db_add_client(sqlite3* db, const std::wstring& nickname,
                  const std::wstring& password, char** z_err_msg);
int db_get_id_by_nickname(sqlite3* db, const std::wstring& nickname,
                          bool* exists, long long* id, char** z_err_msg);
int db_get_password(sqlite3* db, long long id, std::wstring* password,
                    char** z_err_msg);
int db_get_rating(sqlite3* db, long long id, long long* rating,
                  char** z_err_msg);
int db_set_rating(sqlite3*db, long long id, long long rating,
                  char** z_err_msg);
int db_get_top(sqlite3* db, unsigned int n,
               std::vector<std::pair<std::wstring, long long>>& top,
               char** z_err_msg);

#endif // DATABASE_H
