#include "database.h"
#include <cstring>

#include "util/unicode.h"

int db_add_client(sqlite3* db, const std::wstring& nickname,
                  const std::wstring& password, char** z_err_msg){
  char cmd[256];
  sprintf(cmd, "INSERT INTO clients (nickname, password, rating) "
               "VALUES ('%s', '%s', %I64d);",
          converter.to_bytes(nickname).c_str(), converter.to_bytes(password).c_str(), (long long) DEFAULT_RATING);
  return sqlite3_exec(db, cmd, NULL, 0, z_err_msg);
}

struct db_get_id_by_nickname_args{
  long long* id;
  bool* exists;
};

int db_get_id_by_nickname_cb(void* args, int columns,
                             char** values, char** names){
  if((columns != 1) || (names == NULL) ||
     (names[0] == NULL) || strcmp(names[0], "id") ||
     (values == NULL) || (values[0] == NULL))
    return -1;
  sscanf(values[0], "%I64d", ((db_get_id_by_nickname_args*) args)->id);
  *(((db_get_id_by_nickname_args*) args)->exists) = true;
  return 0;
}

int db_get_id_by_nickname(sqlite3* db, const std::wstring& nickname,
                          bool* exists, long long* id, char** z_err_msg){
  char cmd[256];
  sprintf(cmd, "SELECT id FROM clients WHERE nickname='%s';",
          converter.to_bytes(nickname).c_str());
  *exists = false;
  db_get_id_by_nickname_args args = {id, exists};
  return sqlite3_exec(db, cmd, db_get_id_by_nickname_cb, &args, z_err_msg);
}

int db_get_password_cb(void* password, int columns,
                       char** values, char** names){
  if((columns != 1) || (names == NULL) ||
     (names[0] == NULL) || strcmp(names[0], "password") ||
     (values == NULL) || (values[0] == NULL))
    return -1;
  //log("Pass from DB: '%s'", values[0]);
  *((std::wstring*) password) = converter.from_bytes(values[0]);
  return 0;
}

int db_get_password(sqlite3* db, long long id, std::wstring* password,
                    char** z_err_msg){
  char cmd[256];
  sprintf(cmd, "SELECT password FROM clients WHERE id=%I64d;", id);
  return sqlite3_exec(db, cmd, db_get_password_cb, password, z_err_msg);
}

int db_set_password(sqlite3*db, long long id, const std::wstring& password,
                    char** z_err_msg){
  char cmd[256];
  sprintf(cmd, "UPDATE clients SET password='%s' WHERE id=%I64d;",
          converter.to_bytes(password).c_str(), id);
  return sqlite3_exec(db, cmd, NULL, nullptr, z_err_msg);
}

int db_get_rating_cb(void* rating, int columns, char** values, char** names){
  if((columns != 1) || (names == NULL) ||
     (names[0] == NULL) || strcmp(names[0], "rating") ||
     (values == NULL) || (values[0] == NULL))
    return -1;
  sscanf(values[0], "%I64d", (long long*) rating);
  return 0;
}

int db_get_rating(sqlite3* db, long long id, long long* rating,
                  char** z_err_msg){
  char cmd[256];
  sprintf(cmd, "SELECT rating FROM clients WHERE id=%I64d;", id);
  return sqlite3_exec(db, cmd, db_get_rating_cb, rating, z_err_msg);
}

int db_set_rating(sqlite3*db, long long id, long long rating,
                  char** z_err_msg){
  char cmd[256];
  sprintf(cmd, "UPDATE clients SET rating=%I64d WHERE id=%I64d;", rating, id);
  return sqlite3_exec(db, cmd, NULL, nullptr, z_err_msg);
}

int db_get_top_cb(void* top, int columns, char** values, char** names){
  if((columns != 2) || (names == NULL) ||
     (names[0] == NULL) || strcmp(names[0], "nickname") ||
     (names[1] == NULL) || strcmp(names[1], "rating"))
    return -1;
  long long rating;
  if(sscanf(values[1], "%I64d", &rating) != 1)
    return -1;
  ((std::vector<std::pair<std::wstring, long long>>*) top)->
    emplace_back(converter.from_bytes(values[0]), rating);
  return 0;
}

int db_get_top(sqlite3* db, unsigned int n,
               std::vector<std::pair<std::wstring, long long>>& top,
               char** z_err_msg){
  char cmd[256];
  sprintf(cmd, "SELECT nickname, rating FROM clients ORDER BY rating DESC LIMIT %u;", n);
  return sqlite3_exec(db, cmd, db_get_top_cb, &top, z_err_msg);
}
