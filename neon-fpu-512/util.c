/*
 * Utils function
 *
 * =============================================================================
 * Copyright (c) 2021 by Cryptographic Engineering Research Group (CERG)
 * ECE Department, George Mason University
 * Fairfax, VA, U.S.A.
 * Author: Duc Tri Nguyen
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * =============================================================================
 * @author   Duc Tri Nguyen <dnguye69@gmu.edu>
 */


#include "inner.h"
#include "macrofx4.h"
#include <stdio.h>

/*
 * Convert an integer polynomial (with small values) into the
 * representation with complex numbers.
 * IMPORTANT: Correct, verified, optimized.
 */
void smallints_to_fpr(fpr *r, const int8_t *t, const unsigned logn)
{
    float64x2x4_t neon_flo64, neon_fhi64;
    int64x2x4_t neon_lo64, neon_hi64;
    int32x4_t neon_lo32[2], neon_hi32[2];
    int16x8_t neon_lo16, neon_hi16;
    int8x16_t neon_8;

    const unsigned falcon_n =  1 << logn;

    for (unsigned i = 0; i < falcon_n; i += 16)
    {
        neon_8 = vld1q_s8(&t[i]);

        // Extend from 8 to 16 bit
        // x7 | x6 | x5 | x5 - x3 | x2 | x1 | x0
        neon_lo16 = vshll_n_s8(vget_low_s8(neon_8), 0);
        neon_hi16 = vshll_high_n_s8(neon_8, 0);

        // Extend from 16 to 32 bit
        // xxx3 | xxx2 | xxx1 | xxx0
        neon_lo32[0] = vshll_n_s16(vget_low_s16(neon_lo16), 0);
        neon_lo32[1] = vshll_high_n_s16(neon_lo16, 0);
        neon_hi32[0] = vshll_n_s16(vget_low_s16(neon_hi16), 0);
        neon_hi32[1] = vshll_high_n_s16(neon_hi16, 0);

        // Extend from 32 to 64 bit
        neon_lo64.val[0] = vshll_n_s32(vget_low_s32(neon_lo32[0]), 0);
        neon_lo64.val[1] = vshll_high_n_s32(neon_lo32[0], 0);
        neon_lo64.val[2] = vshll_n_s32(vget_low_s32(neon_lo32[1]), 0);
        neon_lo64.val[3] = vshll_high_n_s32(neon_lo32[1], 0);

        neon_hi64.val[0] = vshll_n_s32(vget_low_s32(neon_hi32[0]), 0);
        neon_hi64.val[1] = vshll_high_n_s32(neon_hi32[0], 0);
        neon_hi64.val[2] = vshll_n_s32(vget_low_s32(neon_hi32[1]), 0);
        neon_hi64.val[3] = vshll_high_n_s32(neon_hi32[1], 0);

        vfcvtx4(neon_flo64, neon_lo64);
        vfcvtx4(neon_fhi64, neon_hi64);

        vstorex4(&r[i], neon_flo64);
        vstorex4(&r[i + 8], neon_fhi64);
    }
}

void print_farray(fpr *r, unsigned logn, const char *string)
{
    printf("%s:\n", string);
    const unsigned n = 1 << logn;
    for (unsigned i = 0; i < n; i++)
    {
        // printf("[%3d]:%.20f\n", i, r[i]);
        // printf("[%3d]:%f\n", i, r[i]);
        printf("%.10f, ", r[i]);
    }
    printf("\n");
    fflush(stdout); 
}

void print_iarray(int8_t *a, const char* string)
{
    printf("%s:\n", string);
    for (int i =0; i < 512; i++)
    {
        printf("%d, ", a[i] & 0xff);
    }
    printf("\n");
    fflush(stdout);
}
