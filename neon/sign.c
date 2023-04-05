/*
 * Falcon signature generation.
 *
 * ==========================(LICENSE BEGIN)============================
 *
 * Copyright (c) 2017-2019  Falcon Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * ===========================(LICENSE END)=============================
 *
 * @author   Thomas Pornin <thomas.pornin@nccgroup.com>
 */

#include "inner.h"
#include "macrofx4.h"
#include "macrof.h"
#include <arm_neon.h>
#include "util.h"
#include <stdio.h>
/* =================================================================== */

/*
 * Compute degree N from logarithm 'logn'.
 */
#define MKN(logn)   ((size_t)1 << (logn))

/* =================================================================== */
/*
 * Binary case:
 *   N = 2^logn
 *   phi = X^N+1
 */

/*
 * Get the size of the LDL tree for an input with polynomials of size
 * 2^logn. The size is expressed in the number of elements.
 */
static inline unsigned
ffLDL_treesize(unsigned logn)
{
	/*
	 * For logn = 0 (polynomials are constant), the "tree" is a
	 * single element. Otherwise, the tree node has size 2^logn, and
	 * has two child trees for size logn-1 each. Thus, treesize s()
	 * must fulfill these two relations:
	 *
	 *   s(0) = 1
	 *   s(logn) = (2^logn) + 2*s(logn-1)
	 */
	return (logn + 1) << logn;
}

/*
 * Inner function for ffLDL_fft(). It expects the matrix to be both
 * auto-adjoint and quasicyclic; also, it uses the source operands
 * as modifiable temporaries.
 *
 * tmp[] must have room for at least one polynomial.
 */
static void
ffLDL_fft_inner(fpr *restrict tree,
	fpr *restrict g0, fpr *restrict g1, unsigned logn, fpr *restrict tmp)
{
	size_t n, hn;

	n = MKN(logn);
	if (n == 1) {
		tree[0] = g0[0];
		return;
	}
	hn = n >> 1;

	/*
	 * The LDL decomposition yields L (which is written in the tree)
	 * and the diagonal of D. Since d00 = g0, we just write d11
	 * into tmp.
	 */
	ZfN(poly_LDLmv_fft)(tmp, tree, g0, g1, g0, logn);

	/*
	 * Split d00 (currently in g0) and d11 (currently in tmp). We
	 * reuse g0 and g1 as temporary storage spaces:
	 *   d00 splits into g1, g1+hn
	 *   d11 splits into g0, g0+hn
	 */
	ZfN(poly_split_fft)(g1, g1 + hn, g0, logn);
	ZfN(poly_split_fft)(g0, g0 + hn, tmp, logn);

	/*
	 * Each split result is the first row of a new auto-adjoint
	 * quasicyclic matrix for the next recursive step.
	 */
	ffLDL_fft_inner(tree + n,
		g1, g1 + hn, logn - 1, tmp);
	ffLDL_fft_inner(tree + n + ffLDL_treesize(logn - 1),
		g0, g0 + hn, logn - 1, tmp);
}

/*
 * Compute the ffLDL tree of an auto-adjoint matrix G. The matrix
 * is provided as three polynomials (FFT representation).
 *
 * The "tree" array is filled with the computed tree, of size
 * (logn+1)*(2^logn) elements (see ffLDL_treesize()).
 *
 * Input arrays MUST NOT overlap, except possibly the three unmodified
 * arrays g00, g01 and g11. tmp[] should have room for at least three
 * polynomials of 2^logn elements each.
 */
static void
ffLDL_fft(fpr *restrict tree, const fpr *restrict g00,
	const fpr *restrict g01, const fpr *restrict g11,
	unsigned logn, fpr *restrict tmp)
{
	size_t n, hn;
	fpr *d00, *d11;

	n = MKN(logn);
	if (n == 1) {
		tree[0] = g00[0];
		return;
	}
	hn = n >> 1;
	d00 = tmp;
	d11 = tmp + n;
	tmp += n << 1;

	memcpy(d00, g00, n * sizeof *g00);
	ZfN(poly_LDLmv_fft)(d11, tree, g00, g01, g11, logn);
	ZfN(poly_split_fft)(tmp, tmp + hn, d00, logn);
	ZfN(poly_split_fft)(d00, d00 + hn, d11, logn);    
	memcpy(d11, tmp, n * sizeof *tmp);

	ffLDL_fft_inner(tree + n, d11, d11 + hn, logn - 1, tmp);
	ffLDL_fft_inner(tree + n + ffLDL_treesize(logn - 1), d00, d00 + hn, logn - 1, tmp);

}

/*
 * Normalize an ffLDL tree: each leaf of value x is replaced with
 * sigma / sqrt(x).
 */
static void
ffLDL_binary_normalize(fpr *tree, unsigned orig_logn, unsigned logn)
{
	/*
	 * TODO: make an iterative version.
	 */
	size_t n;

	n = MKN(logn);
	if (n == 1) {
		/*
		 * We actually store in the tree leaf the inverse of
		 * the value mandated by the specification: this
		 * saves a division both here and in the sampler.
		 */
#if FALCON_LOGN == 9
        tree[0] = fpr_mul(fpr_sqrt(tree[0]), fpr_inv_sigma_9);
#elif FALCON_LOGN == 10
        tree[0] = fpr_mul(fpr_sqrt(tree[0]), fpr_inv_sigma_10);
#endif
	} else {
		ffLDL_binary_normalize(tree + n, orig_logn, logn - 1);
		ffLDL_binary_normalize(tree + n + ffLDL_treesize(logn - 1),
			orig_logn, logn - 1);
	}
}

/* =================================================================== */

/*
 * The expanded private key contains:
 *  - The B0 matrix (four elements)
 *  - The ffLDL tree
 */

static inline size_t
skoff_b00(unsigned logn)
{
	(void)logn;
	return 0;
}

static inline size_t
skoff_b01(unsigned logn)
{
	return MKN(logn);
}

static inline size_t
skoff_b10(unsigned logn)
{
	return 2 * MKN(logn);
}

static inline size_t
skoff_b11(unsigned logn)
{
	return 3 * MKN(logn);
}

static inline size_t
skoff_tree(unsigned logn)
{
	return 4 * MKN(logn);
}

/* see inner.h */
void
Zf(expand_privkey)(fpr *restrict expanded_key,
	const int8_t *f, const int8_t *g,
	const int8_t *F, const int8_t *G,
	uint8_t *restrict tmp)
{
	fpr *rf, *rg, *rF, *rG;
	fpr *b00, *b01, *b10, *b11;
	fpr *g00, *g01, *g11, *gxx;
	fpr *tree;

	b00 = expanded_key + skoff_b00(FALCON_LOGN);
	b01 = expanded_key + skoff_b01(FALCON_LOGN);
	b10 = expanded_key + skoff_b10(FALCON_LOGN);
	b11 = expanded_key + skoff_b11(FALCON_LOGN);
	tree = expanded_key + skoff_tree(FALCON_LOGN);

	/*
	 * We load the private key elements directly into the B0 matrix,
	 * since B0 = [[g, -f], [G, -F]].
	 */
	rg = b00;
	rf = b01;
	rG = b10;
	rF = b11;

	smallints_to_fpr(rg, g, FALCON_LOGN);
    ZfN(FFT)(rg, FALCON_LOGN);

	smallints_to_fpr(rf, f, FALCON_LOGN);
	ZfN(FFT)(rf, FALCON_LOGN);
    ZfN(poly_neg)(rf, rf, FALCON_LOGN);

	smallints_to_fpr(rG, G, FALCON_LOGN);
	ZfN(FFT)(rG, FALCON_LOGN);

	smallints_to_fpr(rF, F, FALCON_LOGN);
	ZfN(FFT)(rF, FALCON_LOGN);
    ZfN(poly_neg)(rF, rF, FALCON_LOGN);

	/*
	 * Compute the FFT for the key elements, and negate f and F.
	 */

	/*
	 * The Gram matrix is G = B·B*. Formulas are:
	 *   g00 = b00*adj(b00) + b01*adj(b01)
	 *   g01 = b00*adj(b10) + b01*adj(b11)
	 *   g10 = b10*adj(b00) + b11*adj(b01)
	 *   g11 = b10*adj(b10) + b11*adj(b11)
	 *
	 * For historical reasons, this implementation uses
	 * g00, g01 and g11 (upper triangle).
	 */
	g00 = (fpr *)tmp;
	g01 = g00 + FALCON_N;
	g11 = g01 + FALCON_N;
	gxx = g11 + FALCON_N;

    ZfN(poly_mulselfadj_fft)(g00, b00, FALCON_LOGN);
    ZfN(poly_mulselfadj_add_fft)(g00, g00, b01, FALCON_LOGN);
        
    ZfN(poly_muladj_fft)(g01, b00, b10, FALCON_LOGN);
    ZfN(poly_muladj_add_fft)(g01, g01, b01, b11, FALCON_LOGN);
    
    ZfN(poly_mulselfadj_fft)(g11, b10, FALCON_LOGN);
    ZfN(poly_mulselfadj_add_fft)(g11, g11, b11, FALCON_LOGN);
    
    /*
	 * Compute the Falcon tree.
	 */
	ffLDL_fft(tree, g00, g01, g11, FALCON_LOGN, gxx);

	/*
	 * Normalize tree.
	 */
	ffLDL_binary_normalize(tree, FALCON_LOGN, FALCON_LOGN);
}

typedef int (*samplerZ)(void *ctx, fpr mu, fpr sigma);

/*
 * Perform Fast Fourier Sampling for target vector t. The Gram matrix
 * is provided (G = [[g00, g01], [adj(g01), g11]]). The sampled vector
 * is written over (t0,t1). The Gram matrix is modified as well. The
 * tmp[] buffer must have room for four polynomials.
 */

/* 
 * This code is developed by Raymond K. Zhao from https://eprint.iacr.org/2023/399
 * Vectorize performance on ARMv8 remain unchanged.
 */
 typedef struct
 {
	fpr *t0; 
	fpr *t1;
	fpr *g00; 
	fpr *g01; 
	fpr *g11;
	unsigned logn; 
	fpr *tmp;
	fpr *z0; 
	fpr *z1;
 } STACK;
 

static void
ffSampling_fft_dyntree(samplerZ samp, void *samp_ctx,
	fpr *restrict t0, fpr *restrict t1,
	fpr *restrict g00, fpr *restrict g01, fpr *restrict g11,
	unsigned orig_logn, unsigned logn, fpr *tmp)
{
	size_t n, hn;
	STACK stack[orig_logn + 1];
	unsigned stack_top = 0;
	
	stack[0].t0 = t0;
	stack[0].t1 = t1;
	stack[0].g00 = g00;
	stack[0].g01 = g01;
	stack[0].g11 = g11;
	stack[0].logn = logn;
	stack[0].tmp = tmp;
	stack[0].z0 = NULL;
	stack[0].z1 = NULL;
	
	while (1)
	{
		/*
		 * Deepest level: the LDL tree leaf value is just g00 (the
		 * array has length only 1 at this point); we normalize it
		 * with regards to sigma, then use it for sampling.
		 */
		if (stack[stack_top].logn == 0) {
			fpr leaf;

			leaf = stack[stack_top].g00[0];
#if FALCON_LOGN == 9
            leaf = fpr_mul(fpr_sqrt(leaf), fpr_inv_sigma_9);
#elif FALCON_LOGN == 10
            leaf = fpr_mul(fpr_sqrt(leaf), fpr_inv_sigma_10);
#endif		
			stack[stack_top].t0[0] = fpr_of(samp(samp_ctx, stack[stack_top].t0[0], leaf));
			stack[stack_top].t1[0] = fpr_of(samp(samp_ctx, stack[stack_top].t1[0], leaf));
			
			if (stack[--stack_top].z0 == NULL)
			{
				ZfN(poly_merge_fft)(stack[stack_top].tmp + 4, stack[stack_top].z1, stack[stack_top].z1 + 1, 1);
			}
			else
			{
				ZfN(poly_merge_fft)(stack[stack_top].t0, stack[stack_top].z0, stack[stack_top].z0 + 1, 1);
			}
		}
		else
		{
			n = (size_t)1 << stack[stack_top].logn;
			hn = n >> 1;

			if (stack[stack_top].z1 == NULL)
			{
				/*
				 * Decompose G into LDL. We only need d00 (identical to g00),
				 * d11, and l10; we do that in place.
				 */
				ZfN(poly_LDL_fft)(stack[stack_top].g00, stack[stack_top].g01, stack[stack_top].g11, stack[stack_top].logn);

				/*
				 * Split d00 and d11 and expand them into half-size quasi-cyclic
				 * Gram matrices. We also save l10 in tmp[].
				 */
				ZfN(poly_split_fft)(stack[stack_top].tmp, stack[stack_top].tmp + hn, stack[stack_top].g00, stack[stack_top].logn);
				memcpy(stack[stack_top].g00, stack[stack_top].tmp, n * sizeof *tmp);
				ZfN(poly_split_fft)(stack[stack_top].tmp, stack[stack_top].tmp + hn, stack[stack_top].g11, stack[stack_top].logn);
				memcpy(stack[stack_top].g11, stack[stack_top].tmp, n * sizeof *tmp);
				memcpy(stack[stack_top].tmp, stack[stack_top].g01, n * sizeof *g01);
				memcpy(stack[stack_top].g01, stack[stack_top].g00, hn * sizeof *g00);
				memcpy(stack[stack_top].g01 + hn, stack[stack_top].g11, hn * sizeof *g00);

				/*
				 * The half-size Gram matrices for the recursive LDL tree
				 * building are now:
				 *   - left sub-tree: g00, g00+hn, g01
				 *   - right sub-tree: g11, g11+hn, g01+hn
				 * l10 is in tmp[].
				 */
				 
				/*
				 * We split t1 and use the first recursive call on the two
				 * halves, using the right sub-tree. The result is merged
				 * back into tmp + 2*n.
				 */
				stack[stack_top].z1 = stack[stack_top].tmp + n;
				ZfN(poly_split_fft)(stack[stack_top].z1, stack[stack_top].z1 + hn, stack[stack_top].t1, stack[stack_top].logn);

				stack[stack_top + 1].t0 = stack[stack_top].z1;
				stack[stack_top + 1].t1 = stack[stack_top].z1 + hn;
				stack[stack_top + 1].g00 = stack[stack_top].g11;
				stack[stack_top + 1].g01 = stack[stack_top].g11 + hn;
				stack[stack_top + 1].g11 = stack[stack_top].g01 + hn;
				stack[stack_top + 1].logn = stack[stack_top].logn - 1;
				stack[stack_top + 1].tmp = stack[stack_top].z1 + n;
				stack[stack_top + 1].z0 = NULL;
				stack[++stack_top].z1 = NULL;
			}
			else if (stack[stack_top].z0 == NULL)
			{
				/*
				 * Compute tb0 = t0 + (t1 - z1) * l10.
				 * At that point, l10 is in tmp, t1 is unmodified, and z1 is
				 * in tmp + (n << 1). The buffer in z1 is free.
				 *
				 * In the end, z1 is written over t1, and tb0 is in t0.
				 */
				memcpy(stack[stack_top].z1, stack[stack_top].t1, n * sizeof *t1);
				ZfN(poly_sub)(stack[stack_top].z1, stack[stack_top].z1, stack[stack_top].tmp + (n << 1), stack[stack_top].logn);
				memcpy(stack[stack_top].t1, stack[stack_top].tmp + (n << 1), n * sizeof *tmp);
				ZfN(poly_mul_fft)(stack[stack_top].tmp, stack[stack_top].tmp, stack[stack_top].z1, stack[stack_top].logn);
				ZfN(poly_add)(stack[stack_top].t0, stack[stack_top].t0, stack[stack_top].tmp, stack[stack_top].logn);

				/*
				 * Second recursive invocation, on the split tb0 (currently in t0)
				 * and the left sub-tree.
				 */
				stack[stack_top].z0 = stack[stack_top].tmp;
				ZfN(poly_split_fft)(stack[stack_top].z0, stack[stack_top].z0 + hn, stack[stack_top].t0, stack[stack_top].logn);

				stack[stack_top + 1].t0 = stack[stack_top].z0;
				stack[stack_top + 1].t1 = stack[stack_top].z0 + hn;
				stack[stack_top + 1].g00 = stack[stack_top].g00;
				stack[stack_top + 1].g01 = stack[stack_top].g00 + hn;
				stack[stack_top + 1].g11 = stack[stack_top].g01;
				stack[stack_top + 1].logn = stack[stack_top].logn - 1;
				stack[stack_top + 1].tmp = stack[stack_top].z0 + n;
				stack[stack_top + 1].z0 = NULL;
				stack[++stack_top].z1 = NULL;
			}
			else
			{
				if (stack[stack_top].logn == orig_logn)
				{
					return;
				}
				else
				{
					if (stack[--stack_top].z0 == NULL)
					{
						ZfN(poly_merge_fft)(stack[stack_top].tmp + (n << 2), stack[stack_top].z1, stack[stack_top].z1 + n, stack[stack_top].logn);
					}
					else
					{
						ZfN(poly_merge_fft)(stack[stack_top].t0, stack[stack_top].z0, stack[stack_top].z0 + n, stack[stack_top].logn);
					}
				}
			}
		}
	}
}



/*
 * Perform Fast Fourier Sampling for target vector t and LDL tree T.
 * tmp[] must have size for at least two polynomials of size 2^logn.
 */
static void
ffSampling_fft(samplerZ samp, void *samp_ctx,
	fpr *restrict z0, fpr *restrict z1,
	const fpr *restrict tree,
	const fpr *restrict t0, const fpr *restrict t1, unsigned logn,
	fpr *restrict tmp)
{
	size_t n, hn;
	const fpr *tree0, *tree1;

    /*
	 * When logn == 2, we inline the last two recursion levels.
	 */
	if (logn == 2) {
		fpr x0, x1, y0, y1, w0, w1, w2, w3, sigma;
		fpr a_re, a_im, b_re, b_im, c_re, c_im;

		tree0 = tree + 4;
		tree1 = tree + 8;
        
		/*
		 * We split t1 into w*, then do the recursive invocation,
		 * with output in w*. We finally merge back into z1.
		 */
        // Split
		a_re = t1[0];
		a_im = t1[2];
		b_re = t1[1];
		b_im = t1[3];
		c_re = fpr_add(a_re, b_re);
		c_im = fpr_add(a_im, b_im);
		w0 = fpr_half(c_re);
		w1 = fpr_half(c_im);
		c_re = fpr_sub(a_re, b_re);
		c_im = fpr_sub(a_im, b_im);
		w2 = fpr_mul(fpr_add(c_re, c_im), fpr_invsqrt8);
		w3 = fpr_mul(fpr_sub(c_im, c_re), fpr_invsqrt8);

        // Sampling
		x0 = w2;
		x1 = w3;
		sigma = tree1[3];
		w2 = fpr_of(samp(samp_ctx, x0, sigma));
		w3 = fpr_of(samp(samp_ctx, x1, sigma));
		a_re = fpr_sub(x0, w2);
		a_im = fpr_sub(x1, w3);
		b_re = tree1[0];
		b_im = tree1[1];
		c_re = fpr_sub(fpr_mul(a_re, b_re), fpr_mul(a_im, b_im));
		c_im = fpr_add(fpr_mul(a_re, b_im), fpr_mul(a_im, b_re));
		x0 = fpr_add(c_re, w0);
		x1 = fpr_add(c_im, w1);
		sigma = tree1[2];
		w0 = fpr_of(samp(samp_ctx, x0, sigma));
		w1 = fpr_of(samp(samp_ctx, x1, sigma));

        // Merge
		a_re = w0;
		a_im = w1;
		b_re = w2;
		b_im = w3;
		c_re = fpr_mul(fpr_sub(b_re, b_im), fpr_invsqrt2);
		c_im = fpr_mul(fpr_add(b_re, b_im), fpr_invsqrt2);
		z1[0] = w0 = fpr_add(a_re, c_re);
		z1[2] = w2 = fpr_add(a_im, c_im);
		z1[1] = w1 = fpr_sub(a_re, c_re);
		z1[3] = w3 = fpr_sub(a_im, c_im);

		/*
		 * Compute tb0 = t0 + (t1 - z1) * L. Value tb0 ends up in w*.
		 */
		w0 = fpr_sub(t1[0], w0);
		w1 = fpr_sub(t1[1], w1);
		w2 = fpr_sub(t1[2], w2);
		w3 = fpr_sub(t1[3], w3);

		a_re = w0;
		a_im = w2;
		b_re = tree[0];
		b_im = tree[2];
		w0 = fpr_sub(fpr_mul(a_re, b_re), fpr_mul(a_im, b_im));
		w2 = fpr_add(fpr_mul(a_re, b_im), fpr_mul(a_im, b_re));
		a_re = w1;
		a_im = w3;
		b_re = tree[1];
		b_im = tree[3];
		w1 = fpr_sub(fpr_mul(a_re, b_re), fpr_mul(a_im, b_im));
		w3 = fpr_add(fpr_mul(a_re, b_im), fpr_mul(a_im, b_re));

		w0 = fpr_add(w0, t0[0]);
		w1 = fpr_add(w1, t0[1]);
		w2 = fpr_add(w2, t0[2]);
		w3 = fpr_add(w3, t0[3]);

		/*
		 * Second recursive invocation.
		 */
        // Split
		a_re = w0;
		a_im = w2;
		b_re = w1;
		b_im = w3;
		c_re = fpr_add(a_re, b_re);
		c_im = fpr_add(a_im, b_im);
		w0 = fpr_half(c_re);
		w1 = fpr_half(c_im);
		c_re = fpr_sub(a_re, b_re);
		c_im = fpr_sub(a_im, b_im);
		w2 = fpr_mul(fpr_add(c_re, c_im), fpr_invsqrt8);
		w3 = fpr_mul(fpr_sub(c_im, c_re), fpr_invsqrt8);

        // Sampling
		x0 = w2;
		x1 = w3;
		sigma = tree0[3];
		w2 = y0 = fpr_of(samp(samp_ctx, x0, sigma));
		w3 = y1 = fpr_of(samp(samp_ctx, x1, sigma));
		a_re = fpr_sub(x0, y0);
		a_im = fpr_sub(x1, y1);
		b_re = tree0[0];
		b_im = tree0[1];
		c_re = fpr_sub(fpr_mul(a_re, b_re), fpr_mul(a_im, b_im));
		c_im = fpr_add(fpr_mul(a_re, b_im), fpr_mul(a_im, b_re));
		x0 = fpr_add(c_re, w0);
		x1 = fpr_add(c_im, w1);
		sigma = tree0[2];
		w0 = fpr_of(samp(samp_ctx, x0, sigma));
		w1 = fpr_of(samp(samp_ctx, x1, sigma));

        // Merge
		a_re = w0;
		a_im = w1;
		b_re = w2;
		b_im = w3;
		c_re = fpr_mul(fpr_sub(b_re, b_im), fpr_invsqrt2);
		c_im = fpr_mul(fpr_add(b_re, b_im), fpr_invsqrt2);
		z0[0] = fpr_add(a_re, c_re);
		z0[2] = fpr_add(a_im, c_im);
		z0[1] = fpr_sub(a_re, c_re);
		z0[3] = fpr_sub(a_im, c_im);

		return;
	}

	/*
	 * Case logn == 1 is reachable only when using Falcon-2 (the
	 * smallest size for which Falcon is mathematically defined, but
	 * of course way too insecure to be of any use).
	 */
	if (logn == 1) {
#if COMPLEX == 1
        float64x2_t x, y, a, b, c, w;
        fpr buf[2];

        z1[0] = fpr_of(samp(samp_ctx, t1[0], tree[3]));
		z1[1] = fpr_of(samp(samp_ctx, t1[1], tree[3]));

        vload(w, &t0[0]);
        vload(x, &t1[0]);
        vload(y, &z1[0]);
        vload(b, &tree[0]);
        
        vfsub(a, x, y);
        vfmul_lane(c, b, a, 0);
        vfcmla_90(c, a, b);
        vfadd(x, c, w);

        vstore(&buf[0], x);

        z0[0] = fpr_of(samp(samp_ctx, buf[0], tree[2]));
		z0[1] = fpr_of(samp(samp_ctx, buf[1], tree[2]));

#else 
        fpr x0, x1, y0, y1, sigma;
		fpr a_re, a_im, b_re, b_im, c_re, c_im;

		x0 = t1[0];
		x1 = t1[1];
		sigma = tree[3];
		z1[0] = y0 = fpr_of(samp(samp_ctx, x0, sigma));
		z1[1] = y1 = fpr_of(samp(samp_ctx, x1, sigma));
		a_re = fpr_sub(x0, y0);
		a_im = fpr_sub(x1, y1);
		b_re = tree[0];
		b_im = tree[1];
		c_re = fpr_sub(fpr_mul(a_re, b_re), fpr_mul(a_im, b_im));
		c_im = fpr_add(fpr_mul(a_re, b_im), fpr_mul(a_im, b_re));
		x0 = fpr_add(c_re, t0[0]);
		x1 = fpr_add(c_im, t0[1]);
		sigma = tree[2];
		z0[0] = fpr_of(samp(samp_ctx, x0, sigma));
		z0[1] = fpr_of(samp(samp_ctx, x1, sigma));
#endif

		return;
	}

	/*
	 * General recursive case (logn >= 2).
	 */

	n = (size_t)1 << logn;
	hn = n >> 1;
	tree0 = tree + n;
	tree1 = tree + n + ffLDL_treesize(logn - 1);

	/*
	 * We split t1 into z1 (reused as temporary storage), then do
	 * the recursive invocation, with output in tmp. We finally
	 * merge back into z1.
	 */
	ZfN(poly_split_fft)(z1, z1 + hn, t1, logn);
	ffSampling_fft(samp, samp_ctx, tmp, tmp + hn,
		tree1, z1, z1 + hn, logn - 1, tmp + n);
	ZfN(poly_merge_fft)(z1, tmp, tmp + hn, logn);

	/*
	 * Compute tb0 = t0 + (t1 - z1) * L. Value tb0 ends up in tmp[].
	 */
	ZfN(poly_sub)(tmp, t1, z1, logn);
    ZfN(poly_mul_add_fft)(tmp, t0, tmp, tree, logn);

	/*
	 * Second recursive invocation.
	 */
	ZfN(poly_split_fft)(z0, z0 + hn, tmp, logn);
	ffSampling_fft(samp, samp_ctx, tmp, tmp + hn,
		tree0, z0, z0 + hn, logn - 1, tmp + n);
	ZfN(poly_merge_fft)(z0, tmp, tmp + hn, logn);
}

/*
 * Compute a signature: the signature contains two vectors, s1 and s2.
 * The s1 vector is not returned. The squared norm of (s1,s2) is
 * computed, and if it is short enough, then s2 is returned into the
 * s2[] buffer, and 1 is returned; otherwise, s2[] is untouched and 0 is
 * returned; the caller should then try again. This function uses an
 * expanded key.
 *
 * tmp[] must have room for at least six polynomials.
 */
static int
do_sign_tree(samplerZ samp, void *samp_ctx, int16_t *s2,
	const fpr *restrict expanded_key,
	const uint16_t *hm, fpr *restrict tmp)
{
	fpr *t0, *t1, *tx, *ty;
	const fpr *b00, *b01, *b10, *b11, *tree;
	fpr ni;
	int16_t *s1tmp, *s2tmp;

	t0 = tmp;
	t1 = t0 + FALCON_N;
	b00 = expanded_key + skoff_b00(FALCON_LOGN);
	b01 = expanded_key + skoff_b01(FALCON_LOGN);
	b10 = expanded_key + skoff_b10(FALCON_LOGN);
	b11 = expanded_key + skoff_b11(FALCON_LOGN);
	tree = expanded_key + skoff_tree(FALCON_LOGN);

	/*
	 * Set the target vector to [hm, 0] (hm is the hashed message).
	 */
    ZfN(poly_fpr_of_s16)(t0, hm, FALCON_N);

	/*
	 * Apply the lattice basis to obtain the real target
	 * vector (after normalization with regards to modulus).
	 */
	ZfN(FFT)(t0, FALCON_LOGN);
	ni = fpr_inverse_of_q;
	ZfN(poly_mul_fft)(t1, t0, b01, FALCON_LOGN);
	ZfN(poly_mulconst)(t1, t1, fpr_neg(ni), FALCON_LOGN);
	ZfN(poly_mul_fft)(t0, t0, b11, FALCON_LOGN);
	ZfN(poly_mulconst)(t0, t0, ni, FALCON_LOGN);

	tx = t1 + FALCON_N;
	ty = tx + FALCON_N;
    
    /*
	 * Apply sampling. Output is written back in [tx, ty].
	 */
	ffSampling_fft(samp, samp_ctx, tx, ty, tree, t0, t1, FALCON_LOGN, ty + FALCON_N);

	/*
	 * Get the lattice point corresponding to that tiny vector.
	 */
	ZfN(poly_mul_fft)(t0, tx, b00, FALCON_LOGN);
	ZfN(poly_mul_add_fft)(t0, t0, ty, b10, FALCON_LOGN);
	ZfN(iFFT)(t0, FALCON_LOGN);
	
    ZfN(poly_mul_fft)(t1, tx, b01, FALCON_LOGN);
	ZfN(poly_mul_add_fft)(t1, t1, ty, b11, FALCON_LOGN);
	ZfN(iFFT)(t1, FALCON_LOGN);
    
	/*
	 * Compute the signature.
	 */

	/*
	 * With "normal" degrees (e.g. 512 or 1024), it is very
	 * improbable that the computed vector is not short enough;
	 * however, it may happen in practice for the very reduced
	 * versions (e.g. degree 16 or below). In that case, the caller
	 * will loop, and we must not write anything into s2[] because
	 * s2[] may overlap with the hashed message hm[] and we need
	 * hm[] for the next iteration.
	 */

    s1tmp = (int16_t *)tx;
	s2tmp = (int16_t *)tmp;

    if (ZfN(is_short_tmp)(s1tmp, s2tmp, (int16_t *) hm, t0, t1)){
		memcpy(s2, s2tmp, FALCON_N * sizeof *s2);
		memcpy(tmp, s1tmp, FALCON_N * sizeof *s1tmp);
		return 1;
	}
	return 0;
}

/*
 * Compute a signature: the signature contains two vectors, s1 and s2.
 * The s1 vector is not returned. The squared norm of (s1,s2) is
 * computed, and if it is short enough, then s2 is returned into the
 * s2[] buffer, and 1 is returned; otherwise, s2[] is untouched and 0 is
 * returned; the caller should then try again.
 *
 * tmp[] must have room for at least nine polynomials.
 */
static int
do_sign_dyn(samplerZ samp, void *samp_ctx, int16_t *s2,
	const int8_t *restrict f, const int8_t *restrict g,
	const int8_t *restrict F, const int8_t *restrict G,
	const uint16_t *hm, fpr *restrict tmp)
{
	fpr *t0, *t1, *tx, *ty;
	fpr *b00, *b01, *b10, *b11, *g00, *g01, *g11;
	fpr ni;
	int16_t *s1tmp, *s2tmp;

	/*
	 * Lattice basis is B = [[g, -f], [G, -F]]. We convert it to FFT.
	 */
	b00 = tmp;
	b01 = b00 + FALCON_N;
	b10 = b01 + FALCON_N;
	b11 = b10 + FALCON_N;
    t0 = b11 + FALCON_N;
	t1 = t0 + FALCON_N;

	smallints_to_fpr(b00, g, FALCON_LOGN);
    ZfN(FFT)(b00, FALCON_LOGN);
	
	smallints_to_fpr(b01, f, FALCON_LOGN);
    ZfN(FFT)(b01, FALCON_LOGN);
    ZfN(poly_neg)(b01, b01, FALCON_LOGN);

    smallints_to_fpr(b10, G, FALCON_LOGN);
	ZfN(FFT)(b10, FALCON_LOGN);

	smallints_to_fpr(b11, F, FALCON_LOGN);
	ZfN(FFT)(b11, FALCON_LOGN);
    ZfN(poly_neg)(b11, b11, FALCON_LOGN);

	/*
	 * Compute the Gram matrix G = B·B*. Formulas are:
	 *   g00 = b00*adj(b00) + b01*adj(b01)
	 *   g01 = b00*adj(b10) + b01*adj(b11)
	 *   g10 = b10*adj(b00) + b11*adj(b01)
	 *   g11 = b10*adj(b10) + b11*adj(b11)
	 *
	 * For historical reasons, this implementation uses
	 * g00, g01 and g11 (upper triangle). g10 is not kept
	 * since it is equal to adj(g01).
	 *
	 * We _replace_ the matrix B with the Gram matrix, but we
	 * must keep b01 and b11 for computing the target vector.
     * 
     * Memory layout: 
     * b00 | b01 | b10 | b11 | t0 | t1
     * g00 | g01 | g11 | b01 | t0 | t1
	 */
	
	ZfN(poly_muladj_fft)(t1, b00, b10, FALCON_LOGN);   // t1 <- b00*adj(b10)

	ZfN(poly_mulselfadj_fft)(t0, b01, FALCON_LOGN);    // t0 <- b01*adj(b01)
	ZfN(poly_mulselfadj_fft)(b00, b00, FALCON_LOGN);   // b00 <- b00*adj(b00)
	ZfN(poly_add)(b00, b00, t0, FALCON_LOGN);      // b00 <- g00
    
    memcpy(t0, b01, FALCON_N * sizeof *b01);
    ZfN(poly_muladj_add_fft)(b01, t1, b01, b11, FALCON_LOGN);  // b01 <- b01*adj(b11)
	
    ZfN(poly_mulselfadj_fft)(b10, b10, FALCON_LOGN);   // b10 <- b10*adj(b10)
	ZfN(poly_mulselfadj_add_fft)(b10, b10, b11, FALCON_LOGN);    // t1 = g11 <- b11*adj(b11)

    /*
	 * We rename variables to make things clearer. The three elements
	 * of the Gram matrix uses the first 3*n slots of tmp[], followed
	 * by b11 and b01 (in that order).
	 */
	g00 = b00;
	g01 = b01;
	g11 = b10;
	b01 = t0;
	t0 = b01 + FALCON_N;
	t1 = t0 + FALCON_N;

	/*
	 * Memory layout at that point:
	 *   g00 g01 g11 b11 b01 t0 t1
	 */

	/*
	 * Set the target vector to [hm, 0] (hm is the hashed message).
	 */
    ZfN(poly_fpr_of_s16)(t0, hm, FALCON_N);

    
	/*
	 * Apply the lattice basis to obtain the real target
	 * vector (after normalization with regards to modulus).
	 */
	ZfN(FFT)(t0, FALCON_LOGN);
	ni = fpr_inverse_of_q;
	ZfN(poly_mul_fft)(t1, t0, b01, FALCON_LOGN);
	ZfN(poly_mulconst)(t1, t1, fpr_neg(ni), FALCON_LOGN);
	ZfN(poly_mul_fft)(t0, t0, b11, FALCON_LOGN);
	ZfN(poly_mulconst)(t0, t0, ni, FALCON_LOGN);
  
	/*
	 * b01 and b11 can be discarded, so we move back (t0,t1).
	 * Memory layout is now:
	 *      g00 g01 g11 t0 t1
	 */
	memcpy(b11, t0, FALCON_N * 2 * sizeof *t0);
	t0 = g11 + FALCON_N;
	t1 = t0 + FALCON_N;

	/*
	 * Apply sampling; result is written over (t0,t1).
     * t1, g00
	 */
	ffSampling_fft_dyntree(samp, samp_ctx,
		t0, t1, g00, g01, g11, FALCON_LOGN, FALCON_LOGN, t1 + FALCON_N);
    
	/*
	 * We arrange the layout back to:
	 *     b00 b01 b10 b11 t0 t1
	 *
	 * We did not conserve the matrix basis, so we must recompute
	 * it now.
	 */
	b00 = tmp;
	b01 = b00 + FALCON_N;
	b10 = b01 + FALCON_N;
	b11 = b10 + FALCON_N;
	memmove(b11 + FALCON_N, t0, FALCON_N * 2 * sizeof *t0);
	t0 = b11 + FALCON_N;
	t1 = t0 + FALCON_N;

	smallints_to_fpr(b00, g, FALCON_LOGN);
	ZfN(FFT)(b00, FALCON_LOGN);

	smallints_to_fpr(b01, f, FALCON_LOGN);
	ZfN(FFT)(b01, FALCON_LOGN);
    ZfN(poly_neg)(b01, b01, FALCON_LOGN);

	smallints_to_fpr(b10, G, FALCON_LOGN);
	ZfN(FFT)(b10, FALCON_LOGN);
    
	smallints_to_fpr(b11, F, FALCON_LOGN);
	ZfN(FFT)(b11, FALCON_LOGN);
    ZfN(poly_neg)(b11, b11, FALCON_LOGN);

	tx = t1 + FALCON_N;
	ty = tx + FALCON_N;

	/*
	 * Get the lattice point corresponding to that tiny vector.
	 */


    ZfN(poly_mul_fft)(tx, t0, b00, FALCON_LOGN);
	ZfN(poly_mul_fft)(ty, t0, b01, FALCON_LOGN);
	ZfN(poly_mul_add_fft)(t0, tx, t1, b10, FALCON_LOGN);
    ZfN(poly_mul_add_fft)(t1, ty, t1, b11, FALCON_LOGN);
	
	ZfN(iFFT)(t0, FALCON_LOGN);
	ZfN(iFFT)(t1, FALCON_LOGN);


	/*
	 * With "normal" degrees (e.g. 512 or 1024), it is very
	 * improbable that the computed vector is not short enough;
	 * however, it may happen in practice for the very reduced
	 * versions (e.g. degree 16 or below). In that case, the caller
	 * will loop, and we must not write anything into s2[] because
	 * s2[] may overlap with the hashed message hm[] and we need
	 * hm[] for the next iteration.
	 */
	s1tmp = (int16_t *)tx;
	s2tmp = (int16_t *)tmp;
	
    if (ZfN(is_short_tmp)(s1tmp, s2tmp, (int16_t *) hm, t0, t1)){
		memcpy(s2, s2tmp, FALCON_N * sizeof *s2);
		memcpy(tmp, s1tmp, FALCON_N * sizeof *s1tmp);
		return 1;
	}
	return 0;
}


/* see inner.h */
void
Zf(sign_tree)(int16_t *sig, inner_shake256_context *rng,
	const fpr *restrict expanded_key,
	const uint16_t *hm, uint8_t *tmp)
{
	fpr *ftmp;

	ftmp = (fpr *)tmp;
	for (;;) {
		/*
		 * Signature produces short vectors s1 and s2. The
		 * signature is acceptable only if the aggregate vector
		 * s1,s2 is short; we must use the same bound as the
		 * verifier.
		 *
		 * If the signature is acceptable, then we return only s2
		 * (the verifier recomputes s1 from s2, the hashed message,
		 * and the public key).
		 */
		sampler_context spc;
		samplerZ samp;
		void *samp_ctx;

		/*
		 * Normal sampling. We use a fast PRNG seeded from our
		 * SHAKE context ('rng').
		 */
#if FALCON_LOGN == 9
		spc.sigma_min = fpr_sigma_min_9;
#elif FALCON_LOGN == 10
        spc.sigma_min = fpr_sigma_min_10;
#else 
#error "Support 512, 1024 only"
#endif
		Zf(prng_init)(&spc.p, rng);
		samp = Zf(sampler);
		samp_ctx = &spc;

		/*
		 * Do the actual signature.
		 */
		if (do_sign_tree(samp, samp_ctx, sig, expanded_key, hm, ftmp))
		{
			break;
		}
	}
}

/* see inner.h */
void
Zf(sign_dyn)(int16_t *sig, inner_shake256_context *rng,
	const int8_t *restrict f, const int8_t *restrict g,
	const int8_t *restrict F, const int8_t *restrict G,
	const uint16_t *hm, uint8_t *tmp)
{
	fpr *ftmp;

	ftmp = (fpr *)tmp;
	for (;;) {

		/*
		 * Signature produces short vectors s1 and s2. The
		 * signature is acceptable only if the aggregate vector
		 * s1,s2 is short; we must use the same bound as the
		 * verifier.
		 *
		 * If the signature is acceptable, then we return only s2
		 * (the verifier recomputes s1 from s2, the hashed message,
		 * and the public key).
		 */
		sampler_context spc;
		samplerZ samp;
		void *samp_ctx;

		/*
		 * Normal sampling. We use a fast PRNG seeded from our
		 * SHAKE context ('rng').
		 */
#if FALCON_LOGN == 9
		spc.sigma_min = fpr_sigma_min_9;
#elif FALCON_LOGN == 10
        spc.sigma_min = fpr_sigma_min_10;
#else 
#error "Support 512, 1024 only"
#endif
		Zf(prng_init)(&spc.p, rng);
		samp = Zf(sampler);
		samp_ctx = &spc;

		/*
		 * Do the actual signature.
		 */
		if (do_sign_dyn(samp, samp_ctx, sig, f, g, F, G, hm, ftmp))
		{
			break;
		}
	}
}
