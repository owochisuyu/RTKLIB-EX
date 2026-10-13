#ifndef RTK_AV_FFRT_H
#define RTK_AV_FFRT_H
/* LAMBDA 3.0 lookup FFRT, tolerances 0.01 and 0.001.
 * mu is the best/second-best squared-norm threshold (0..1).
 * Invalid inputs return NAN. Large n saturates as in ratioinv.m.
 * pf_ils_upper is a model-based upper bound, not empirical failure rate. */
double ffrt_mu(double pf_tol, double pf_ils_upper, int n);
int ffrt_accept(double best, double second, double mu);
#endif
