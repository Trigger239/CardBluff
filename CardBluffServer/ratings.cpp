#include "ratings.h"

#include <cmath>

//r1 = db(1) (winner)
//r2 = db(2) (loser)
//R1 = 10^(r1 / 400)
//R2 = 10^(r2 / 400)
//E1 = R1 / (R1 + R2)
//E2 = R2 / (R1 + R2)
//r1 = r1 + K * (1 - E1)
//r2 = r2 - K * E2

int update_ratings(sqlite3* db, Client* winner, Client* loser, long long* winner_rating, long long* loser_rating, long long* _delta, char** z_err_msg){
  long long winner_id = winner->get_id();
  long long loser_id = loser->get_id();
  long long winner_r, loser_r;
  int ret;

  if((ret = db_get_rating(db, winner_id, &winner_r, z_err_msg)))
    return ret;
  if((ret = db_get_rating(db, loser_id, &loser_r, z_err_msg)))
    return ret;

  double winner_R = std::pow(RATING_EXP, (double) winner_r / RATING_D);
  double loser_R = std::pow(RATING_EXP, (double) loser_r / RATING_D);
  double loser_E = loser_R / (winner_R + loser_R);

  long long delta = RATING_K * loser_E + 0.5;

  winner_r += delta;
  loser_r -= delta;

  if((ret = db_set_rating(db, winner_id, winner_r, z_err_msg)))
    return ret;
  if((ret = db_set_rating(db, loser_id, loser_r, z_err_msg)))
    return ret;

  *_delta = delta;
  *winner_rating = winner_r;
  *loser_rating = loser_r;

  return 0;
}
