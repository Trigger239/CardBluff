#include "database.h"
#include <cstring>

int db_add_client(sqlite3* db, const std::string& nickname,
                  const std::string& password, char** z_err_msg){
  char cmd[256];
  sprintf(cmd, "INSERT INTO clients (nickname, password, rating) "
               "VALUES ('%s', '%s', %I64d);",
          nickname.c_str(), password.c_str(), (long long) DEFAULT_RATING);
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

int db_get_id_by_nickname(sqlite3* db, const std::string& nickname,
                          bool* exists, long long* id, char** z_err_msg){
  char cmd[256];
  sprintf(cmd, "SELECT id FROM clients WHERE nickname='%s';",
          nickname.c_str());
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
  ((std::string*) password)->assign(values[0]);
  return 0;
}

int db_get_password(sqlite3* db, long long id, std::string* password,
                    char** z_err_msg){
  char cmd[256];
  sprintf(cmd, "SELECT password FROM clients WHERE id=%I64d;", id);
  return sqlite3_exec(db, cmd, db_get_password_cb, password, z_err_msg);
}

int db_set_password(sqlite3*db, long long id, const std::string& password,
                    char** z_err_msg){
  char cmd[256];
  sprintf(cmd, "UPDATE clients SET password='%s' WHERE id=%I64d;",
          password.c_str(), id);
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
