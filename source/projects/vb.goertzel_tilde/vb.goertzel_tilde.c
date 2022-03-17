#include "ext.h"
#include "z_dsp.h"


/*
	implementation of an "optimized goertzel filter"
	based on an articel by Kevin Banks
	http://www.embedded.com/design/configurable-systems/4024443/The-Goertzel-Algorithm#
 
	volker böhm, feb 08
	revision, jan 11, vb
 */

// updated to max6, september 2012 
// updated to max6.1 SDK, april 2013


typedef struct {
	t_pxobject	b_ob;
	double		freq, coeff, scale, q1, q2, mag;
	int			N, newN;
	double		sr;
	unsigned int count;
	double		*wgauss;
	short		wflag;
} t_myObj;


void myObj_freq(t_myObj *x, double input);			// set  frequency
void myObj_setN(t_myObj *x, long input);
void init(t_myObj *x);
void myObj_setwflag(t_myObj *x, long in);
int mem_alloc(t_myObj *x, long size);

void myObj_dsp(t_myObj *x, t_signal **sp, short *count);
t_int *myObj_perform(t_int *w);

void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, 
				 long maxvectorsize, long flags);
void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					 double **outs, long numouts, long sampleframes, long flags, void *userparam);

void *myObj_new(double infreq, long n);
void myObj_assist(t_myObj *x, void *b, long m, long a, char *s);
void myObj_free(t_myObj *x);
void *myObj_class;

void ext_main(void *r)  {
	t_class *c;
	c = class_new("vb.goertzel~", (method)myObj_new, (method)myObj_free, 
				  (short)sizeof(t_myObj), 0L, A_DEFFLOAT, A_DEFLONG, 0L);
	class_addmethod(c, (method)myObj_dsp, "dsp", A_CANT, 0);
	class_addmethod(c, (method)myObj_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c, (method)myObj_assist, "assist", A_CANT,0);
	class_addmethod(c, (method)myObj_setN, "block", A_LONG, 0L);	
	class_addmethod(c, (method)myObj_setwflag, "wind", A_LONG, 0L);
	class_addmethod(c, (method)myObj_freq, "ft1", A_FLOAT, 0);
	
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	myObj_class = c;
	
	post("vb.goertzel~ by volker böhm - version 1.0.3");		
	
}

void init(t_myObj *x) {	
	x->coeff = 2 * cos( TWOPI * x->freq/x->sr );
}

void myObj_freq(t_myObj *x, double input) {	
	x->freq = CLAMP( input, 50, x->sr*0.45 );
	init(x);
}

void myObj_setN(t_myObj *x, long input) {
	if(input < 16) {
		input = 16;
		object_warn((t_object*)x, "oh! smallest possible block size is 16 samps!");
	}
	x->newN = input;
}

void myObj_setwflag(t_myObj *x, long in) {
	if(in != 0) x->wflag = 1;
	else x->wflag = 0;
}

void myObj_dsp(t_myObj *x, t_signal **sp, short *count) {	
	dsp_add(myObj_perform, 4, x, sp[0]->s_vec, sp[1]->s_vec, sp[0]->s_n);
	x->sr = sp[0]->s_sr;
	if(x->sr<=0) x->sr = 44100.0;
	
	if( mem_alloc(x, x->newN) ) {
		x->N = x->newN;
		x->scale = 2.0 / x->N ;
	}
}

void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
	x->sr = samplerate;
	if(x->sr <= 0) x->sr = 44100;	// prevent against bad driver settings
	
	if( mem_alloc(x, x->newN) ) {
		x->count = 0;
		x->N = x->newN;
		x->scale = 2.0 / x->N ;
	}
	
	object_method(dsp64, gensym("dsp_add64"), x, myObj_perform64, 0, NULL);
}


t_int *myObj_perform(t_int *w) 
{
	t_myObj *x = (t_myObj*)(w[1]);
	float *in= (float *)(w[2]);
	float *out= (float *)(w[3]);
	int vs = w[4];
	int count, N;
	double *wind = x->wgauss;
	double q0, q1, q2, mag, coeff;
	
	q1 = x->q1;
	q2 = x->q2;
	coeff = x->coeff;
	N = x->N;
	mag = x->mag;
	count = x->count;
	
	
	if(x->wflag) {			// use gaussian window
		while(vs--) {
			q0 = coeff * q1 - q2 + (*in++ * wind[count]);
			q2 = q1;
			q1 = q0;
			count++;
			
			// optimized goertzel
			if(count >= N) { 
				mag = sqrt( q1*q1 + q2*q2 - q1 *q2 * coeff ) * x->scale * 2.021623;	
				count = 0;
				q1 = q2 = 0;
			}
			*out++ = mag ;
		}
	}
	else {
		while(vs--) {
			q0 = coeff * q1 - q2 + *in++;
			q2 = q1;
			q1 = q0;
			count++;
			
			// optimized goertzel
			if(count >= N) { 
				mag = sqrt( q1*q1 + q2*q2 - q1 *q2 * coeff ) * x->scale;
				count = 0;
				q1 = q2 = 0;
			}
			*out++ = mag ;
		}
	}
	
	x->q1 = q1;
	x->q2 = q2;
	x->mag = mag;
	x->count = count;
	
	return w+5;		
}


void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					 double **outs, long numouts, long sampleframes, long flags, void *userparam)
{
	
	t_double *in= ins[0];
	t_double *out= outs[0];
	int vs = sampleframes;
	int count, N;
	double *wind = x->wgauss;
	double q0, q1, q2, mag, coeff;
	
	q1 = x->q1;
	q2 = x->q2;
	coeff = x->coeff;
	N = x->N;
	mag = x->mag;
	count = x->count;
	
	
	if(x->wflag) {			// use gaussian window
		while(vs--) {
			q0 = coeff * q1 - q2 + (*in++ * wind[count]);
			q2 = q1;
			q1 = q0;
			count++;
			
			// optimized goertzel
			if(count >= N) { 
				mag = sqrt( q1*q1 + q2*q2 - q1 *q2 * coeff ) * x->scale * 2.021623;	
				count = 0;
				q1 = q2 = 0;
			}
			*out++ = mag ;
		}
	}
	else {
		while(vs--) {
			q0 = coeff * q1 - q2 + *in++;
			q2 = q1;
			q1 = q0;
			count++;
			
			// optimized goertzel
			if(count >= N) { 
				mag = sqrt( q1*q1 + q2*q2 - q1 *q2 * coeff ) * x->scale;
				count = 0;
				q1 = q2 = 0;
			}
			*out++ = mag;
		}
	}
	
	x->q1 = q1;
	x->q2 = q2;
	x->mag = mag;
	x->count = count;
	
	
}


int mem_alloc(t_myObj *x, long size) {
	long i, n;
	if(size != x->N) {
		n = size;
		x->wgauss = (double *)sysmem_resizeptr(x->wgauss, n * sizeof(double));
		if(x->wgauss != NULL) {
			for(i=0; i<n; i++) {
				// gaussian
				double sigma = 0.4;
				x->wgauss[i] = exp( -0.5 * pow( (i-(n-1)*0.5) / (sigma*(n-1)*0.5), 2) );
			}
			return 1;
		}
		else 
			object_error((t_object*)x, "out of memory!");
			
	}
	return 0;
}


void myObj_free(t_myObj *x) {
	dsp_free((t_pxobject *)x);
	if(x->wgauss) 
		sysmem_freeptr(x->wgauss);
}


void *myObj_new(double infreq, long n) {
	int i;
	t_myObj *x = object_alloc(myObj_class);
	dsp_setup((t_pxobject*)x, 1); 
	outlet_new((t_object *)x, "signal"); 
	
	floatin(x, 1);
	
	//
	// initialize variables
	if(n<16) x->N = 1024;
	else x->N = n;
	x->newN = x->N;

	if(infreq <= 0) x->freq = 1000;
	else x->freq = infreq;
	

	x->sr = sys_getsr();	
	if(x->sr<=0) x->sr = 44100.0;
	x->scale = 2.0 / x->N ;
	x->count = 0;
	x->wflag = 0;
	x->mag = 0;
	
	// alloc mem
	x->wgauss = (double *)sysmem_newptr( x->N * sizeof(double));
	if(x->wgauss != NULL) {
		for(i=0; i<x->N; i++) {
			// blackman
			//x->wind[i] = 0.426591 - 0.496561*cos(TWOPI*i/x->N) +.076848*cos(2*TWOPI*i/x->N); 
			//x->wind[i] = 0.54 - 0.46 * cos(TWOPI*i/x->N);	// hamming
			//x->wind[i] = 0.5 - 0.5 * cos(TWOPI*i/x->N);	// hanning
			// gaussian
			double sigma = 0.4;
			x->wgauss[i] = exp( -0.5 * pow( (i-(x->N-1)*0.5) / (sigma*(x->N-1)*0.5), 2) );
		}
	}
	else
		object_error((t_object*)x, "out of memory!");
	
	init(x);
	
	return x;
}

void myObj_assist(t_myObj *x, void *b, long m, long a, char *s) {
	if (m == ASSIST_INLET) {
		switch(a) {
			case 0:
				sprintf (s,"(signal) audio in");
				break;
			case 1:
				sprintf (s,"(float) filter freq");
				break;
		}
	}
	else
		sprintf (s,"(signal) audio output");
}
