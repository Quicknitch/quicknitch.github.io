/**
 * wake_word.h
 * Wake Word Detection Engine interface
 */
#ifndef WAKE_WORD_H
#define WAKE_WORD_H

#include "quicknitch.h"

typedef enum {
    WW_RESULT_NONE        = 0,
    WW_RESULT_HEY_NITCH   = 1,
    WW_RESULT_QUICKNITCH  = 2,
} ww_result_t;

void        wake_word_init(void);
ww_result_t wake_word_process_audio(void);
void        wake_word_build_i2c_msg(ww_result_t result, uint8_t conf_pct,
                                    qn_i2c_msg_t *out);

#endif /* WAKE_WORD_H */
