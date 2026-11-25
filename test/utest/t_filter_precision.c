#include "../../src/rtklib.h"

static int close_to(double value,double reference,double tol)
{
    if (isfinite(value)&&fabs(value-reference)<=tol) return 1;
    fprintf(stderr,"got %.16g; expected %.16g (+/- %.3g)\n",value,reference,tol);
    return 0;
}

int main(void)
{
    /* Two correlated measurements of the same position are equivalent to
     * one measurement with variance (r11+r12)/2. The other state covaries
     * with position. This gives an analytic posterior at long-interval scale. */
    double p=1E9,c=2E5,r=0.0012,den=p+r;
    double x[]={10,20},P[]={p,c,c,100},H[]={1,0,1,0},v[]={2,2};
    double R[]={0.002,0.0004,0.0004,0.002};
    if (filter(x,P,H,v,R,2,2)||
        !close_to(x[0],10+2*p/den,1E-8)||!close_to(x[1],20+2*c/den,1E-8)||
        !close_to(P[0],p*r/den,1E-8)||!close_to(P[1],c*r/den,1E-8)||
        !close_to(P[2],P[1],1E-12)||!close_to(P[3],100-c*c/den,1E-7)) return 1;
    /* An unsolvable innovation must not partially replace the prior. */
    double oldx[2],oldP[4],zeroH[2]={0},zeroR=0.0;
    memcpy(oldx,x,sizeof(x)); memcpy(oldP,P,sizeof(P));
    if (!filter(x,P,zeroH,v,&zeroR,2,1)||
        memcmp(oldx,x,sizeof(x))||memcmp(oldP,P,sizeof(P))) return 1;
    return 0;
}
