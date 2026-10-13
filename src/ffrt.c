/* Lookup FFRT following LAMBDA 3.0 ratioinv.m (TU Delft).
 * Two distinct tables for Pf_tol=0.001 and 0.01; no satellite-count polynomial
 * or fixed ratio floor. See SOURCE.json for table provenance.
 */
#include <math.h>
#include "ffrt.h"
#include "ffrt_tables.h"

double ffrt_mu(double pf_tol, double pf_ils_upper, int n)
{
    const double *table;
    int cols, lo=0, hi=30, mid;
    double x0,x1,y0,y1;
    if (n < 1 || !isfinite(pf_ils_upper) ||
        pf_ils_upper < 0.0 || pf_ils_upper > 1.0) return NAN;
    if (fabs(pf_tol - 0.01) < 1E-12) {
        table=&ffrt_table10[0][0]; cols=41;
    }
    else if (fabs(pf_tol - 0.001) < 1E-12) {
        table=&ffrt_table1[0][0]; cols=64;
    }
    else return NAN;
    /* Match the published ratioinv dimension saturation, without extrapolation. */
    if (n>=cols) n=cols-1;
    if (pf_ils_upper<=table[0]) return table[n];
    if (pf_ils_upper>=table[30*cols]) return table[30*cols+n];
    while (hi-lo>1) {
        mid=(lo+hi)/2;
        if (pf_ils_upper<table[mid*cols]) hi=mid;
        else lo=mid;
    }
    x0=table[lo*cols]; x1=table[hi*cols];
    y0=table[lo*cols+n]; y1=table[hi*cols+n];
    return y0+(pf_ils_upper-x0)/(x1-x0)*(y1-y0);
}

int ffrt_accept(double best, double second, double mu)
{
    /* A weak-model mu==0 is unconditional rejection, including best==0.
     * LAMBDA reference rejects only if best/second > mu (inclusive boundary). */
    return isfinite(mu) && mu > 0.0 && mu <= 1.0 &&
           isfinite(best) && isfinite(second) && best >= 0.0 &&
           second > 0.0 && best <= second && best / second <= mu;
}
