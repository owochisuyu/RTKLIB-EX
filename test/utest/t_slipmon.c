/* Steps are applied to raw phases, not detector statistics. */
#include "../../src/rtklib.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); exit(1); } } while (0)
static nav_t nav;
static void epoch(obsd_t *o,int t,int r)
{
    memset(o,0,6*sizeof(*o));
    for (int i=0;i<6;i++) for (int f=0;f<2;f++) {
        double lam=CLIGHT/(f?FREQL2:FREQL1),v=110+i,range=2.1e7+t*v;
        o[i].sat=i+1; o[i].rcv=r; o[i].time=gpst2time(2200,1000+t);
        o[i].code[f]=f?CODE_L2W:CODE_L1C; o[i].L[f]=range/lam+200-i;
        o[i].D[f]=(float)(-v/lam); o[i].P[f]=range; o[i].SNR[f]=47;
    }
}
enum kind { EQUAL,SPIKE,GF_BLIND,NEAR_BLIND,ONE_FREQ,BASE_ONLY,CLOCK,CODE_SPIKE,
            GAP,LOCK,HALF,CODE_SWITCH,TOO_FEW,LOST_LLI,PAIR_MISSING,NEGATIVE,CLOCK_PLUS_SLIP };
static void scenario(enum kind kind)
{
    struct slipmon *state=NULL;
    obsd_t o[6];
    prcopt_t opt=prcopt_default; opt.slipmon=2; opt.thresdop=0.05; opt.slipgap=10;
    if (kind<=NEAR_BLIND) opt.thresdop=0;
    for (int t=0;t<13;t++) for (int r=1;r<=2;r++) {
        epoch(o,t+((kind==GAP&&t>=8&&r==1)?20:0),r);
        int n=kind==TOO_FEW?1:6,active=(kind==BASE_ONLY?r==2:r==1);
        double d1=0,d2=0;
        if (kind==ONE_FREQ||kind==TOO_FEW) o[0].L[1]=0;
        if (active&&t>=8) switch (kind) {
            case SPIKE: d1=d2=t==8; break;
            case EQUAL: d1=d2=1; break;
            case GF_BLIND: d1=77; d2=60; break;
            case NEAR_BLIND: d1=9; d2=7; break;
            case ONE_FREQ: case TOO_FEW: d1=1; break;
            case BASE_ONLY: d2=-1; break;
            case NEGATIVE: d1=d2=-1; break;
            case CLOCK: case CLOCK_PLUS_SLIP:
                for (int i=0;i<n;i++) for (int f=0;f<2;f++) {
                    o[i].L[f]+=120*(f?FREQL2:FREQL1)/CLIGHT; o[i].P[f]+=120;
                }
                if (kind==CLOCK_PLUS_SLIP) d2=1;
                break;
            case CODE_SPIKE: if (t==8) o[0].P[0]+=15; break;
            case LOCK: if (t==8) o[0].LLI[0]=LLI_SLIP; break;
            case HALF: o[0].LLI[0]=LLI_HALFC; break;
            case CODE_SWITCH: o[0].code[0]=CODE_L1W; break;
            case LOST_LLI: if (t==8) { o[0].L[0]=0; o[0].LLI[0]=LLI_SLIP; } break;
            case PAIR_MISSING: if (t==8) o[0].L[1]=0; d1=-1; break;
            default: break;
        }
        o[0].L[0]+=d1; o[0].L[1]+=d2;
        CHECK(slipmon_scan(&state,o,n,r,&nav,&opt));
        for (int i=0;i<n;i++) for (int f=0;f<2;f++) {
            int expected=1,skip=0,affected=0;
            if (!o[i].L[f]) expected=0;
            else if (active&&t>=8) {
                if (kind==GAP) expected=2;
                if (i==0) {
                    if (kind==LOCK||kind==HALF||kind==CODE_SWITCH||kind==LOST_LLI)
                        expected=f==0?2:1;
                    if (kind==SPIKE) skip=t==8;
                    if (kind==EQUAL||kind==GF_BLIND||kind==NEAR_BLIND||kind==NEGATIVE) affected=1;
                    if (kind==ONE_FREQ||kind==PAIR_MISSING) affected=f==0;
                    if (kind==BASE_ONLY||kind==CLOCK_PLUS_SLIP) affected=f==1;
                    if (affected) { skip=t==8; expected=t==8?1:2; }
                }
            }
            if (o[i].phase_arc[f]!=(uint32_t)expected||o[i].phase_skip[f]!=skip) {
                fprintf(stderr,"case %d t=%d r=%d sat=%d f=%d got %u,%u expected %d,%d\n",
                        kind,t,r,i+1,f+1,o[i].phase_arc[f],o[i].phase_skip[f],expected,skip);
                exit(1);
            }
        }
        if (t==8) {
            obsd_t original=o[0]; CHECK(slipmon_scan(&state,o,n,r,&nav,&opt));
            CHECK(!memcmp(o[0].phase_arc,original.phase_arc,sizeof(original.phase_arc)));
            CHECK(!memcmp(o[0].phase_skip,original.phase_skip,sizeof(original.phase_skip)));
        }
    }
    free(state);
}
static void grid(void)
{
    for (int d=0;d<=1;d++) for (int a=-5;a<=5;a++) for (int b=-5;b<=5;b++) {
        if (!a&&!b) continue;
        struct slipmon *state=NULL;
        prcopt_t opt=prcopt_default; opt.slipmon=2; opt.thresdop=d?0.05:0;
        obsd_t o[6];
        for (int t=0;t<12;t++) {
            epoch(o,t,1);
            if (t>=8) { o[0].L[0]+=a; o[0].L[1]+=b; }
            CHECK(slipmon_scan(&state,o,6,1,&nav,&opt));
            for (int f=0;f<2;f++) {
                /* Without per-band evidence the conservative result is the pair. */
                int affected=!d||(f?b:a)!=0;
                CHECK(o[0].phase_skip[f]==(affected&&t==8));
                CHECK(o[0].phase_arc[f]==(uint32_t)(1+(affected&&t>8)));
            }
        }
        free(state);
    }
}
int main(void)
{
    for (int i=EQUAL;i<=CLOCK_PLUS_SLIP;i++) scenario((enum kind)i);
    grid();
    puts("17 targeted cases and 240 persistent-step injections passed"); return 0;
}
