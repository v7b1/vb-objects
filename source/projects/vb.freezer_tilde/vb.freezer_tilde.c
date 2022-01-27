
#include "ext.h"				
#include "ext_obex.h"
#include "ext_common.h"
#include "z_dsp.h"
#include "ext_buffer.h"
#include "ext_systhread.h"
#include <fftw3.h>

#define vDSP 1

#ifdef vDSP
#include <Accelerate/Accelerate.h>
#endif


#define MAXFFTSIZE      16777216 // 1<<24        // about 6 min. 20 seconds @ sr: 44.1 kHz
#define RAND_SCALE	    1.0f/RAND_MAX
#define MY_PI			3.14159265
#define VERSION			"1.1.2"


/****************************************************
 
	by volker boehm, 2011
	update to max 6.1.2 SDK, april 2013  - dropped support for max5...
	threading implemented, dezember 2014
	changed FFTW to float - feb. 2015
	small bugfixes, august 2015
    update fftwlib, august 2019
    increase maximum FFT size, august 2020
 
 ****************************************************/




void *myObj_class;

typedef struct _myObj {	
	t_pxobject	l_obj;
	t_buffer_ref *bufref;
	t_symbol	*bufname;	// buffer name
	void			*outB;		// bang out when done processing
	void			*outA;		// output size of FFT
	unsigned int n, nrec;		// size of allocated memory for FFT, nrec: max size of rec buffer in samps
	unsigned int reclen;		// current RECsize
	fftwf_complex *spec;
	fftwf_plan	pIN ;
	fftwf_plan	pOUT;
	float		*in;
	float		*out;
	float		*ringbuf;	// hold audio input
	float		*wind;
	float		thresh;
	float		bgain, fgain, sr;
	unsigned int	index;
	short		doit, mustRealloc;
	short		busy;
	short		siginconnected;			// detect if input signal is connected
	
	t_systhread		x_systhread;		// thread reference
	t_systhread_mutex	x_mutex;		// mutual exclusion lock for threadsafety
	void				*x_qelem;
} t_myObj;


void myObj_setBuf(t_myObj *x, t_symbol *s);
void myObj_thresh(t_myObj *x, double t);
void myObj_bgain(t_myObj *x, double f);
void myObj_fgain(t_myObj *x, double f);
void myObj_set_reclen(t_myObj *x, double t);
void myObj_bang(t_myObj *x); 
void myObj_clear(t_myObj *x);
void myObj_freezeBuf(t_myObj *x);
void do_freezeBuf(t_myObj *x);
void *do_freeze(t_myObj *x);
float getRand();
void myObj_dblclick(t_myObj *x);

void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, 
				 long maxvectorsize, long flags);
void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					 double **outs, long numouts, long sampleframes, long flags, void *userparam);

void start_thread(t_myObj *x);
void stop_thread(t_myObj *x);
void output_info(t_myObj *x);

void myObj_assist(t_myObj *x, void *b, long m, long a, char *s);
void myObj_free(t_myObj *x);
int allocMem(t_myObj *x, unsigned int n);
int allocRecMem(t_myObj *x, unsigned int nrec);
t_max_err myObj_notify(t_myObj *x, t_symbol *s, t_symbol *msg, void *sender, void *data);
void *myObj_new(t_symbol *s, long nrec);
void *myObj_class;

void myObj_info(t_myObj *x);


//--------------------------------------------------------------------------

int C74_EXPORT main(void)
{
	t_class *c;
	
	c = class_new("vb.freezer~", (method)myObj_new, (method)myObj_free, sizeof(t_myObj), 0L, 
				  A_SYM, A_DEFLONG, 0);
	class_addmethod(c, (method)myObj_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c, (method)myObj_setBuf, "set", A_SYM, 0L);
	class_addmethod(c, (method)myObj_thresh, "thresh", A_FLOAT, 0);
	class_addmethod(c, (method)myObj_bgain, "bgain", A_FLOAT, 0);
	class_addmethod(c, (method)myObj_fgain, "fgain", A_FLOAT, 0);
	class_addmethod(c, (method)myObj_freezeBuf, "freeze_buf", 0);
	//class_addmethod(c, (method)myObj_set_reclen, "reclen", A_FLOAT, 0);
	class_addmethod(c, (method)myObj_clear, "clear", 0);
	class_addmethod(c, (method)myObj_assist,"assist", A_CANT,0);
	class_addmethod(c, (method)myObj_notify, "notify", A_CANT, 0);
	class_addmethod(c, (method)myObj_bang, "bang", 0);
	class_addmethod(c, (method)myObj_info, "info", 0);
	class_addmethod(c, (method)myObj_dblclick, "dblclick", A_CANT, 0);
	
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	myObj_class = c;
	
	post("*** vb.freezer~ by volker böhm, version %s -- 64-bit", VERSION);
	
	return 0;
}

void myObj_info(t_myObj *x) {
	post("Loop size: %ld\nRec size: %ld\nreferenced buffer~: %s", x->n, x->nrec, x->bufname->s_name);
}

float getRand() {
	//random floats [-1., 1.]
	float out = ((float)rand() * RAND_SCALE)*2.f-1.f;
	return out;
}

float fastSine(float x) {
	// input must be in range -1 +1 !
	if( x > 0 )
		return 1.27323954f * x - .405284735f * x * x;
	else
		return 1.27323954f * x + .405284735f * x * x;
}

float fastCosine(float x) {
	// input must be in range -1 +1 !
	// cosine: sin(x + PI/2) = cos(x)
	x += 1.57079632;
	if (x >  3.14159265)
		x -= 6.28318531;
	
	if( x > 0 )
		return 1.27323954f * x - .405284735f * x * x;
	else
		return 1.27323954f * x + .405284735f * x * x;
}


void myObj_bgain(t_myObj *x, double f) {
	x->bgain = CLAMP(f, 0., 1.0f);
}

void myObj_fgain(t_myObj *x, double f) {
	x->fgain = CLAMP(f, 0., 1.0f);
}


void myObj_thresh(t_myObj *x, double t) {
	if(t >= 0)
		x->thresh = t;
	else x->thresh = 0;
}


void myObj_bang(t_myObj *x) {
	x->doit = true;
	/*
	if(x->busy)
		object_warn((t_object *)x, "sorry, i'm busy, try later...");
	 */
}



void myObj_clear(t_myObj *x) {
	t_buffer_obj *buf = buffer_ref_getobject(x->bufref);
	object_method( (t_object *)buf, gensym("clear"));
}


void myObj_setBuf(t_myObj *x, t_symbol *s)
{
	long frames;
	t_buffer_obj	*buffer;
	
	if (!x->bufref)
		x->bufref = buffer_ref_new((t_object*)x, s);
	else
		buffer_ref_set(x->bufref, s);
	
	x->bufname = s;
	buffer = buffer_ref_getobject(x->bufref);
	
	if(!buffer) {
		object_warn((t_object *)x, "claiming buffer %s failed!", s->s_name);
		return;
	}

	frames = buffer_getframecount(buffer);
	//object_post((t_object *)x, "frames: %ld", frames);
	if( ! allocMem(x, frames)) {
		object_error((t_object *)x, "memory allocation failed!");
		return;
	}
	
}



void myObj_set_reclen(t_myObj *x, double t) {
	int tt = t * 0.001 * x->sr;
	if(tt<2000) {
		tt = 2000;
		object_warn((t_object *)x,"reclen minimum size is 2000 samples");
	}
	else if(tt>x->nrec) {
		tt = x->nrec;
		object_warn((t_object *)x,"reclen can't be larger than RECsize %f ms", (float)(x->nrec*1000./x->sr));
	}
	x->reclen = tt;
	post("reclen: %d", x->reclen);
	
}



#pragma mark freeze complete buffer contents ------

void myObj_freezeBuf(t_myObj *x)
{
    myObj_setBuf(x, x->bufname);
	if(!x->busy)
		defer_low(x, (method)do_freezeBuf, NULL,0, NULL);
}

void do_freezeBuf(t_myObj *x)
{
	t_float		*tab;
	long		frames, nchnls;
	t_buffer_obj	*b = buffer_ref_getobject(x->bufref);
	float		*in = NULL;
	float		scale;
	unsigned int	n, i, k;
	
	if(!b) return;
	tab = buffer_locksamples(b);				// access samples in buffer
	if (!tab) return;
	
	frames = buffer_getframecount(b);		// buffer size in samples
	nchnls = buffer_getchannelcount(b);		// number of channels
	
	x->busy = true;
	
	if( ! allocMem(x, frames)) {
		buffer_unlocksamples(b);
		return;
	}
		
	
	n = x->n;
	in = x->in;
	switch (nchnls) {
		case 1:
			scale = 1.f; break;
		case 2:
			scale = 0.707f; break;
		case 4:
			scale = 0.5f; break;
		case 8:
			scale = 0.333f; break;
		default:
			scale = 1.f;
			break;
	}
	
	memset(in, 0, sizeof(float) * n);		// reset FFT in buffer
	
	for(k=0; k<nchnls; k++) {
		for(i=0; i<n; i++) {
			in[i] += tab[i*nchnls+k]*scale;	// mix down all channels to mono
		}
	}
	buffer_unlocksamples(b);

	start_thread(x);
	//defer_low(x, (method)do_freeze, NULL,0, NULL);

}



#pragma mark threading business -------------------

void start_thread(t_myObj *x) {
	
	stop_thread(x);								// kill thread if, any
	
	// create new thread + begin execution
	if (x->x_systhread == NULL) {
		//post("starting a new thread");
		systhread_create((method)do_freeze, x, 0, 0, 0, &x->x_systhread);
	}
}


void stop_thread(t_myObj *x) {
	unsigned int ret;
	
	if (x->x_systhread) {
		//post("stopping our thread");	
		systhread_join(x->x_systhread, &ret);					// wait for the thread to stop
		x->x_systhread = NULL;
	}
}



void *do_freeze(t_myObj *x) 
{	
	unsigned int i, k;
	int n, n2;
	fftwf_complex *spec;
	float		rnd, mag, thresh;
	float		*in;
	float		*out;
	t_float		*tab;
	long		frames, nchnls;
	t_buffer_obj	*b;
	float		scale;
	float		fgain, bgain;
	short		mustRealloc = 0;
	
	systhread_mutex_lock(x->x_mutex);
	spec = x->spec;
	b = buffer_ref_getobject(x->bufref);
	n = x->n;
	in = x->in;
	out = x->out;
	thresh = x->thresh;
	fgain = x->fgain;		// gain for frozen material
	bgain = x->bgain;	// gain for buffer contents
	systhread_mutex_unlock(x->x_mutex);
	
	n2 = n/2;
	scale = 1.0/n;
	
	
	//double time = systimer_gettime();
	
	if(!b) { // check if buffer is ok
		goto out;
	}
	
	fftwf_execute(x->pIN);				//---- do forward FFT

	in[0] = 0.0;					// zero out DC bin
	in[n2] = 0.0;

#ifdef vDSP
	
	for(i=1; i<n2; i++)
		in[i] = (spec[i][0]*spec[i][0] + spec[i][1]*spec[i][1]);		// calc magnitude
	vvsqrtf(in, in, &n2);	
	
	if(thresh>0)
		vDSP_vthres(in, 1, &thresh, in, 1, n2);
		
#else
	for(i=1; i<n2; i++) {
		mag = sqrtf(spec[i][0]*spec[i][0] + spec[i][1]*spec[i][1]);		// calc magnitude
		if(mag>=thresh) {
			in[i] = mag;			// use FFT in buffer to store magnitudes
		}
		else {
			in[i] = 0.0;
		}
	}
#endif
	

	frames = buffer_getframecount(b);		// buffer size in samples
	nchnls = buffer_getchannelcount(b);		// number of channels
	
	
	// check length of buffer we're writing to. has it changed meanwhile?
	if(frames != n) {
		if( frames > MAXFFTSIZE ) {
			n = MAXFFTSIZE;
			object_warn((t_object *)x,"maximum buffer size reached! cropping loop to %ld samples.", MAXFFTSIZE);
		}
		else {
			if(frames < n) {
				object_warn((t_object*)x, "buffer %s to small! truncating loop...", x->bufname->s_name);
				n = frames;
			}
			mustRealloc = 1;		// remember to realloc memory after this freeze.
			object_warn((t_object*)x, "will realloc after this freeze...");
		}
	}
	
	
	for(k=0; k<nchnls; k++) {		// step through buffer channels (k)
		
		for(i=1; i<n2; i++) {
			rnd = getRand() * MY_PI ;		// randomize phases
			//spec[i][0] =  in[i] * cosf(rnd);
			//spec[i][1] =  in[i] * sinf(rnd);
			
			spec[i][0] =  in[i] * fastCosine(rnd);
			spec[i][1] =  in[i] * fastSine(rnd);
			
			//spec[i][0] = spec[i][1] =  in[i] * rnd;
			/*
			rnd = getRand();
			spec[i][0] =  in[i] * rnd;
			rnd = getRand();
			spec[i][1] =  in[i] * rnd;
			 */
		} 
		// zero out DC and Nyquist
		spec[0][0] = 0;
		spec[0][1] = 0;
		spec[n2][0] = 0;
		spec[n2][1] = 0;
		
		fftwf_execute(x->pOUT);			//---- do inverse FFT
		
		
		//-- copy to buffer and scale (watch out for number of channels) --
		tab = buffer_locksamples(b);				// access samples in buffer
		if (!tab) {
			buffer_unlocksamples(b);
			goto out;
		}
			
		
		for(i=0; i<n; i++) {
			//tab[i*nchnls+k] = x->in[i];		// test input....
			tab[i*nchnls+k] = tab[i*nchnls+k] *bgain + (out[i] * scale)*fgain;
		}
		buffer_unlocksamples(b);
	}
	
	//object_post((t_object *)x, "proc time: %f", systimer_gettime() - time);
	
	//buffer_unlocksamples(b);
	
	buffer_setdirty(b);

out:
	systhread_mutex_lock(x->x_mutex);
	x->mustRealloc = mustRealloc;
	x->busy = false;
	//x->frames = n;	// in case buffer was resized, use this to realloc in 'output_info'
	systhread_mutex_unlock(x->x_mutex);
	
	// bang when finished processing
	qelem_set(x->x_qelem);
	
	systhread_exit(0);
	return NULL;
	
}


void output_info(t_myObj *x) {
	unsigned int n;
	short		mustRealloc;
	
	systhread_mutex_lock(x->x_mutex);
	n = x->n;
	mustRealloc = x->mustRealloc;
	systhread_mutex_unlock(x->x_mutex);
	
	outlet_bang(x->outB);
	outlet_int(x->outA, n);
	
	if(mustRealloc) myObj_setBuf(x, x->bufname);
		//allocMem(x, frames);
}



#pragma mark DSP routines ----------------

void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
	x->siginconnected = count[0];	// check if signal is connected to input
	
	myObj_setBuf(x, x->bufname);
	object_method(dsp64, gensym("dsp_add64"), x, myObj_perform64, 0, NULL);
}



void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					 double **outs, long numouts, long sampleframes, long flags, void *userparam)
{
	t_double *insamp = ins[0];
	int vs = sampleframes;
	float	*ringbuf = x->ringbuf;
	float	*wind = x->wind;
	unsigned int index = x->index;
	//unsigned int nrec = x->nrec;
	unsigned int reclen = x->reclen;
	unsigned int i, count;
	float		*in = NULL;
	
	if( !x->siginconnected )	// if no signal is connected to inlet, don't do anything here.
		return;
	
	// overrule for now...
	reclen = x->nrec;		// make the recording length always the max. recording length
	
	if(x->doit && !x->busy) {
		x->doit = false;
		x->busy = true;
		in = x->in;
		if(!in) {
			post("aaaachtung!");
			return;	// if referenced buffer doesn't exist, no mem is alloc, so better check
		}
		
		memset(in, 0, sizeof(float)*x->n);		// reset FFT in buffer -- do i need this every time?
		
		// copy contents of ringbuffer into fft input
		/*
		 count = nrec-index;
		 memcpy(in, ringbuf+index, sizeof(double)*count);
		 memcpy(in+count, ringbuf, sizeof(double)*index);
		 */
		count = 0;
		for(i=index; i<reclen; i++) {
			*in++ = wind[count] * ringbuf[i];
			count++;
		}
		
		for(i=0; i<index; i++) {
			*in++ = wind[count] * ringbuf[i];
			count++;
		}
		
		//defer_low(x, (method)do_freeze, NULL, 0, NULL);
		defer_low(x, (method)start_thread, NULL, 0, NULL);
	}

	while(vs--) {
		ringbuf[index] = *insamp++;
		index++;
		if(index>=reclen) index = 0;
	} 
	
	x->index = index;
	
}



void myObj_dblclick(t_myObj *x)
{
	buffer_view(buffer_ref_getobject(x->bufref));
}


int allocRecMem(t_myObj *x, unsigned int nrec)		// nrec: size of recbuffer
{
	int i;

	//----------- allocate memory for ringbuffer ------------------------
	x->ringbuf = (float*)sysmem_newptrclear( nrec * sizeof(float));		
	x->wind = (float*)sysmem_newptr( nrec * sizeof(float));
	// create window with 1000 samples fade time
	
	for(i=0; i<1000; i++) {
		x->wind[i] = x->wind[nrec-i-1] =  i*0.001f;	//sqrt(i*0.001);
	}
	for(i=1000; i<nrec-1000; i++)
		x->wind[i] = 1;
	 
	
	if(x->ringbuf) return 1;
	else return 0;
}
	
	
	
int allocMem(t_myObj *x, unsigned int n)		// n: size of buffer
{
	//object_post((t_object *)x, "enter allocMem");
	if( n != x->n ) {
		//object_post((t_object *)x, "have to resize memory!");
		if( n > MAXFFTSIZE ) {
			n = MAXFFTSIZE;
			object_warn((t_object *)x,"maximum buffer size reached! cropping loop to %ld samples.", MAXFFTSIZE);
		}
		else if( n < x->nrec ) {
			n = x->nrec;
			object_warn((t_object *)x,"buffer size is smaller than RECsize %d", x->nrec);
		}
		//----------- allocate memory and create plan
		if(x->in) fftwf_free(x->in);
		x->in = (float *) fftwf_malloc(sizeof(float) * n);
		memset(x->in, 0, sizeof(float)*n);
		if(x->in==NULL) {
			object_error((t_object *)x,"error allocating memory!");
			return 0;
		}
		if(x->spec) fftwf_free(x->spec);
		x->spec = (fftwf_complex *) fftwf_malloc(sizeof(fftwf_complex) * (n/2+1));
		if(x->spec==NULL) {
			object_error((t_object *)x,"error allocating memory!");
			return 0;
		}
		if(x->out) fftwf_free(x->out);
		x->out = (float *) fftwf_malloc(sizeof(float) * n);
		if(x->out==NULL) {
			object_error((t_object *)x,"error allocating memory!");
			return 0;
		}
		/*
		 If you want to transform a different array of the same size, you can create a 
		 new plan with fftw_plan_dft_1d and FFTW automatically reuses the information 
		 from the previous plan, if possible.
		 */
		//if(x->pIN) fftw_destroy_plan(x->pIN);
		x->pIN = fftwf_plan_dft_r2c_1d( n, x->in, x->spec, FFTW_ESTIMATE);
		//if(x->pOUT) fftw_destroy_plan(x->pOUT);
		x->pOUT = fftwf_plan_dft_c2r_1d( n, x->spec, x->out, FFTW_ESTIMATE);
		
		x->n = n;
		//object_post((t_object *)x, "fftw memory allocated: %d sampels", x->n);
	}
	return 1;
}
	



t_max_err myObj_notify(t_myObj *x, t_symbol *s, t_symbol *msg, void *sender, void *data)
{
	return buffer_ref_notify(x->bufref, s, msg, sender, data);
}


//--------------------------------------------------------------------------

// s: buffer name, nrec: size of recording buffer (rest of FFT buffer is zero)
void *myObj_new(t_symbol *s, long nrec) {
	
	t_myObj *x;	
	x = (t_myObj *)object_alloc(myObj_class); 
	
	dsp_setup((t_pxobject*)x, 1);
	
	x->outB = bangout(x);		// create an outlet for bangs (right)
	x->outA = intout(x);			// outlet for FFT size (left)
	
	
	x->sr = sys_getsr();
	if(x->sr<=0) x->sr = 44100.f;
	
	x->n = 0;
	if(nrec<=0) nrec = 250;		// default to 250 ms
	else if(nrec<=2000/x->sr) nrec = 2000/x->sr;		// size of recbuffer - must be at least as long as fade in/out	
	x->nrec = (unsigned int) (nrec * 0.001 * x->sr);	// size of rec buffer in samples
	x->reclen = x->nrec;

	allocRecMem(x, x->nrec);
	
	x->bufref = NULL;
	x->bufname = s;	
	
	x->index = 0;
	x->thresh = 0;
	x->bgain = 1.f;
	x->fgain = 1.f;
	x->mustRealloc = 0;
	x->doit = false;
	x->busy = false;
	x->siginconnected = false;
	//x->frames = 0;
	
	x->x_systhread = NULL;
	systhread_mutex_new(&x->x_mutex,0);
	x->x_qelem = qelem_new(x,(method)output_info);
	
	return x;
}


//--------------------------------------------------------------------------

void myObj_assist(t_myObj *x, void *b, long m, long a, char *s) 
{
	if (m==ASSIST_INLET)
		sprintf(s, "(signal) audio in, (bang) start freeze");
	else {
		switch(a) {
			case 0:
				sprintf(s, "(int) Loop size"); break;
			case 1:
				sprintf(s, "bang when done processing"); break;
		}
	}
}


void myObj_free(t_myObj *x) {
	//unsigned int ret;
	dsp_free((t_pxobject *)x);
	stop_thread(x);
	
	// free our qelem
	if (x->x_qelem) qelem_free(x->x_qelem); 
	
	// free our mutex
	if (x->x_mutex) systhread_mutex_free(x->x_mutex);
	//systhread_join(x->x_systhread, &ret);
	
	object_free(x->bufref);
	if(x->wind) sysmem_freeptr(x->wind);
	if(x->ringbuf) sysmem_freeptr(x->ringbuf);
	if(x->in) fftwf_free(x->in);
	if(x->out) fftwf_free(x->out);
	if(x->spec) fftwf_free(x->spec);
	if(x->pIN) fftwf_destroy_plan(x->pIN);
	if(x->pOUT) fftwf_destroy_plan(x->pOUT);
}
