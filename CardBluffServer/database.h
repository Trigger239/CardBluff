#ifndef DATABASE_H
#define DATABASE_H

#include <string>
#include "sqlite/sqlite3.h"
#include "common.h"

int db_add_client(sqlite3* db, const std::string& nickname,
                  const std::string& password, char** z_err_msg);
int db_get_id_by_nickname(sqlite3* db, const std::string& nickname,
                          bool* exists, long long* id, char** z_err_msg);
int db_get_password(sqlite3* db, long long id, std::string* password,
                    char** z_err_msg);
int db_get_rating(sqlite3* db, long long id, long long* rating,
                  char** z_err_msg);
int db_set_rating(sqlite3*db, long long id, long long rating,
                  char** z_err_msg);

#endif // DATABASE_H
