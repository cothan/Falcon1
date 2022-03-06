#include "ntt_consts.h"

#define PADDING 0

extern const int16_t qmvq[8] = {FALCON_Q, FALCON_Q,
                                FALCON_MONT, FALCON_MONT,
                                FALCON_V, FALCON_V,
                                FALCON_MONT_QINV, FALCON_MONT_QINV};

#if FALCON_LOGN == 9
// ❯ python pattern_fwd.py 512 >  ntt_consts9.c
// ❯ python pattern_inv.py 512 >>  ntt_consts9.c
#include "ntt_consts9.c"

#elif FALCON_LOGN == 10
// ❯ python pattern_fwd.py 1024 >  ntt_consts10.c
// ❯ python pattern_inv.py 1024 >>  ntt_consts10.c
#include "ntt_consts10.c"

#else
#error "Only support falcon_logn = 9,10"

#endif
