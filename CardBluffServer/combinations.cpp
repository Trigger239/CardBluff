#include "combinations.h"
const int COMBINATION_SIZE[] =
{
    1,  // NOTHING
	2,  // HIGH_CARD
	2,  // PAIR
	3,  // TWO_PAIRS
	2,  // THREE
	2,  // STRAIGHT
	3,  // FLUSH
	3,  // FULL_HOUSE
	2,  // FOUR
	3   // STRAIGHT_FLUSH
};
const vector<bool> HAS_SUIT =
{
    false,  // NOTHING
	false,  // HIGH_CARD
	false,  // PAIR
	false,  // TWO_PAIRS
	false,  // THREE
	false,  // STRAIGHT
	true,   // FLUSH
	false,  // FULL_HOUSE
	false,  // FOUR
	true    // STRAIGHT_FLUSH
};
Hand::Hand()
{
    suit_rank.resize(SUITS);
    suit_rank_b.resize(SUITS);
    rank_number.resize(RANKS);
    for (int i = 0; i < SUITS; ++i)
        suit_rank_b[i].resize(RANKS);
    fill_number_rank();
}
Hand::Hand(const vector<CARD_TYPE>& v)
{
    suit_rank.resize(SUITS);
    suit_rank_b.resize(SUITS);
    rank_number.resize(RANKS);
    for (int i = 0; i < SUITS; ++i)
        suit_rank_b[i].resize(RANKS);
    for (int i = 0; i < ((int)((v).size())); ++i)
    {
        int a;
        int rank = v[i] >> 2;
        ++rank_number[rank];
        int suit = v[i] & 3;
        suit_rank[suit].push_back(rank);
        suit_rank_b[suit][rank] = true;
    }
    for (int i = 0; i < SUITS; ++i)
        sort(suit_rank[i].rbegin(), suit_rank[i].rend());
    fill_number_rank();
}
void Hand::fill_number_rank()
{
    number_rank.resize(4);
    for (int i = RANKS - 1; i >= 0; --i)
        for (int j = 0; j < rank_number[i]; ++j)
            number_rank[j].push_back(i);
}
vector<pair<int, int>> Hand::find_every_straight_flush() const
{
    bool good = true;
    vector<pair<int, int>> ans;
    vector<int> indices(SUITS, 4);
    while (good)
    {
        int x = -1;
        for (int i = 0; i < SUITS; ++i)
        {
            while ((indices[i] < ((int)((suit_rank[i]).size()))) &&
                (suit_rank[i][indices[i] - 4] - 4 != suit_rank[i][indices[i]]))
                ++indices[i];
            if ((indices[i] == ((int)((suit_rank[i]).size()))) &&
                ((suit_rank_b[i][RANKS - 1] == false) || (suit_rank[i][indices[i] - 4] != 3)))
                ++indices[i];
            if (((indices[i] < ((int)((suit_rank[i]).size()))) &&
                ((x == -1) || (suit_rank[i][indices[i] - 4] >
                    suit_rank[x][indices[x] - 4])))
                ||
                ((indices[i] == ((int)((suit_rank[i]).size()))) && (x == -1)))
                x = i;
        }
        if (x == -1)
            good = false;
        else
        {
            ans.emplace_back(suit_rank[x][indices[x] - 4], x);
            ++indices[x];
        }
    }
    return ans;
}
pair<int, int> Hand::find_best_straight_flush() const
{
    bool good = true;
    vector<int> indices(SUITS, 4);
    while (good)
    {
        int x = -1;
        for (int i = 0; i < SUITS; ++i)
        {
            while ((indices[i] < ((int)((suit_rank[i]).size()))) &&
                (suit_rank[i][indices[i] - 4] - 4 != suit_rank[i][indices[i]]))
                ++indices[i];
            if ((indices[i] == ((int)((suit_rank[i]).size()))) &&
                ((suit_rank_b[i][RANKS - 1] == false) || (suit_rank[i][indices[i] - 4] != 3)))
                ++indices[i];
            if (((indices[i] < ((int)((suit_rank[i]).size()))) &&
                ((x == -1) || (suit_rank[i][indices[i] - 4] >
                    suit_rank[x][indices[x] - 4])))
                ||
                ((indices[i] == ((int)((suit_rank[i]).size()))) && (x == -1)))
                x = i;
        }
        if (x == -1)
            good = false;
        else
             return pair<int, int>(suit_rank[x][indices[x] - 4], x);
    }
    return pair<int, int>(-1, -1);
}
bool Hand::check_straight_flush(const pair<int, int>& rank_suit) const
{
    int rank = rank_suit.first, suit = rank_suit.second;
    if (rank >= 3)
    {
        if (rank == 3)
        {
            for (int i = 0; i <= 3; ++i)
                if (suit_rank_b[suit][i] == false)
                    return false;
            return suit_rank_b[suit][RANKS - 1];
        }
        else
        {
            for (int i = rank - 4; i <= rank; ++i)
                if (suit_rank_b[suit][i] == false)
                    return false;
            return true;
        }
    }
}
vector<int> Hand::find_every_four_of_a_kind() const
{
    return number_rank[3];
}
int Hand::find_best_four_of_a_kind() const
{
    if (((int)((number_rank[3]).size())))
        return number_rank[3].front();
    else
        return -1;
}
bool Hand::check_four_of_a_kind(const int& rank) const
{
    return rank_number[rank] >= 4;
}
vector<pair<int, int>> Hand::find_every_full_house() const
{
    vector<pair<int, int>> ans;
    for (int i = 0; i < ((int)((number_rank[2]).size())); ++i)
        for (int j = 0; j < ((int)((number_rank[1]).size())); ++j)
            if (number_rank[2][i] != number_rank[1][j])
                ans.emplace_back(number_rank[2][i], number_rank[1][j]);
    return ans;
}
pair<int, int> Hand::find_best_full_house() const
{
    for (int i = 0; i < ((int)((number_rank[2]).size())); ++i)
        for (int j = 0; j < ((int)((number_rank[1]).size())); ++j)
            if (number_rank[2][i] != number_rank[1][j])
                return pair<int, int>(number_rank[2][i], number_rank[1][j]);
    return pair<int, int>(-1, -1);
}
bool Hand::check_full_house(const pair<int, int>& rank3_rank2) const
{
    return (rank_number[rank3_rank2.first] >= 3) && (rank_number[rank3_rank2.second] >= 2);
}
// first rank, then suit
vector<pair<int, int>> Hand::find_every_flush() const
{
    bool good = true;
    vector<pair<int, int>> ans;
    vector<int> indices(SUITS);
    for (int i = 0; i < SUITS; ++i)
        indices[i] = ((int)((suit_rank[i]).size())) - 5;
    while (good)
    {
        int x = -1;
        for (int i = 0; i < SUITS; ++i)
        {

            if ((indices[i] >= 0) &&
                ((x == -1) || (suit_rank[i][indices[i]] <=
                    suit_rank[x][indices[x]])))
                x = i;
        }
        if (x == -1)
            good = false;
        else
        {
            ans.emplace_back(suit_rank[x][indices[x]], x);
            --indices[x];
        }
    }
    return ans;
}
pair<int, int> Hand::find_best_flush() const
{
    pair<int, int> ans(-1, -1);
    for (int i = 0; i < SUITS; ++i)
    {
        int suit_size = ((int)((suit_rank[i]).size()));
        if ((suit_size >= 5) && ((ans.first == -1) || (ans.first > suit_rank[i][suit_size - 5])))
        {
            ans.first = suit_rank[i][suit_size - 5];
            ans.second = i;
        }
    }
    return ans;
}
bool Hand::check_flush(const pair<int, int>& rank_suit) const
{
    int rank = rank_suit.first;
    int suit = rank_suit.second;
    int suit_size = ((int)((suit_rank[suit]).size()));
    return (suit_rank[suit][rank]) && (suit_size >= 5) && (suit_rank[suit][suit_size - 5] <= rank);
}
vector<int> Hand::find_every_straight() const
{
    vector<int> ans;
    for (int x = 4; x < ((int)((number_rank[0]).size())); ++x)
        if (number_rank[0][x - 4] - 4 == number_rank[0][x])
            ans.push_back(number_rank[0][x - 4]);
    if (check_straight(3))
        ans.push_back(3);
    return ans;
}
int Hand::find_best_straight() const
{
    for (int x = 4; x < ((int)((number_rank[0]).size())); ++x)
        if (number_rank[0][x - 4] - 4 == number_rank[0][x])
            return number_rank[0][x - 4];
    if (check_straight(3))
        return 3;
    else
        return -1;
}
bool Hand::check_straight(const int& rank) const
{
    if (rank == 3)
    {
        for (int i = 0; i <= rank; ++i)
            if (rank_number[i] == 0)
                return false;
        return rank_number[RANKS - 1];
    }
    else
    {
        for (int i = rank - 4; i <= rank; ++i)
            if (rank_number[i] == 0)
                return false;
        return true;
    }
}
vector<int> Hand::find_every_three_of_a_kind() const
{
    return number_rank[2];
}
int Hand::find_best_three_of_a_kind() const
{
    if (((int)((number_rank[2]).size())))
        return number_rank[2].front();
    else
        return -1;
}
bool Hand::check_three_of_a_kind(const int& rank) const
{
    return rank_number[rank] >= 3;
}
vector<pair<int, int>> Hand::find_every_two_pairs() const
{
    vector<pair<int, int>> ans;
    for (int i = 0; i < ((int)((number_rank[1]).size())); ++i)
        for (int j = i + 1; j < ((int)((number_rank[1]).size())); ++j)
            ans.emplace_back(number_rank[1][i], number_rank[1][j]);
    return ans;
}
pair<int, int> Hand::find_best_two_pairs() const
{
    if (((int)((number_rank[1]).size())) >= 2)
        return pair<int, int>(number_rank[1][0], number_rank[1][1]);
    else
        return pair<int, int>(-1, -1);
}
bool Hand::check_two_pairs(const pair<int, int>& rank1_rank2) const
{
    return rank_number[rank1_rank2.first] && rank_number[rank1_rank2.second];
}
vector<int> Hand::find_every_pair() const
{
    return number_rank[1];
}
int Hand::find_best_pair() const
{
    if (((int)((number_rank[1]).size())))
        return number_rank[1].front();
    else
        return -1;
}
bool Hand::check_pair(const int& rank) const
{
    return rank_number[rank] >= 2;
}
vector<int> Hand::find_every_high_card() const
{
    return number_rank.front();
}
int Hand::find_best_high_card() const
{
    if (((int)((number_rank.front()).size())))
        return number_rank.front().front();
    else
        return -1;
}
bool Hand::check_high_card(const int& rank) const
{
    return rank_number[rank];
}
void Hand::find_every_nothing() const {}
void Hand::find_best_nothing() const {}
bool Hand::check_nothing() const
{
    return true;
}
vector<int> Hand::find_best_combination()
{
    vector<int> ans;
    auto c9 = find_best_straight_flush();
    if (c9.first != -1)
    {
        ans.push_back(9);
        ans.push_back(c9.first);
        ans.push_back(c9.second);
        return ans;
    }
    auto c8 = find_best_four_of_a_kind();
    if (c8 != -1)
    {
        ans.push_back(8);
        ans.push_back(c8);
        return ans;
    }
    auto c7 = find_best_full_house();
    if (c7.first != -1)
    {
    ans.push_back(7);
    ans.push_back(c7.first);
    ans.push_back(c7.second);
    return ans;
    }
    auto c6 = find_best_flush();
    if (c6.first != -1)
    {
        ans.push_back(6);
        ans.push_back(c6.first);
        ans.push_back(c6.second);
        return ans;
    }
    auto c5 = find_best_straight();
    if (c5 != -1)
    {
        ans.push_back(5);
        ans.push_back(c5);
        return ans;
    }
    auto c4 = find_best_three_of_a_kind();
    if (c4 != -1)
    {
        ans.push_back(4);
        ans.push_back(c4);
        return ans;
    }
    auto c3 = find_best_two_pairs();
    if (c3.first != -1)
    {
        ans.push_back(3);
        ans.push_back(c3.first);
        ans.push_back(c3.second);
        return ans;
    }
    auto c2 = find_best_pair();
    if (c2 != -1)
    {
        ans.push_back(2);
        ans.push_back(c2);
        return ans;
    }
    auto c1 = find_best_high_card();
    if (c1 != -1)
    {
        ans.push_back(1);
        ans.push_back(c1);
        return ans;
    }
    ans.push_back(0);
    return ans;
}
void clever_asserts(const vector<int>& comb)
{
    assert(comb.size());
    assert((comb.front() >= 0) && (comb.front() < COMBINATIONS));
    assert(COMBINATION_SIZE[comb.front()] == comb.size());
}
bool Hand::check_combination(const vector<int>& comb)
{
    clever_asserts(comb);
    switch (comb.front())
    {
        case NOTHING: return check_nothing();
        case HIGH_CARD: return check_high_card(comb[1]);
        case TWO_PAIRS: return check_two_pairs(pair<int, int>(comb[1], comb[2]));
        case SET: return check_three_of_a_kind(comb[1]);
        case STRAIGHT: return check_straight(comb[1]);
        case FLUSH: return check_flush(pair<int, int>(comb[1], comb[2]));
        case FULL_HOUSE: return check_full_house(pair<int, int>(comb[1], comb[2]));
        case SQUARE: return check_four_of_a_kind(comb[1]);
        case STRAIGHT_FLUSH: return check_straight_flush(pair<int, int>(comb[1], comb[2]));
    }
    assert(false);
    return false;
}
bool Hand::is_combination_nothing(const vector<int>& comb)
{
    clever_asserts(comb);
    return (comb.front() == NOTHING);
}
bool Hand::is_best_combination(vector<int> comb)
{
    check_combination(comb);
    vector<int> best_combination = find_best_combination();
    remove_suit(best_combination);
    remove_suit(comb);
    return best_combination == comb;
}
void Hand::remove_suit(vector<int>& comb)
{
    clever_asserts(comb);
    if (HAS_SUIT[comb.front()])
        comb.pop_back();
}
