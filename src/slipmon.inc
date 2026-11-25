/* Receiver-local slip monitor, compiled with rtkpos.c so the standalone and
 * GUI builds use the same implementation as the shared library. */
#define SM_S (NFREQ+NEXOBS)
#define SM_P ((SM_S*(SM_S-1)/2)>0?(SM_S*(SM_S-1)/2):1)
#define SM_W 9
typedef struct {
    gtime_t raw,good;
    double L,D,lambda,acc,var,pending_value;
    int code,lli,deferred,integral,pending,count;
    uint32_t arc;
} sm_sig_t;
typedef struct {
    gtime_t time;
    double gf,rate[SM_W],mw[SM_W];
    int nr,nm,ir,im;
} sm_pair_t;
typedef struct {
    sm_sig_t sig[SM_S];
    sm_pair_t pair[SM_P];
    gtime_t stamp;
    uint32_t arc[NFREQ];
    uint8_t skip[NFREQ];
} sm_sat_t;
struct slipmon {
    sm_sat_t sat[2][MAXSAT];
    gtime_t time[2];
};
typedef struct { double x; int sat; } sm_clock_t;
typedef struct { int key,seen; double x,gate,oldx,oldgate; } sm_evidence_t;

extern int slipmon_enabled(const prcopt_t *opt)
{
    return (opt->slipmon==1||opt->slipmon==2)&&
           opt->mode>=PMODE_KINEMA&&opt->mode<=PMODE_FIXED;
}
static int sm_double_cmp(const void *a,const void *b)
{
    double x=*(const double *)a,y=*(const double *)b;
    return (x>y)-(x<y);
}
static int sm_clock_cmp(const void *a,const void *b)
{
    double x=((const sm_clock_t *)a)->x,y=((const sm_clock_t *)b)->x;
    return (x>y)-(x<y);
}
static double sm_median(double *v,int n)
{
    if (!n) return 0.0;
    qsort(v,n,sizeof(*v),sm_double_cmp);
    return (v[(n-1)/2]+v[n/2])*0.5;
}
static double sm_stats(const double *v,int n,double *mad)
{
    double w[SM_W],m;
    memcpy(w,v,n*sizeof(*v));
    m=sm_median(w,n);
    for (int i=0;i<n;i++) w[i]=fabs(v[i]-m);
    *mad=1.4826*sm_median(w,n);
    return m;
}
static void sm_push(double *v,int *n,int *i,double x)
{
    v[*i]=x; *i=(*i+1)%SM_W;
    if (*n<SM_W) (*n)++;
}
/* L is cycles, RINEX D is Hz with the opposite sign. Compare simultaneous
 * range increments (m), never count two signals as two clock references. */
static int sm_increment(const sm_sig_t *s,const obsd_t *o,int f,double lam,
                        gtime_t previous,double gap,double *dx)
{
    double dt=timediff(o->time,s->raw);
    if (!s->raw.time||fabs(timediff(previous,s->raw))>DTTOL||
        fabs(dt)<=DTTOL||fabs(dt)>gap||!lam||s->lambda!=lam||
        !o->L[f]||!o->D[f]||!s->D||s->code!=o->code[f]) return 0;
    *dx=lam*(o->L[f]-s->L+0.5*(s->D+o->D[f])*dt);
    return isfinite(*dx);
}
static void sm_vote(sm_evidence_t *e,int pending,int key,double x,double gate)
{
    if (!(gate>0.0)||!isfinite(x)) return;
    if (key==pending) { e->seen=1; e->oldx=x; e->oldgate=gate; }
    if (fabs(x)<=gate) return;
    /* A per-signal Doppler innovation localizes a jump; a pair does not. */
    if (key==1||!e->key||(e->key!=1&&fabs(x)/gate>fabs(e->x)/e->gate)) {
        e->key=key; e->x=x; e->gate=gate;
    }
}
static int sm_pair_index(int a,int b)
{
    return a*(2*SM_S-a-1)/2+b-a-1;
}
static void sm_record(sm_sat_t *sat,obsd_t *o,int rcv,const nav_t *nav,
                      const prcopt_t *opt,gtime_t prev,double gap,
                      int clock_ok,double clock,double spread)
{
    sm_evidence_t e[SM_S]={{0}};
    double lam[SM_S]={0},gf[SM_P]={0},mw[SM_P]={0};
    int usable[SM_S]={0},skip[SM_S]={0},reset[SM_S]={0},dop[SM_S]={0};
    int pg[SM_P]={0},pm[SM_P]={0};
    const char *reason[SM_S]={0};
    if (sat->stamp.time&&fabs(timediff(o->time,sat->stamp))<=DTTOL) {
        memcpy(o->phase_arc,sat->arc,sizeof(sat->arc));
        memcpy(o->phase_skip,sat->skip,sizeof(sat->skip));
        return;
    }
    memset(o->phase_arc,0,sizeof(o->phase_arc));
    memset(o->phase_skip,0,sizeof(o->phase_skip));
    for (int f=0;f<SM_S;f++) {
        sm_sig_t *s=sat->sig+f;
        double freq,dx,dt=timediff(o->time,s->raw);
        if (!o->L[f]||!isfinite(o->L[f])||!o->code[f]) {
            if (o->LLI[f]&LLI_SLIP) s->deferred=1;
            continue;
        }
        usable[f]=1;
        freq=sat2freq(o->sat,o->code[f],nav);
        if (freq>0.0) lam[f]=CLIGHT/freq;
        if (!s->arc) reason[f]="start";
        else if (s->code!=o->code[f]||(s->lambda&&lam[f]&&s->lambda!=lam[f])) reason[f]="signal";
        else if (s->deferred||((dt<0?s->lli:o->LLI[f])&LLI_SLIP)) reason[f]="lock";
        else if ((s->lli^o->LLI[f])&LLI_HALFC) reason[f]="half";
        else if (fabs(timediff(o->time,s->good))>gap) reason[f]="gap";
        if (reason[f]) { reset[f]=1; continue; }
        if (!clock_ok||!sm_increment(s,o,f,lam[f],prev,gap,&dx)) s->integral=0;
        if (s->integral) {
            double gate=fmax(opt->thresdop*fabs(dt),4.5*spread);
            s->acc+=dx-clock; s->var+=gate*gate;
            dop[f]=1;
            sm_vote(e+f,s->pending,1,s->acc,sqrt(s->var));
        }
    }
    for (int a=0;a<SM_S;a++) for (int b=a+1;b<SM_S;b++) {
        int k=sm_pair_index(a,b);
        sm_pair_t *p=sat->pair+k;
        if (!usable[a]||!usable[b]||!lam[a]||!lam[b]||lam[a]==lam[b]) continue;
        gf[k]=lam[a]*o->L[a]-lam[b]*o->L[b];
        pg[k]=isfinite(gf[k]);
        if (o->P[a]>0&&o->P[b]>0) {
            /* Wide-lane cycles; keep the sign even when frequencies are reversed. */
            double wl=1.0/(1.0/lam[a]-1.0/lam[b]);
            mw[k]=o->L[a]-o->L[b]-(o->P[a]*lam[b]+o->P[b]*lam[a])/((lam[a]+lam[b])*wl);
            pm[k]=isfinite(mw[k]);
        }
        if (!p->time.time||reset[a]||reset[b]) continue;
        double dt=timediff(o->time,p->time),sg,sw;
        if (fabs(dt)>gap) continue;
        double slope=sm_stats(p->rate,p->nr,&sg),center=sm_stats(p->mw,p->nm,&sw);
        double dg=gf[k]-p->gf-slope*dt,dm=mw[k]-center;
        double tg=fmax(opt->thresslip,4.5*sg*fabs(dt)),tm=fmax(0.75,4.5*sw);
        for (int j=0;j<2;j++) {
            int f=j?b:a,other=j?a:b;
            /* When Doppler resolves the other frequency and clears this one,
             * do not spread a pair alarm to the healthy partner. */
            double g=dg,w=dm;
            if (dop[other]&&e[other].key==1&&e[f].key!=1) {
                if (dop[f]) continue;
                /* Subtract a known single-frequency displacement only if its
                 * trusted epoch is the pair's reference. Handles a returning
                 * partner for which a one-epoch Doppler integral is missing. */
                if (fabs(timediff(sat->sig[other].good,p->time))<=DTTOL) {
                    double sign=other==a?1.0:-1.0;
                    g-=sign*sat->sig[other].acc;
                    w-=sign*sat->sig[other].acc/lam[other];
                }
            }
            if (pg[k]&&opt->thresslip>0)
                sm_vote(e+f,sat->sig[f].pending,2+2*k,g,tg);
            /* A code spike alone should not discard two Doppler-clean phases. */
            if (pm[k]&&p->nm>=3&&!(dop[a]&&dop[b]&&e[a].key!=1&&e[b].key!=1))
                sm_vote(e+f,sat->sig[f].pending,3+2*k,w,tm);
        }
    }
    for (int f=0;f<SM_S;f++) {
        sm_sig_t *s=sat->sig+f;
        sm_evidence_t *v=e+f;
        int diagkey=s->pending?s->pending:v->key;
        if (!usable[f]) continue;
        if (!reset[f]&&s->pending) {
            if (v->seen&&fabs(v->oldx)>0.65*v->oldgate&&
                fabs(v->oldx-s->pending_value)<=fmax(v->oldgate,0.25*fabs(s->pending_value))&&
                v->oldx*s->pending_value>0.0) {
                reason[f]=s->pending==1?"doppler-step":"pair-step"; reset[f]=1;
            }
            else if (!v->seen||v->key) {
                if (++s->count>=3) { reason[f]="unverified"; reset[f]=1; }
                else skip[f]=1;
            }
            else s->pending=0; /* returned to the unmodified pre-event arc */
        }
        if (!reset[f]&&!s->pending&&v->key) {
            s->pending=v->key; s->pending_value=v->x; s->count=1; skip[f]=1;
        }
        if (reset[f]) { s->arc++; if (!s->arc) s->arc=1; s->pending=0; }
        if (skip[f]||(reset[f]&&s->arc!=1)) {
            char stamp[40];
            trace(2,"slipmon %s rcv=%d sat=%d code=%s arc=%u action=%s reason=%s key=%d residual=%.5f gate=%.5f\n",
                  time2str(o->time,stamp,3),rcv,o->sat,code2obs(o->code[f]),s->arc,
                  skip[f]?"skip":"reset",reason[f]?reason[f]:"innovation",
                  diagkey,v->seen?v->oldx:v->x,v->seen?v->oldgate:v->gate);
        }
        if (!skip[f]) { s->good=o->time; s->acc=s->var=0; s->integral=1; }
        s->raw=o->time; s->L=o->L[f]; s->D=o->D[f]; s->lambda=lam[f];
        s->code=o->code[f]; s->lli=o->LLI[f]; s->deferred=0;
        if (f<NFREQ) { o->phase_arc[f]=s->arc; o->phase_skip[f]=(uint8_t)skip[f]; }
    }
    /* Only trusted endpoints may advance either combination's baseline. */
    for (int a=0;a<SM_S;a++) for (int b=a+1;b<SM_S;b++) {
        int k=sm_pair_index(a,b);
        sm_pair_t *p=sat->pair+k;
        if (reset[a]||reset[b]||(p->time.time&&fabs(timediff(o->time,p->time))>gap))
            memset(p,0,sizeof(*p));
        if (!pg[k]||skip[a]||skip[b]) continue;
        double dt=timediff(o->time,p->time);
        if (p->time.time&&fabs(dt)>DTTOL) sm_push(p->rate,&p->nr,&p->ir,(gf[k]-p->gf)/dt);
        p->gf=gf[k]; p->time=o->time;
        if (pm[k]) sm_push(p->mw,&p->nm,&p->im,mw[k]);
    }
    memcpy(sat->arc,o->phase_arc,sizeof(sat->arc));
    memcpy(sat->skip,o->phase_skip,sizeof(sat->skip));
    sat->stamp=o->time;
}

extern int slipmon_scan(struct slipmon **state,obsd_t *obs,int n,int rcv,
                        const nav_t *nav,const prcopt_t *opt)
{
    sm_clock_t refs[MAXSAT];
    double scatter[MAXSAT],spread=0,gap=opt->slipgap;
    int rank[MAXSAT],nr=0;
    if (!n||!slipmon_enabled(opt)) return 1;
    if (rcv<1||rcv>2||n<0||!isfinite(gap)||gap<=0||opt->nf<1||opt->nf>NFREQ||
        (opt->ionoopt==IONOOPT_IFLC&&opt->nf<2)) return 0;
    if (!*state&&!(*state=calloc(1,sizeof(**state)))) return 0;
    sm_sat_t *sats=(*state)->sat[rcv-1];
    gtime_t prev=(*state)->time[rcv-1];
    for (int i=0;i<MAXSAT;i++) rank[i]=-1;
    for (int i=0;opt->thresdop>0&&i<n;i++) {
        const obsd_t *o=obs+i;
        int sat=o->sat-1;
        if (sat<0||sat>=MAXSAT||rank[sat]>=0||opt->exsats[sat]==1||
            !(satsys(o->sat,NULL)&opt->navsys)) continue;
        double v[SM_S],dx; int nv=0;
        for (int f=0;f<SM_S;f++) {
            sm_sig_t *s=sats[sat].sig+f;
            if (!s->arc||s->pending||s->deferred||!o->code[f]||
                (o->LLI[f]&LLI_SLIP)||((o->LLI[f]^s->lli)&LLI_HALFC)) continue;
            double freq=sat2freq(o->sat,o->code[f],nav);
            if (freq>0&&sm_increment(s,o,f,CLIGHT/freq,prev,gap,&dx)) v[nv++]=dx;
        }
        if (nv) { rank[sat]=nr; refs[nr].sat=sat; refs[nr++].x=sm_median(v,nv); }
    }
    if (nr>=3) {
        qsort(refs,nr,sizeof(*refs),sm_clock_cmp);
        double center=(refs[(nr-1)/2].x+refs[nr/2].x)*0.5;
        for (int i=0;i<nr;i++) { rank[refs[i].sat]=i; scatter[i]=fabs(refs[i].x-center); }
        spread=1.4826*sm_median(scatter,nr);
    }
    for (int i=0;i<n;i++) {
        int sat=obs[i].sat-1;
        if (sat<0||sat>=MAXSAT) continue;
        int k=rank[sat],m=nr-(k>=0),a=(m-1)/2,b=m/2;
        double clk=0.0;
        if (m>=3) { if (k>=0) { a+=a>=k; b+=b>=k; } clk=(refs[a].x+refs[b].x)*0.5; }
        sm_record(sats+sat,obs+i,rcv,nav,opt,prev,gap,m>=3,clk,spread);
    }
    (*state)->time[rcv-1]=obs[0].time;
    return 1;
}

/* Consume a boundary once, regardless of reused base epochs or solve direction. */
static void sm_consume(ssat_t *s,const obsd_t *u,const obsd_t *b,const prcopt_t *opt)
{
    const obsd_t *ends[2]={u,b};
    int nf=opt->nf<NFREQ?opt->nf:NFREQ;
    for (int f=0;f<nf;f++) {
        int reset=0,skip=0,half=0,fresh=0;
        for (int r=0;r<2;r++) {
            uint32_t id=ends[r]->phase_arc[f];
            if (id&&!s->phase_arc[r][f]) fresh=1;
            if (id&&s->phase_arc[r][f]&&s->phase_arc[r][f]!=id) reset=1;
            if (id) s->phase_arc[r][f]=id;
            skip|=!id||ends[r]->phase_skip[f];
            half|=ends[r]->LLI[f]&LLI_HALFC;
        }
        if (reset) s->slip[f]|=LLI_SLIP;
        if (half) s->slip[f]|=LLI_HALFC;
        s->half[f]=!half;
        s->phase_skip[f]=(uint8_t)skip;
        s->ar_block[f]=(uint8_t)(fresh||reset||skip||half);
        if (s->ar_block[f]) s->fix[f]=0;
        if (reset||u->phase_skip[f]||b->phase_skip[f]) {
            char stamp[40];
            trace(3,"slipmon-use %s sat=%d f=%d reset=%d skip=%d ar_block=%d\n",
                  time2str(u->time,stamp,3),u->sat,f+1,reset,skip,s->ar_block[f]);
        }
    }
    if (opt->ionoopt==IONOOPT_IFLC) {
        int f2=seliflc(nf,s->sys);
        s->phase_skip[0]|=s->phase_skip[f2];
        s->ar_block[0]|=s->ar_block[f2];
        s->half[0]&=s->half[f2];
        s->slip[0]|=s->slip[f2]&(LLI_SLIP|LLI_HALFC);
        if (s->ar_block[0]) s->fix[0]=0;
    }
}
