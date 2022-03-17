#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"
#include "ext_common.h"
#include <Accelerate/Accelerate.h>	// speed things up, using vector optimization
#include <stdio.h>


/* 
vb, oktober 2011
 
 */


#define ONEOVERPI	(1.0 / M_PI)
#define RAND_SCALE 2.0/RAND_MAX


void *myObj_class;

typedef struct {
	t_pxobject x_obj;
	double		r_sr;
	double		sr, slide, thresh;
	double		omega;			// 2π*hop/n
	double		*inbuf, *outbuf, *collector, *wind, *buffer;
	double		*mag_input, *mag, *lastmag, *magpitch, *magthresh;
	double		*phdiff_input, *phase, *phdiff, *lastphdiff, *phaspitch;
	FFTSetupD	setupReal;
	DSPDoubleSplitComplex A;			// DSPDoubleComplex
	unsigned int	n, log2n, ov;
	unsigned int	nnew, log2nnew;
	int			count, idx, idxout;
	int			freeze;			// flag
	double		pitscale, incr, grain, fb;
	double		scalenoise;
	short		proc;
	
} t_myObj;


void myObj_fb(t_myObj *x, double a);
void myObj_slide(t_myObj *x, double a);
void myObj_rampsmooth(t_myObj *x, double a);
void myObj_thresh(t_myObj *x, double a);
void myObj_set_transpose(t_myObj *x, double a);
void myObj_grain(t_myObj *x, double a);
void myObj_clear(t_myObj *x);
void myObj_proc(t_myObj *x, long input);
void myObj_setn(t_myObj *x, long fftsize);
void myObj_bang(t_myObj *x);

void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, 
				 long maxvectorsize, long flags);
void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					 double **outs, long numouts, long sampleframes, long flags, void *userparam);


void allocMem(t_myObj *x);
void myObj_free(t_myObj *x);
void *myObj_new(long fftsize, long overlap);
void myObj_assist(t_myObj *x, void *b, long m, long a, char *s);

double phasewrap( double input );
void phaseunwrap( double *phasdiff, double omega, int n2 );
void transpose( double *magin, double *magout, double *phin, double *phout, double pitscale, int n2);
double getRand();


void ext_main(void *r) {
	t_class *c;
	
	c = class_new("vb.evolution~", (method)myObj_new, (method)myObj_free, (short)sizeof(t_myObj), 
				  0L, A_LONG, A_LONG, 0L);
	class_addmethod(c, (method)myObj_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c, (method)myObj_slide, "slide", A_FLOAT, 0);
	class_addmethod(c, (method)myObj_rampsmooth, "rampsmooth", A_FLOAT, 0);
	class_addmethod(c, (method)myObj_thresh, "thresh", A_FLOAT, 0);
	class_addmethod(c, (method)myObj_grain, "grain", A_FLOAT, 0);
	class_addmethod(c, (method)myObj_set_transpose, "transp", A_FLOAT, 0);
	class_addmethod(c, (method)myObj_bang, "bang", 0);
	class_addmethod(c, (method)myObj_clear, "clear", 0);
	class_addmethod(c, (method)myObj_fb, "fb", A_FLOAT, 0);
	//class_addmethod(c, (method)myObj_setn, "setn", A_LONG, 0);
	class_addmethod(c, (method)myObj_proc, "proc", A_LONG, 0);
	class_addmethod(c, (method)myObj_assist, "assist", A_CANT,0);
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	myObj_class = c;
	
	srand((unsigned int)time(NULL));
	post("vb.evolution~ by volker böhm https://vboehm.net ");
	
}


void myObj_proc(t_myObj *x, long input) {
	if(input) x->proc = 1;
	else x->proc = 0;
}


void myObj_setn(t_myObj *x, long fftsize) {
	if(fftsize < 128) fftsize = 128;
	else if(fftsize > 32768) fftsize = 32768;
	x->log2nnew = (int)log2(fftsize);
	x->nnew = 1 << x->log2nnew;
}


void myObj_bang(t_myObj *x) {
	if(!x->freeze)
		x->freeze = 1;
}


void myObj_clear(t_myObj *x) {
	vDSP_vclrD(x->mag, 1, x->n/2);
	vDSP_vclrD(x->lastmag, 1, x->n/2);
	vDSP_vclrD(x->mag_input, 1, x->n/2);
}


void myObj_fb(t_myObj *x, double a) {
	x->fb = CLAMP(a, 0, 1);
}



void myObj_thresh(t_myObj *x, double a) {
	if(a>0) x->thresh = a;
	else	
		x->thresh = DBL_EPSILON;	//we use thresh also to kill denormals
	
	//post("FLT_EPSILON: %1.12f", FLT_EPSILON);
}


void myObj_slide(t_myObj *x, double a) {
	x->slide = 1. - sqrt(CLAMP(a, 0., 1.0));
}

// entweder slide oder rampsmooth -- wobei slide kein wirklicher slide ist!

void myObj_rampsmooth(t_myObj *x, double a) {
	double frms;
	if(a >= 0) {
		frms = a*x->sr*x->ov / x->n;
		x->slide = 1./frms;
	}
	else x->slide = 1.;
	
	//post("slide: %f", x->slide);
}

void myObj_grain(t_myObj *x, double a) {
	double b = CLAMP(a, 0., 1.);
	x->grain = b*b * M_PI;
}

void myObj_set_transpose(t_myObj *x, double a) {
	x->pitscale = pow(2, (a/12.));
}




void calc_phaseDiff(t_myObj *x, double *collector, double *inbuf, int pos) {
	
	int count, n, n2;
	int bufmax, start, hopsize, len, end;
	double scale = 0.5;
	double	*mag_input = x->mag_input;
	double	*phdiff_input = x->phdiff_input;
	count = x->count;
	n = x->n;
	n2 = n/2;

	hopsize = n/x->ov;
	bufmax = n+hopsize;
	
	start = pos - n;
	//start = (pos+hopsize) % bufmax;
	
	if(start >= 0) {
		//post("start positive");
		// no need to copy data
		vDSP_vmulD(collector+start, 1, x->wind, 1, inbuf, 1, n);
	}
	else {
		len = abs(start);
		start = bufmax-len;
		memcpy(inbuf, collector+start, len*sizeof(double));
		memcpy(inbuf+len, collector, (n-len)*sizeof(double));
		
		vDSP_vmulD(inbuf, 1, x->wind, 1, inbuf, 1, n);
	}
	
	vDSP_ctozD ( ( DSPDoubleComplex * ) inbuf, 2, &x->A, 1, n2 );
	vDSP_fft_zripD( x->setupReal, &x->A, 1, x->log2n, FFT_FORWARD);			// FFT
	vDSP_vdistD(x->A.realp, 1, x->A.imagp, 1, mag_input, 1, n2);
	vDSP_vsmulD(mag_input, 1, &scale, mag_input, 1, n2);
	vDSP_zvphasD(&x->A, 1, phdiff_input, 1, n2);
	
	
	// 1 frame back
	start = pos;
	end = start+n;
	if(end <= bufmax) {
		// no need to copy data
		vDSP_vmulD(collector+start, 1, x->wind, 1, inbuf, 1, n);
	}
	else {
		len = bufmax-start;
		//post("1. start: %d, len: %d, end: %d ", start, len, start+len);
		memcpy(inbuf, collector+start, len*sizeof(double));
		memcpy(inbuf+len, collector, (n-len)*sizeof(double));
		
		vDSP_vmulD(inbuf, 1, x->wind, 1, inbuf, 1, n);
	}
	
	
	vDSP_ctozD( ( DSPDoubleComplex * ) inbuf, 2, &x->A, 1, n2 );
	vDSP_fft_zripD( x->setupReal, &x->A, 1, x->log2n, FFT_FORWARD);			// FFT
	vDSP_zvphasD(&x->A, 1, x->A.realp, 1, n2);					// store phases in realpart...
	
	// calculate phase differences
	vDSP_vsubD(x->A.realp, 1, phdiff_input, 1, phdiff_input, 1, n2);

	phaseunwrap(phdiff_input, x->omega, n2);
	
	x->freeze = 0;	// ok, waiting for next freeze
}




//64-bit dsp method
void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, 
				 long maxvectorsize, long flags) {
	object_method(dsp64, gensym("dsp_add64"), x, myObj_perform64, 0, NULL);
	
	x->sr = samplerate;
	if(x->sr<=0) x->sr = 44100.0;
	x->r_sr = 1./x->sr;
	
}


void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					 double **outs, long numouts, long sampleframes, long flags, void *userparam) {
	int i;
	t_double *in = ins[0];
	t_double *out = outs[0];
	long vs = sampleframes;
	int	count, idx, idxout;
	int  n, n2, bufmax, hopsize;
	double scale, incr, slide, grain, fb; //slideout;
	double reversescale;
	
	double	*collector = x->collector;
	double	*inbuf = x->inbuf;
	double	*outbuf = x->outbuf;
	double	*buffer = x->buffer;
	double	*mag = x->mag;
	double	*phase = x->phase;
	double	*phdiff = x->phdiff;
	double	*phaspitch = x->phaspitch;
	double	*magpitch = x->magpitch;
	double	*magthresh = x->magthresh;
	DSPDoubleSplitComplex	A = x->A;
	
	
	if (x->x_obj.z_disabled)
		goto out;
	if( !x->proc )
		goto zero;
	
	n = x->n;
	n2 = n/2;
	//scale = 1.0 / (2*n);			// was: 1.0 / (n);
    scale = 2.0 / (n*x->ov);
	hopsize = n/x->ov;
	bufmax = n+hopsize;
	count = x->count;
	idx = x->idx;
	idxout = x->idxout;
	incr = x->incr;
	slide = x->slide;
	grain = x->grain;
	fb = x->fb;
	
	if(x->freeze==1) {
		x->freeze = -1;		// unset freeze, so it won't retrigger before last freeze is done
		/*
		 if(x->n != x->nnew) {		// check if fftsize changed!
		 x->n = x->nnew;
		 x->log2n = x->log2nnew;
		 vDSP_hann_window(x->wind, x->n, 0);
		 n = x->n;
		 n2 = n/2;
		 scale = 1.0 / (2*n);
		 hopsize = n/x->ov;
		 bufmax = n+hopsize;
		 }*/
		
		memcpy(x->lastmag, mag, n2*sizeof(double));
		memcpy(x->lastphdiff, phdiff, n2*sizeof(double));
		calc_phaseDiff(x, collector, inbuf, count);			// bei neuer FFTsize CRASH!!!!!!!!!!!!!!!!!!!!!!!
		incr = 0.f;			//reset interpolation
	}
	
	if(idx>=hopsize) {
		idx = 0;
		
		// linear interpolation
		if(incr<=1) {
			// try something with feedback instead
			reversescale = (fb - 1)*incr + 1;		//reverse ramp
			vDSP_vsmulD(x->mag_input, 1, &incr, mag, 1, n2);
			vDSP_vsmaD(x->lastmag, 1, &reversescale, mag, 1, mag, 1, n2);
			
			// normal linear interpol.
			vDSP_vsbsmD(x->phdiff_input, 1, x->lastphdiff, 1, &incr, phdiff, 1, n2);
			vDSP_vaddD(phdiff, 1, x->lastphdiff, 1, phdiff, 1, n2);
			incr += slide;
		}

        vDSP_vthresD(mag, 1, &x->thresh, magthresh, 1, n2);

		
		
		//if(x->pitscale != 1)
		// do transposition
		transpose(magthresh, magpitch, phdiff, phaspitch, x->pitscale, n2);
		//transpose(magthresh, A.realp, phdiff, phaspitch, x->pitscale, n2);
		/*
		 else {
		 magpitch = magthresh;
		 phaspitch = phdiff;
		 }*/
		
		
		if(grain > 0.f) {
			//float scalenoise = x->scalenoise;
			for(i=0; i<n2; i++) {
				//phaspitch[i] += getRand()*grain*i*scalenoise;
				// why scale every bin with increasing values? i don't get it anymore...
				phaspitch[i] += getRand()*grain;		
			}
		}
		//else phaspitch = phdiff;
		
		// accumulate phases and do phasewrap
		vDSP_vaddD(phase, 1, phaspitch, 1, phase, 1, n2);
		
		for(i=0; i<vs; i++) {
			//A.imagp[i] = phasewrap(phase[i]); 
			phase[i] = phasewrap(phase[i]); 
		}
			
		
		// poltocar 
		
		vvcos(x->A.realp, phase, &n2);
		vvsin(x->A.imagp, phase, &n2);
		vDSP_vmulD(x->A.realp, 1, magpitch, 1, x->A.realp, 1, n2);
		vDSP_vmulD(x->A.imagp, 1, magpitch, 1, x->A.imagp, 1, n2);
		
		// different poltocar...
		/*
		vDSP_ztocD( &A, 1, ( DSPDoubleComplex * ) buffer, 2, n2 );
		vDSP_rectD(buffer, 2, buffer, 2, n2);
		vDSP_ctozD ( ( DSPDoubleComplex * ) buffer, 2, &A, 1, n2 );		// rearrage data
		*/
		// iFFT
		vDSP_fft_zripD( x->setupReal, &A, 1, x->log2n, FFT_INVERSE);	
		vDSP_ztocD( &A, 1, ( DSPDoubleComplex * ) buffer, 2, n2 );	
		
		// apply windowing + scaling
		vDSP_vmulD(buffer, 1, x->wind, 1, buffer, 1, n);				
		vDSP_vsmulD( buffer, 1, &scale, buffer, 1, n );
		
		// overlap save
		vDSP_vaddD( buffer, 1, outbuf+hopsize, 1, buffer, 1, n-hopsize);
		memcpy(outbuf, buffer, sizeof(double)*n);
		
	}
	
	
	for(i=0; i<vs; i++) {
		collector[count] = in[i];
		count++;
		if(count>=bufmax) count = 0;
		/*
		 out_idx[i] = idx;
		 out_phdiff[i] = phdiff[idx];
		 out_mag[i] = mag[idx];
		 */
		out[i] = outbuf[idx];
		idx++;
	}
	x->count = count;
	x->idx = idx;
	x->incr = incr;
	
out:
	return;
	
zero:
	while (vs--)  {
		*out++ = 0.;
	}
}




void transpose( double *magin, double *magout, double *phin, double *phout, double pitscale, int n2) {
	
	int i, idx;
	vDSP_vclrD(magout, 1, n2);
	//memset(magout, 0, n2*sizeof(float));
	//memset(phout, 0, n2*sizeof(float));
	
	for(i=0; i<n2; i++) {
		idx = (int)(i*pitscale+0.5);
		if(idx<n2) {
			magout[idx] = magin[i];
			phout[idx] = phin[i] * pitscale;
		}
		else break;
	}
}


inline double phasewrap( double input ) {
	//long qpd = input / M_PI;
	long qpd = input * ONEOVERPI;
	if(qpd >= 0) qpd += qpd&1;
	else qpd -= qpd&1;
	
	return ( input - M_PI*(double)qpd );
}


inline void phaseunwrap( double *phasdiff, double omega, int n2 ) {
	int k;
	double omk;
	for(k=0; k<n2; k++) {
		omk = omega*k;		
		phasdiff[k] = phasewrap((phasdiff[k]-omk)) + omk;
	}
}

inline double getRand() {
	// return random numbers in range [-1, 1]
	return ((double)rand() * RAND_SCALE) -1;
}


void allocMem(t_myObj *x) {
	int n2, hopsize;
	n2 = x->n/2;
	hopsize = x->n / x->ov;
	x->scalenoise = 150./n2;	// 150 is just a number that seems to fit alright.
	x->A.realp = (double *) sysmem_newptr( n2 * sizeof( double ));
	x->A.imagp = (double *) sysmem_newptr( n2 * sizeof( double ));
	
	x->collector = (double *) sysmem_newptrclear( (hopsize+x->n) * sizeof( double ));
	x->mag_input = (double *) sysmem_newptrclear( n2 * sizeof( double ));
	x->mag = (double *) sysmem_newptrclear( n2 * sizeof( double ));
	x->lastmag = (double *) sysmem_newptrclear( n2 * sizeof( double ));
	x->magpitch = (double *) sysmem_newptrclear( n2 * sizeof( double ));
	x->magthresh = (double *) sysmem_newptrclear( n2 * sizeof( double ));
	x->phaspitch = (double *) sysmem_newptrclear( n2 * sizeof( double ));
	x->phdiff = (double *) sysmem_newptrclear( n2 * sizeof( double ));
	x->phdiff_input = (double *) sysmem_newptrclear( n2 * sizeof( double ));
	x->lastphdiff = (double *) sysmem_newptrclear( n2 * sizeof( double ));
	x->phase = (double *) sysmem_newptrclear( n2 * sizeof( double ));
	x->inbuf = (double *) sysmem_newptrclear( x->n * sizeof( double ));
	x->outbuf = (double *) sysmem_newptrclear( x->n * sizeof( double ));
	x->buffer = (double *) sysmem_newptrclear( x->n * sizeof( double ));
	x->wind = (double *) sysmem_newptr( x->n * sizeof( double ));
	
	x->setupReal = vDSP_create_fftsetupD( x->log2n, FFT_RADIX2);
	
	vDSP_hann_windowD(x->wind, x->n, 0);
	// int i;
	//for(i=0; i<x->n; i++)
		//x->wind[i] = pow(x->wind[i], 0.5);		// take square root of hanning window!
}


void *myObj_new(long fftsize, long overlap) 
{
	t_myObj *x = object_alloc(myObj_class);
	dsp_setup((t_pxobject*)x, 1);				// audio in
	//outlet_new((t_object *)x, "signal");			// 4. index out
	//outlet_new((t_object *)x, "signal");			// 3. phasediff out
	//outlet_new((t_object *)x, "signal");			// 2. mag out
	outlet_new((t_object *)x, "signal");			// 1. audio out
	
	x->sr = sys_getsr();
	if(x->sr <= 0)
		x->sr = 44100.f;
	x->r_sr = 1.0/sys_getsr();
	
	x->log2n = (int)log2(fftsize);
	x->n = 1 << x->log2n;
	//if(overlap<=2) x->ov = 2;
    overlap = CLAMP(overlap, 2, 8);
	overlap = (int)log2(overlap);
	//x->ov = pow(2, overlap);
    x->ov = 1 << overlap;
	x->omega = TWOPI / x->ov;
	object_post((t_object *)x, "fftsize: %d -- overlap: %d", x->n, x->ov);
	
	x->incr = 1.;
	x->slide = 1.;
	x->thresh = 0.;
	x->pitscale = 1.;
	x->grain = 0.;
	x->fb = 0.;
	x->count = x->idx = x->idxout = 0;
	x->freeze = 0;
	//x->l_sym = s;
	x->scalenoise = 0.2;
	x->proc = 1;
	allocMem(x);
	
	return x;
}


void myObj_assist(t_myObj *x, void *b, long m, long a, char *s) {
	if (m==1) {
		sprintf(s, "(signal) audio in");
	}
	else {
		switch(a) {
			case 0: sprintf (s,"(signal) magnitude"); break;
			case 1: sprintf (s,"(signal) phase differences"); break;
			case 2: sprintf (s,"(signal) index"); break;
		}
	}
}

void myObj_free(t_myObj *x) {
	dsp_free((t_pxobject *)x);
	vDSP_destroy_fftsetupD( x->setupReal );
	if(x->wind)
		sysmem_freeptr(x->wind);
	if(x->A.realp)
		sysmem_freeptr(x->A.realp);
	if(x->A.imagp)
		sysmem_freeptr(x->A.imagp);
	if(x->mag_input)
		sysmem_freeptr(x->mag_input);
	if(x->mag)
		sysmem_freeptr(x->mag);
	if(x->lastmag)
		sysmem_freeptr(x->lastmag);
	if(x->magpitch)
		sysmem_freeptr(x->magpitch);
	if(x->magthresh)
		sysmem_freeptr(x->magthresh);
	if(x->phaspitch)
		sysmem_freeptr(x->phaspitch);
	if(x->phdiff)
		sysmem_freeptr(x->phdiff);
	if(x->phdiff_input)
		sysmem_freeptr(x->phdiff_input);
	if(x->lastphdiff)
		sysmem_freeptr(x->lastphdiff);
	if(x->phase)
		sysmem_freeptr(x->phase);
	if(x->inbuf)
		sysmem_freeptr(x->inbuf);
	if(x->outbuf)
		sysmem_freeptr(x->outbuf);
	if(x->buffer)
		sysmem_freeptr(x->buffer);
	if(x->collector)
		sysmem_freeptr(x->collector);
	
}
