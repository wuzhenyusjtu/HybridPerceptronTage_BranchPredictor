#ifndef _PREDICTOR_H_
#define _PREDICTOR_H_

#include "utils.h"
#include "tracer.h"
#include <cmath>
#include <cstdlib>
#include <stdint.h>
#include <bitset>
/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////
class PerceptronPredictor;
class TagePredictor;

class PREDICTOR{
private:
    UINT64 GHR;           // global history register
    static const UINT32 index_bits_hybrid = 8;
    
    uint8_t HybridTable[1 << index_bits_hybrid];
    
    UINT32 PCmask_hybrid;
    
    
    bool prediction_perceptron;
    bool prediction_tage;
    PerceptronPredictor *_perceptron;
    TagePredictor *_tage;
    
public:
    // The interface to the four functions below CAN NOT be changed
    PREDICTOR(void);
    ~PREDICTOR(void);
    bool    GetPrediction(UINT32 PC);
    void    UpdatePredictor(UINT32 PC, bool resolveDir, bool predDir, UINT32 branchTarget);
    void    TrackOtherInst(UINT32 PC, OpType opType, UINT32 branchTarget);
};


class PerceptronPredictor{
private:
    // # perceptrons.
    static const int kSize = 512;
    // The size of the global history shift register (implemented as a circular buffer)
    static const int kHistorySize = 63;
    // The theta value used as the threshold for determining weight saturation.
    int kTheta;
    // The number of bits each weight can use.
    int kWeightSize;
    int w_max;
    int w_min;
    
    UINT64 GHR;
    
    int _y_out;
    // The bias, i.e., w_0 for each perceptron.
    int8_t *_bias;
    
    // The array of perceptrons.
    int8_t *_weights;
    
    inline int get_key(UINT32 pc) {
        return ((pc >> 2) ^ GHR) % kSize;
    }

    inline int sign(int val) {
        return (val > 0) - (val < 0);
    }

public:
    // The interface to the four functions below CAN NOT be changed
    PerceptronPredictor(void);
    ~PerceptronPredictor(void);
    // Computes the y value of the perceptron with the given key.
    bool    GetPrediction(UINT32 PC);
    // Trains the perceptron associated with PC, given its previously-computed
    // y value as well as t, denoting whether or not the branch was taken.
    void    UpdatePredictor(UINT32 PC, bool resolveDir, bool predDir, UINT32 branchTarget);
    void    UpdateGHR(UINT64 GHR);
};


#define LOG_BASE 13
#define LOG_GLOBAL 12
#define N_BANKS 4
#define CTR_BITS 3
#define TAG_BITS 11

#define MAX_LENGTH 131
#define MIN_LENGTH 3

/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////

struct base_entry {
    int pred;
    base_entry(): pred(0) {}
};

struct global_entry {
    int ctr, tag, ubit;
    global_entry(): tag(0), ubit(random() & 3) {
        ctr = (random() & ((1 << CTR_BITS) - 1)) - (1 << (CTR_BITS - 1));
    }
};

struct folded_history {
    unsigned hash;
    int MOD, ORIGIN_LEN, COMPRESSED_LEN;
    
    void create(int origin_len, int compressed_len) {
        hash = 0;
        ORIGIN_LEN = origin_len;
        COMPRESSED_LEN = compressed_len;
        MOD = ORIGIN_LEN % COMPRESSED_LEN;
    }
    
    void update(bitset<MAX_LENGTH> h) {
        hash = (hash << 1) | h[0];
        hash ^= h[ORIGIN_LEN] << MOD;
        hash ^= (hash >> COMPRESSED_LEN);
        hash &= (1 << COMPRESSED_LEN) - 1;
    }
};

class TagePredictor{
    
    // The state is defined for Gshare, change for your design
    
private:
    global_entry global_table[N_BANKS][1 << LOG_GLOBAL];
    base_entry base_table[1 << LOG_BASE];
    
    folded_history comp_hist_i[N_BANKS], comp_hist_t[2][N_BANKS];
    bitset<MAX_LENGTH> global_history;
    
    int path_history;
    int G_INDEX[N_BANKS], B_INDEX;
    int lens[N_BANKS];
    int bank, alt_bank;
    
    int pred_store;
    bool alt_pred;
    
    
    int get_b_index(UINT32 PC) {
        return PC & ((1 << LOG_BASE) - 1);
    }
    
    int get_g_index(UINT32 PC, int bank) {
        int index = PC ^
        (PC >> ((LOG_GLOBAL - N_BANKS + bank + 1))) ^
        comp_hist_i[bank].hash;
        if (lens[bank] >= 16)
            index ^= mix_func(path_history, 16, bank);
        else
            index ^= mix_func(path_history, lens[bank], bank);
        return index & ((1 << LOG_GLOBAL) - 1);
    }
    
    int g_tag(UINT32 PC, int bank) {
        int temp_tag = PC ^ comp_hist_t[0][bank].hash ^ (comp_hist_t[1][bank].hash << 1);
        return temp_tag & ((1 << (TAG_BITS - ((bank + (N_BANKS & 1)) / 2))) - 1);
    }
    
    int mix_func(int hist, int size, int bank) {
        hist = hist & ((1 << size) - 1);
        int temp_2 = hist >> LOG_GLOBAL;
        temp_2 = ((temp_2 << bank) & ((1 << LOG_GLOBAL) - 1)) + (temp_2 >> (LOG_GLOBAL - bank));
        int temp_1 = hist & ((1 << LOG_GLOBAL) - 1);
        hist = temp_1 ^ temp_2;
        return ((hist << bank) & ((1 << LOG_GLOBAL) - 1)) + (hist >> (LOG_GLOBAL - bank));
    }
    
    void update_base(UINT32 PC, bool taken) {
        if (taken) {
            if (base_table[B_INDEX].pred < 1)
                base_table[B_INDEX].pred ++;
        }
        else {
            if (base_table[B_INDEX].pred > - 2)
                base_table[B_INDEX].pred --;
        }
    }
    
    void update_ctr(int &ctr, bool taken, int bits) {
        if (taken) {
            if (ctr < ((1 << (bits - 1)) - 1))
                ctr ++;
        }
        else {
            if (ctr > - (1 << (bits - 1)))
                ctr --;
        }
    }
    
    void alloc_new_hist(bool taken, UINT32 PC) {
        int minu = 3, index = 0;
        for (int i = 0; i < bank; i ++) {
            if (global_table[i][G_INDEX[i]].ubit < minu) {
                minu = global_table[i][G_INDEX[i]].ubit;
                index = i;
            }
        }
        if (minu > 0) {
            for (int i = 0; i < bank; i ++) {
                global_table[i][G_INDEX[i]].ubit --;
            }
        }
        else {
            global_table[index][G_INDEX[index]].ctr = taken ? 0 : -1;
            global_table[index][G_INDEX[index]].tag = g_tag(PC, index);
            global_table[index][G_INDEX[index]].ubit = 0;
        }
    }
    
    void update_hist(bool taken, UINT32 PC) {
        path_history = (path_history << 1) + (PC & 1);
        path_history &= (1 << 10) - 1;
        global_history <<= 1;
        if (taken)
            global_history = global_history | (bitset<MAX_LENGTH>)1;
        for (int i = 0; i < N_BANKS; i ++) {
            comp_hist_t[0][i].update(global_history);
            comp_hist_t[1][i].update(global_history);
            comp_hist_i[i].update(global_history);
        }
    }
    
    void calc_index(UINT32 PC) {
        B_INDEX = get_b_index(PC);
        for (int i = 0; i < N_BANKS; i ++) {
            G_INDEX[i] = get_g_index(PC, i);
        }
    }
    
    void linear_search(UINT32 PC) {
        bank = alt_bank = N_BANKS;
        for (int i = 0; i < N_BANKS; i ++) {
            if (global_table[i][G_INDEX[i]].tag == g_tag(PC, i)) {
                bank = i;
                break;
            }
        }
        for (int i = bank + 1; i < N_BANKS; i ++) {
            if (global_table[i][G_INDEX[i]].tag == g_tag(PC, i)) {
                alt_bank = i;
                break;
            }
        }
    }
    
    bool get_base_pred(UINT32 PC) {
        return base_table[B_INDEX].pred >= 0;
    }
    
    
public:
    
    // The interface to the four functions below CAN NOT be changed
    
    TagePredictor(void);
    bool    GetPrediction(UINT32 PC);
    void    UpdatePredictor(UINT32 PC, bool resolveDir, bool predDir, UINT32 branchTarget);
    void    UpdateGHR(UINT64 GHR);

    // Contestants can define their own functions below
    
};

#endif

