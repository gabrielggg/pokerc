// holdem_7462.cpp
// Compile: g++ holdem_7462.cpp -O2 -std=c++17 -o holdem_7462
// Runs: ./holdem_7462
//
// Simulates 3 limit hold'em hands and prints full action logs.
// Builds a canonical 5-card hand ranking table (1..7462) at startup
// and uses it to produce the index for each player's final 7-card hand.
//
// This code prioritizes clarity and explanation.

#include <bits/stdc++.h>
using namespace std;

/* ------------------------------------------------------------------
   SECTION A — Card encoding and helpers (Cactus Kev style fields)
   ------------------------------------------------------------------ */

const array<int,13> PRIMES   = {2,3,5,7,11,13,17,19,23,29,31,37,41};
const array<string,13> RANKS = {"2","3","4","5","6","7","8","9","T","J","Q","K","A"};
const array<string,4> SUITS  = {"Clubs","Diamonds","Hearts","Spades"};

enum Suit { CLUBS=0, DIAMONDS=1, HEARTS=2, SPADES=3 };

// Encode a card into a 32-bit int similar to Cactus Kev's format:
// low byte = prime, next nibble = rank, next nibble = suit bit, high bits = rank bitmask
int encodeCard(int rank /*2..14*/, int suit /*0..3*/) {
    int prime = PRIMES[rank-2];
    int rankBit = 1 << (rank-2);
    int suitBit = 1 << suit;
    int card = 0;
    card |= prime;             // bits 0..7
    card |= (rank << 8);       // bits 8..11
    card |= (suitBit << 12);   // bits 12..15
    card |= (rankBit << 16);   // bits 16..28
    return card;
}

string cardToString(int card) {
    int rank = (card >> 8) & 0xF;             // 2..14
    int suitBits = (card >> 12) & 0xF;
    int suitIndex = 0;
    for(int i=0;i<4;++i) if(suitBits & (1<<i)) suitIndex = i;
    return RANKS[rank-2] + " of " + SUITS[suitIndex];
}

/* ------------------------------------------------------------------
   SECTION B — Deck
   ------------------------------------------------------------------ */

struct Deck {
    vector<int> cards;
    Deck() { reset(); }

    void reset() {
        cards.clear();
        cards.reserve(52);
        for(int s=0;s<4;++s)
            for(int r=2;r<=14;++r)
                cards.push_back(encodeCard(r,s));
    }

    void shuffle() {
        static random_device rd;
        static mt19937 rng(rd());
        std::shuffle(cards.begin(), cards.end(), rng);
    }

    int deal() {
        int c = cards.back();
        cards.pop_back();
        return c;
    }
};

/* ------------------------------------------------------------------
   SECTION C — Canonical 5-card hand classification
   We will produce a canonical "class key" and tiebreaker ranks
   used to determine ordering among hands in the same category.
   Categories are ordered strongest -> weakest:
   1 Straight Flush
   2 Four of a Kind
   3 Full House
   4 Flush
   5 Straight
   6 Three of a Kind
   7 Two Pair
   8 One Pair
   9 High Card
   Smaller category number = better hand.
   ------------------------------------------------------------------ */

enum Category {
    CAT_STRAIGHT_FLUSH = 1,
    CAT_FOUR_KIND      = 2,
    CAT_FULL_HOUSE     = 3,
    CAT_FLUSH          = 4,
    CAT_STRAIGHT       = 5,
    CAT_THREE_KIND     = 6,
    CAT_TWO_PAIR       = 7,
    CAT_ONE_PAIR       = 8,
    CAT_HIGH_CARD      = 9
};

// Helper: extract rank (2..14) from encoded card
inline int cardRank(int card) { return (card >> 8) & 0xF; }
// Helper: return suit index 0..3
inline int cardSuit(int card) {
    int s = (card >> 12) & 0xF;
    for(int i=0;i<4;++i) if(s & (1<<i)) return i;
    return 0;
}

// Return sorted vector of ranks descending, with Ace as 14.
// We will also provide a rank-bit mask for straight detection.
vector<int> ranksSortedDesc(const array<int,5>& hand) {
    vector<int> r(5);
    for(int i=0;i<5;++i) r[i] = cardRank(hand[i]);
    sort(r.begin(), r.end(), greater<int>());
    return r;
}

// Return rank bitmask (bits 2..14 mapped to positions 0..12)
int rankBitmask(const array<int,5>& hand) {
    int mask = 0;
    for(int i=0;i<5;++i){
        int r = cardRank(hand[i]);
        mask |= 1 << (r - 2); // bit 0 = rank 2
    }
    return mask;
}

// Detect straight and return highest card of straight (ace-high=14, wheel returns 5)
// Returns 0 if not straight.
int detectStraightTop(int rankMask) {
    // rankMask uses bit0 for '2', bit12 for 'A'
    // Straight patterns: any 5 consecutive bits set.
    // Wheel A-2-3-4-5: bits for A and 2-5 -> mask with top=5
    // Check top from A(14) down to 5
    for(int top=14; top>=5; --top) {
        int topIndex = top - 2;
        // bits from topIndex down to topIndex-4 must be set
        bool ok = true;
        for(int k=0;k<5;++k){
            int idx = topIndex - k;
            if(idx < 0) { ok=false; break;}
            if(((rankMask >> idx) & 1) == 0) { ok=false; break; }
        }
        if(ok) return top;
    }
    // Wheel special case: A-2-3-4-5 -> bits for A and 2..5
    // The loop above already finds top=5 if bits present, so it's okay.
    return 0;
}

// Helper to get frequency map of ranks (value -> count)
array<int,15> rankCounts(const array<int,5>& hand) {
    array<int,15> cnt; cnt.fill(0);
    for(int i=0;i<5;++i) cnt[cardRank(hand[i])] ++;
    return cnt;
}

// Build canonical key string for a 5-card hand
// Key format: category|t1,t2,t3,...
// t1.. are tiebreaker ranks in descending significance (higher -> better)
struct HandClass {
    int category;              // 1..9 (1 best)
    vector<int> kickers;       // tiebreaker ranks (e.g., for full house [tripRank, pairRank])
};

// Create canonical HandClass for a 5-card hand
HandClass classify5(const array<int,5>& hand) {
    HandClass hc;
    // isFlush?
    bool flush = true;
    int s0 = cardSuit(hand[0]);
    for(int i=1;i<5;++i) if(cardSuit(hand[i]) != s0) { flush=false; break; }

    int rankMask = rankBitmask(hand);
    int straightTop = detectStraightTop(rankMask);

    auto countsArr = rankCounts(hand);
    // Build frequency buckets: map count -> list of ranks
    vector<int> quads, trips, pairs, singles;
    for(int r=14; r>=2; --r) {
        int c = countsArr[r];
        if(c==4) quads.push_back(r);
        else if(c==3) trips.push_back(r);
        else if(c==2) pairs.push_back(r);
        else if(c==1) singles.push_back(r);
    }

    if(straightTop && flush) {
        hc.category = CAT_STRAIGHT_FLUSH;
        hc.kickers = {straightTop}; // highest card in straight flush
        return hc;
    }
    if(!quads.empty()) {
        hc.category = CAT_FOUR_KIND;
        int quad = quads[0];
        int kicker = -1;
        for(int r=14;r>=2;--r) if(countsArr[r]==1) { kicker=r; break; }
        hc.kickers = {quad, kicker};
        return hc;
    }
    if(!trips.empty() && !pairs.empty()) {
        hc.category = CAT_FULL_HOUSE;
        hc.kickers = {trips[0], pairs[0]}; // trip rank, pair rank
        return hc;
    }
    if(!trips.empty() && pairs.size()>=1) { // rare if trips and multiple pairs? covered above
        hc.category = CAT_FULL_HOUSE;
        hc.kickers = {trips[0], pairs[0]};
        return hc;
    }
    if(flush) {
        hc.category = CAT_FLUSH;
        // flush tiebreakers: ordered ranks descending
        auto rs = ranksSortedDesc(hand);
        hc.kickers = rs;
        return hc;
    }
    if(straightTop) {
        hc.category = CAT_STRAIGHT;
        hc.kickers = {straightTop};
        return hc;
    }
    if(!trips.empty()) {
        hc.category = CAT_THREE_KIND;
        int trip = trips[0];
        vector<int> rest;
        for(int r=14;r>=2;--r) if(countsArr[r]==1) rest.push_back(r);
        hc.kickers = {trip};
        hc.kickers.insert(hc.kickers.end(), rest.begin(), rest.end()); // 2 kickers
        return hc;
    }
    if(pairs.size()>=2) {
        hc.category = CAT_TWO_PAIR;
        // pairs already ordered descending
        int highPair = pairs[0];
        int lowPair = pairs[1];
        int kicker = -1;
        for(int r=14;r>=2;--r) if(countsArr[r]==1){ kicker=r; break;}
        hc.kickers = {highPair, lowPair, kicker};
        return hc;
    }
    if(pairs.size()==1) {
        hc.category = CAT_ONE_PAIR;
        int pair = pairs[0];
        vector<int> rest;
        for(int r=14;r>=2;--r) if(countsArr[r]==1) rest.push_back(r);
        hc.kickers = {pair};
        hc.kickers.insert(hc.kickers.end(), rest.begin(), rest.end()); // 3 kickers
        return hc;
    }
    // High card
    hc.category = CAT_HIGH_CARD;
    auto rs = ranksSortedDesc(hand);
    hc.kickers = rs;
    return hc;
}

// Convert HandClass to a unique string key for maps/sets
string handClassKey(const HandClass& hc) {
    string s = to_string(hc.category) + "|";
    for(size_t i=0;i<hc.kickers.size();++i){
        if(i) s.push_back(',');
        s += to_string(hc.kickers[i]);
    }
    return s;
}

// Comparator for HandClass: returns true if a is better (should come earlier)
bool handClassBetter(const HandClass& a, const HandClass& b) {
    if(a.category != b.category) return a.category < b.category; // smaller category number = better
    // compare kickers lexicographically
    const auto &A = a.kickers, &B = b.kickers;
    size_t n = max(A.size(), B.size());
    for(size_t i=0;i<n;++i){
        int av = (i < A.size() ? A[i] : -1);
        int bv = (i < B.size() ? B[i] : -1);
        if(av != bv) return av > bv; // larger kicker = better
    }
    return false; // equal
}

/* ------------------------------------------------------------------
   SECTION D — Build canonical table of all distinct 5-card hand classes
   Output: vector<HandClass> canonicalClasses sorted best->worst,
           and map key->index (1..N)
   ------------------------------------------------------------------ */

struct CanonTable {
    vector<HandClass> classes;               // sorted best->worst
    unordered_map<string,int> keyToIndex;    // mapping key -> 1..N

    // Build by enumerating all C(52,5) combinations (2,598,960)
    void build() {
        cout << "Building canonical 5-card hand table (this may take a few seconds)...\n";
        // Generate deck (we need numeric cards to iterate combos)
        vector<int> deck; deck.reserve(52);
        for(int s=0;s<4;++s) for(int r=2;r<=14;++r) deck.push_back(encodeCard(r,s));
        const int N = deck.size(); // 52

        // Use unordered_set<string> to gather unique keys
        unordered_set<string> uniqueKeys;
        uniqueKeys.reserve(8000);

        array<int,5> hand;
        // iterate combinations i<j<k<l<m
        for(int i=0;i<N-4;++i){
            hand[0] = deck[i];
            for(int j=i+1;j<N-3;++j){
                hand[1] = deck[j];
                for(int k=j+1;k<N-2;++k){
                    hand[2] = deck[k];
                    for(int l=k+1;l<N-1;++l){
                        hand[3] = deck[l];
                        for(int m=l+1;m<N;++m){
                            hand[4] = deck[m];
                            HandClass hc = classify5(hand);
                            string key = handClassKey(hc);
                            uniqueKeys.insert(key);
                        }
                    }
                }
            }
        }

        // Now move keys into vector<HandClass>
        classes.clear();
        classes.reserve(uniqueKeys.size());
        for(const auto &k : uniqueKeys) {
            // parse back into HandClass
            // key format: category|t1,t2,...
            HandClass hc;
            auto pos = k.find('|');
            hc.category = stoi(k.substr(0,pos));
            string rest = k.substr(pos+1);
            hc.kickers.clear();
            if(!rest.empty()) {
                stringstream ss(rest);
                string tok;
                while(getline(ss, tok, ',')) hc.kickers.push_back(stoi(tok));
            }
            classes.push_back(hc);
        }

        // Sort by strength best->worst using comparator
        sort(classes.begin(), classes.end(), [](const HandClass& a, const HandClass& b){
            return handClassBetter(a,b);
        });

        // Assign indices starting at 1
        keyToIndex.clear();
        for(size_t idx=0; idx<classes.size(); ++idx) {
            string key = handClassKey(classes[idx]);
            keyToIndex[key] = (int)idx + 1;
        }
        cout << "Canonical table built. Distinct classes: " << classes.size() << "\n";
    }

    // Lookup index for a HandClass
    int lookup(const HandClass& hc) const {
        string key = handClassKey(hc);
        auto it = keyToIndex.find(key);
        if(it == keyToIndex.end()) return -1;
        return it->second;
    }
};

/* ------------------------------------------------------------------
   SECTION E — Evaluate best 5-card class out of 7 cards, return index 1..N
   ------------------------------------------------------------------ */

// Evaluate best five-card HandClass for a 7-card vector and return the canonical index
int evaluate7_bestIndex(const vector<int>& cards7, const CanonTable& table) {
    array<int,5> combo;
    HandClass bestHC;
    bool haveBest = false;

    // iterate all 21 combinations
    for(int a=0;a<7;a++)
    for(int b=a+1;b<7;b++)
    for(int c=b+1;c<7;c++)
    for(int d=c+1;d<7;d++)
    for(int e=d+1;e<7;e++){
        combo = {cards7[a],cards7[b],cards7[c],cards7[d],cards7[e]};
        HandClass hc = classify5(combo);
        if(!haveBest || handClassBetter(hc, bestHC)) {
            bestHC = hc;
            haveBest = true;
        }
    }
    int idx = table.lookup(bestHC);
    return idx; // 1..7462
}

// For human readable category name from HandClass category
string categoryName(int cat) {
    switch(cat) {
        case CAT_STRAIGHT_FLUSH: return "Straight Flush";
        case CAT_FOUR_KIND:      return "Four of a Kind";
        case CAT_FULL_HOUSE:     return "Full House";
        case CAT_FLUSH:          return "Flush";
        case CAT_STRAIGHT:       return "Straight";
        case CAT_THREE_KIND:     return "Three of a Kind";
        case CAT_TWO_PAIR:       return "Two Pair";
        case CAT_ONE_PAIR:       return "One Pair";
        case CAT_HIGH_CARD:      return "High Card";
        default:                 return "Unknown";
    }
}

/* ------------------------------------------------------------------
   SECTION F — Simple legal Limit betting logic per street (2 players, no fold)
   - This prints every action and enforces that after a bet, only call/raise allowed
   - Raises per street limited to MAX_RAISES
   ------------------------------------------------------------------ */

static random_device rd;
static mt19937 rng(rd());

enum Action { A_CHECK, A_BET, A_CALL, A_RAISE, A_FOLD };

Action pickRandom(const vector<Action>& allowed) {
    uniform_int_distribution<int> dist(0, (int)allowed.size()-1);
    return allowed[dist(rng)];
}

string actionStr(Action a) {
    switch(a) {
        case A_CHECK: return "check";
        case A_BET:   return "bet";
        case A_CALL:  return "call";
        case A_RAISE: return "raise";
       case  A_FOLD:  return  "fold";
    }
    return "?";
}

// Play a street and print every action. We do not track chips/pot;
// we only ensure legal action flow. firstPlayer is 1 or 2 starting actor.
void playStreetLog(const string& streetName, int firstPlayer) {
    cout << "\n-- " << streetName << " --\n";
    bool hasBet = false;
    int raises = 0;
    const int MAX_RAISES = 4;
    bool finished = false;
    int current = firstPlayer;
    int actionCount = 0;
    int pot = 0
    int firstPlayerChips = 0
    int secondPlayerChips = 0

    while(!finished) {
        vector<Action> allowed;
        if(!hasBet) allowed = {A_CHECK, A_BET};
        else {
            if(raises < MAX_RAISES) allowed = {A_CALL, A_RAISE, A_FOLD};
            else allowed = {A_CALL, A_FOLD};
        }
        Action pick = pickRandom(allowed);
        cout << "Player " << current << ": " << actionStr(pick) << "\n";

        if(pick == A_BET) {
            hasBet = true;
            raises = 1; // first bet counts as a single bet/raise
            pot = pot + 20
        } else if(pick == A_RAISE) {
            if (actionCount >= 1){ 
               pot = pot + 40
            }

            hasBet = true;
            ++raises;
        } else if(pick == A_CALL) {
            if (actionCount >= 1){ 
               pot = pot + 20
            }
            // call completes and ends the street
            finished = true;
        } else if(pick == A_CHECK) {
            // If both players checked in sequence, end street
            if(actionCount > 0 && current != firstPlayer) finished = true;
        } else if(pick == A_FOLD) {
            // If both players checked in sequence, end street
            if (current != firstPlayer) {
               firstPlayerChips = firstPlayerChips + pot
            } else {
               secondPlayerChips = secondPlayerChips + pot
            }
           
            finished = true;
        }

        // prepare next actor
        current = (current==1 ? 2 : 1);
        ++actionCount;
        if(actionCount > 12) { // safety net
            finished = true;
        }
    }
   return (pot,firstPlayerChips,secondPlayerChips,pick)
}

/* ------------------------------------------------------------------
   SECTION G — Simulation: play n hands (we will do 3)
   Each hand prints hole cards, each street with actions, final board,
   and final showdown with both players' best-class index (1..7462).
   ------------------------------------------------------------------ */

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    // Build canonical table once
    CanonTable table;
    table.build();
    // Sanity check: number of classes should be 7462
    cout << "Expect 7462 distinct classes. Found: " << table.classes.size() << "\n";

    // Simulate 3 hands
    const int NUM_HANDS = 3;
    for(int hnum=1; hnum<=NUM_HANDS; ++hnum) {
        cout << "\n==================================================\n";
        cout << "HAND #" << hnum << "\n";

        Deck deck;
        deck.reset();
        deck.shuffle();
        chips1 = 0
        chips2 = 0

        vector<int> p1 = { deck.deal(), deck.deal(), chips1 };
        vector<int> p2 = { deck.deal(), deck.deal(), chips2 };
        vector<int> board = { deck.deal(), deck.deal(), deck.deal(), deck.deal(), deck.deal() };

        cout << "Player 1: " << cardToString(p1[0]) << ", " << cardToString(p1[1]) << "\n";
        cout << "Player 2: " << cardToString(p2[0]) << ", " << cardToString(p2[1]) << "\n";

        // Preflop (player 1 acts first)
        playStreetLog("Preflop", 1);

        // Flop
        cout << "Flop: " << cardToString(board[0]) << ", " << cardToString(board[1]) << ", " << cardToString(board[2]) << "\n";
        playStreetLog("Flop", 1);

        // Turn
        cout << "Turn: " << cardToString(board[3]) << "\n";
        playStreetLog("Turn", 1);

        // River
        cout << "River: " << cardToString(board[4]) << "\n";
        playStreetLog("River", 1);

        // Showdown: evaluate both players' best 5-card class from 7 cards
        vector<int> all1 = p1; all1.insert(all1.end(), board.begin(), board.end());
        vector<int> all2 = p2; all2.insert(all2.end(), board.begin(), board.end());
        int idx1 = evaluate7_bestIndex(all1, table);
        int idx2 = evaluate7_bestIndex(all2, table);

        // For user-friendliness also compute the HandClass to print category
        // We re-evaluate best HandClass (we could modify evaluate7_bestIndex to return it)
        HandClass bestHC1, bestHC2;
        bool hb1=false, hb2=false;
        array<int,5> combo;
        for(int a=0;a<7;a++) for(int b=a+1;b<7;b++) for(int c=b+1;c<7;c++)
        for(int d=c+1;d<7;d++) for(int e=d+1;e<7;e++){
            combo = { all1[a], all1[b], all1[c], all1[d], all1[e] };
            HandClass hc = classify5(combo);
            if(!hb1 || handClassBetter(hc, bestHC1)) { bestHC1 = hc; hb1=true; }
            combo = { all2[a], all2[b], all2[c], all2[d], all2[e] };
            HandClass hc2 = classify5(combo);
            if(!hb2 || handClassBetter(hc2, bestHC2)) { bestHC2 = hc2; hb2=true; }
        }

        cout << "\n-- Showdown --\n";
        cout << "Board: " << cardToString(board[0]) << ", " << cardToString(board[1]) << ", "
             << cardToString(board[2]) << ", " << cardToString(board[3]) << ", " << cardToString(board[4]) << "\n\n";

        cout << "Player 1: " << cardToString(p1[0]) << ", " << cardToString(p1[1]) << "\n";
        cout << "  Category: " << categoryName(bestHC1.category)
             << "  Index: " << idx1 << " (1=best, 7462=worst)\n";

        cout << "Player 2: " << cardToString(p2[0]) << ", " << cardToString(p2[1]) << "\n";
        cout << "  Category: " << categoryName(bestHC2.category)
             << "  Index: " << idx2 << " (1=best, 7462=worst)\n";

        if(idx1 < idx2) cout << "Result: Player 1 wins (lower index = better)\n";
        else if(idx2 < idx1) cout << "Result: Player 2 wins\n";
        else cout << "Result: Tie (equal index)\n";
    }

    cout << "\nSimulation complete.\n";
    return 0;
}
