

typedef struct {
	double damping;
	double y;
} g_damper;

typedef struct {
	int size;			// max size of delay line (MUST BE POWER OF 2)
	int wrap;		    // needed for efficient(?) wraping
	int tap[3];		    // actual delay time of allpass filter
	int idx;			// write pointer
	double coeff;
	double *buf;		// sample storage
} g_diffuser;


typedef struct {
	int size;
	int wrap;
	int tap[4];
	int writep;
	double *buf;
	
	double damping;
	double lastout;
} g_tapdelay;



g_damper *damper_make(double damping)
{
	g_damper *p;
	
	p = (g_damper *)malloc(sizeof(g_damper));
	p->damping = damping;
	p->y = 0.0;
	return(p);
}

void damper_free(g_damper *p)
{
	free(p);
}

double damper_do(g_damper *p, double x)
{ 
	double y;
	y = x*(1.0-p->damping) + p->y*p->damping;
	
	p->y = y;
	return(y);
}


void damper_do_block(g_damper *p, double *vec, int vecsize)
{ 
	int i;
	double coef2= p->damping;
	double coef1 = 1-coef2;
	double y = p->y;
	
	for(i=0; i<vecsize; i++) 
	 {
		y = vec[i]*coef1+ (y*coef2);		// achtung --> denorms!

		vec[i] = y;				
	}
	
	p->y = y;
	
}


g_diffuser *diffuser_make(int size, int *tap, double coeff)
{
	g_diffuser *p;
	int i;
	
	p = (g_diffuser *)malloc(sizeof(g_diffuser));
	p->size = size;		// MUST BE POWER OF 2!!!
	p->wrap = size-1;
	p->coeff = coeff;
	for(i=0; i<3; i++)
		p->tap[i] = tap[i];
	p->idx = 0;
	p->buf = (double *)malloc(size*sizeof(double));
	memset(p->buf, 0, size*sizeof(double));
	return(p);
}

void diffuser_free(g_diffuser *p)
{
	free(p->buf);
	free(p);
}

double diffuser_do(g_diffuser *p, double x)
{
	double y, v, coeff, delayed;
	int wrap = p->wrap;
	int writep = p->idx;
	int del = p->tap[0];
	int readp = (writep-del) & wrap;
	delayed = p->buf[readp];
	coeff = p->coeff;
	
	v = x - delayed*coeff;
	y = delayed + v*coeff;

	p->buf[writep] = v;
	writep = (writep+1)&wrap;
	
	p->idx = writep;
	return(y);
}



void diffuser_do_block(g_diffuser *p, double *vec, int vecsize)
{
	double v, coeff, delayed;
	int i, writep, readp, del;
	int wrap = p->wrap;
	double *delbuf = p->buf;
	del = p->tap[0];
	writep = p->idx;
	coeff = p->coeff;
	
	for(i=0; i<vecsize; i++) {
		readp = (writep-del) & wrap;
		delayed = delbuf[readp];
		
		v = vec[i] - delayed*coeff;
		//v = *(vec+i) - delayed*coeff;

		vec[i] = delayed + v*coeff;

		delbuf[writep] = v;
		writep = (writep+1)&wrap;
	}
	p->idx = writep;
}




double diffuser_do_decay(g_diffuser *p, double x, double *outL, double *outR)
{
	double y, v, coeff, delayed;
	int wrap = p->wrap;
	int writep = p->idx;
	int del = p->tap[0];
	int del1 = p->tap[1];
	int del2 = p->tap[2];
	double *buf = p->buf;
	int readp = (writep-del) & wrap;
	delayed = buf[readp];
	coeff = p->coeff;
	
    v = x - delayed*coeff;

	y = delayed + v*coeff;

	// output taps
	readp = (writep-del1) & wrap;
	*outL -= buf[readp];
	readp = (writep-del2) & wrap;
	*outR -= buf[readp];
	
	buf[writep] = v;
	writep = (writep+1)&wrap;
	
	p->idx = writep;
	return(y);
}


/****** fixed delays *******/

g_tapdelay *tapdelay_make(int size,  int *deltimes)
{
	g_tapdelay *p;
	int i;
	
	p = (g_tapdelay *)malloc(sizeof(g_tapdelay));
	p->size = size;
	p->wrap = size - 1;
	p->writep = 0;
	
	p->damping = 0.5;
	p->lastout = 0.;
	
	for(i=0; i<4; i++)
		p->tap[i] = deltimes[i];		// deep copy of init delaytimes
	
	p->buf = (double *)malloc(size*sizeof(double));
	memset(p->buf, 0, size*sizeof(double));
	return(p);
}

void tapdelay_free(g_tapdelay *p)
{
	free(p->buf);
	free(p);
}


double tapdelay1_do_left(g_tapdelay *p, double x, double *outL, double *outR)
{
	// tapdelay with onepole damping
	double y, coef1, coef2, lastout;
	int readp;
	int wrap = p->wrap;
	int writep = p->writep;
	int *tap = p->tap;
	double *buf = p->buf;
	
	coef2 = p->damping;
	coef1 = 1.0 - coef2;
	lastout = p->lastout;

	readp = (writep-tap[0]) & wrap;
	y = buf[readp];
	
	readp = (writep-tap[1]) & wrap;
	*outL -= buf[readp];
	
	readp = (writep-tap[2]) & wrap;
	*outR += buf[readp];
	
	readp = (writep-tap[3]) & wrap;
	*outR += buf[readp];

	p->buf[writep] = x;
	writep = (writep+1)&wrap;
	
	// onepole smoothing
	y = coef1 * y + coef2 * lastout;
	
	lastout = y;
	p->writep = writep;
	p->lastout = lastout;
	return(y);
}


double tapdelay1_do_right(g_tapdelay *p, double x, double *outL, double *outR)
{
	double y, coef1, coef2, lastout;
	int readp;
	int wrap = p->wrap;
	int writep = p->writep;
	int *tap = p->tap;
	double *buf = p->buf;
	
	coef2 = p->damping;
	coef1 = 1.0 - coef2;
	lastout = p->lastout;
	
	readp = (writep-tap[0]) & wrap;
	y = buf[readp];
	
	readp = (writep-tap[1]) & wrap;
	*outL += buf[readp];
	
	readp = (writep-tap[2]) & wrap;
	*outL += buf[readp];
	
	readp = (writep-tap[3]) & wrap;
	*outR -= buf[readp];

	p->buf[writep] = x;
	writep = (writep+1)&wrap;
	
	// onepole smoothing
	y = coef1 * y + coef2 * lastout;				// DENORM!!!!

	lastout = y;
	p->writep = writep;
	p->lastout = lastout;
	return(y);
}


double tapdelay2_do_left(g_tapdelay *p, double x, double *outL, double *outR)
{
	double y;
	int readp;
	int wrap = p->wrap;
	int writep = p->writep;
	int *tap = p->tap;
	double *buf = p->buf;
	
	readp = (writep-tap[0]) & wrap;
	y = buf[readp];
	
	readp = (writep-tap[1]) & wrap;
	*outL -= buf[readp];
	
	readp = (writep-tap[2]) & wrap;
	*outR += buf[readp];
	
	p->buf[writep] = x;
	writep = (writep+1)&wrap;
	
	p->writep = writep;

	return(y);
}

double tapdelay2_do_right(g_tapdelay *p, double x, double *outL, double *outR)
{
	double y;
	int readp;
	int wrap = p->wrap;
	int writep = p->writep;
	int *tap = p->tap;
	double *buf = p->buf;
	
	readp = (writep-tap[0]) & wrap;
	y = buf[readp];
	
	readp = (writep-tap[1]) & wrap;
	*outL += buf[readp];
	
	readp = (writep-tap[2]) & wrap;
	*outR -= buf[readp];

	p->buf[writep] = (x);
	writep = (writep+1)&wrap;
	
	p->writep = writep;
	
	return(y);
}


