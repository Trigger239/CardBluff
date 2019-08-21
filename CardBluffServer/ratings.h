#ifndef RATINGS_H_INCLUDED
#define RATINGS_H_INCLUDED

#include "database.h"
#include "client.h"

#define RATING_EXP ((double) 10)
#define RATING_D ((double) 400)
#define RATING_K ((double) 32)

int update_ratings(sqlite3* db, Client* winner, Client* loser, long long* winner_rating, long long* loser_rating, long long* _delta, char** z_err_msg);

#endif // RATINGS_H_INCLUDED
