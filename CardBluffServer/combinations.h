#ifndef COMBINATIONS_H
#define COMBINATIONS_H

#include <vector>
#include <algorithm>
#include <cassert>
#include <cinttypes>
#include <unordered_map>

using namespace std;



typedef uint8_t CARD_TYPE;
#define CARD_TYPE_MAX UINT8_MAX

#define SUITS (4)
#define RANKS (13)
#define COMBINATIONS (10)

typedef enum
{
	TWO = 0,
	THREE = 1,
	FOUR = 2,
	FIVE = 3,
	SIX = 4,
	SEVEN = 5,
	EIGHT = 6,
	NINE = 7,
	TEN = 8,
	JACK = 9,
	QUEEN = 10,
	KING = 11,
	ACE = 12
} rank_t;

typedef enum
{
	HEARTS		= 0,
	DIAMONDS	= 1,
	SPADES		= 2,
	CLUBS		= 3
} suit_t;

typedef enum
{
	NOTHING = 0,
	HIGH_CARD,
	PAIR,
	TWO_PAIRS,
	SET,
	STRAIGHT,
	FLUSH,
	FULL_HOUSE,
	SQUARE,
	STRAIGHT_FLUSH
} combination_t;

extern const unsigned int COMBINATION_SIZE[COMBINATIONS];
extern const vector<bool> HAS_SUIT;
extern const vector<uint8_t> HOW_MANY_RANKS;
extern const unordered_map<wchar_t, rank_t> WCHAR_TO_RANK;
extern const unordered_map<wchar_t, suit_t> WCHAR_TO_SUIT;
extern const unordered_map<wchar_t, combination_t> WCHAR_TO_COMBINATION;

class Hand
{
	vector<vector<bool>> suit_rank_b;	// suit_rank[s][r] is true if the hand contains a card of suit s and rank r
	vector<vector<int>> suit_rank;		// suit_rank[s] contains the ranks of suit s in the decreasing order
	vector<int> rank_number;			// rank_number[r] contains the number of cards of rank r
	vector<vector<int>> number_rank;	// number_rank[n] contains the ranks occuring at least (n + 1) times in the decreasing order

	void fill_number_rank();
	vector<pair<int, int>> find_every_straight_flush() const;
	pair<int, int> find_best_straight_flush() const;
	bool check_straight_flush(const pair<int, int>& rank_suit) const;
	vector<int> find_every_four_of_a_kind() const;
	int find_best_four_of_a_kind() const;
	bool check_four_of_a_kind(const int& rank) const;
	vector<pair<int, int>> find_every_full_house() const;
	pair<int, int> find_best_full_house() const;
	bool check_full_house(const pair<int, int>& rank3_rank2) const;
	// first rank, then suit
	vector<pair<int, int>> find_every_flush() const;
	pair<int, int> find_best_flush() const;
	bool check_flush(const pair<int, int>& rank_suit) const;
	vector<int> find_every_straight() const;
	int find_best_straight() const;
	bool check_straight(const int& rank) const;
	vector<int> find_every_three_of_a_kind() const;
	int find_best_three_of_a_kind() const;
	bool check_three_of_a_kind(const int& rank) const;
	vector<pair<int, int>> find_every_two_pairs() const;
	pair<int, int> find_best_two_pairs() const;
	bool check_two_pairs(const pair<int, int>& rank1_rank2) const;
	vector<int> find_every_pair() const;
	int find_best_pair() const;
	bool check_pair(const int& rank) const;
	vector<int> find_every_high_card() const;
	int find_best_high_card() const;
	bool check_high_card(const int& rank) const;
	void find_every_nothing() const;
	void find_best_nothing() const;
	bool check_nothing() const;
public:
	Hand();
	Hand(const vector<CARD_TYPE>& v);
	vector<int> find_best_combination();
	bool check_combination(const vector<int>& comb);
	static bool is_combination_nothing(const vector<int>& comb);
	bool is_best_combination(vector<int> comb);
	static void remove_suit(vector<int>& comb);
	static wstring parse_m_command(const wstring& command, vector<int>& combination);
	static bool less_combination(vector<int> u, vector<int> v);
};

#endif // COMBINATIONS_H
