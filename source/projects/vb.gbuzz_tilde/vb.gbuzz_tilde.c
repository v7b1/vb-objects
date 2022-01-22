#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"
#include "ext_common.h"
#include <Accelerate/Accelerate.h>	// speed things up, using vector optimization

/* 
    GBUZZ - impluse train (not bandlimited)
    volker boehm, oktober 2011, https://vboehm.net

    based on Jerse/Dodge p.164:
	 
			sin( 2πft )
	 ------------------------------		a < 1
	  (1 + a*a - 2a*cos( 2πft ) )		
 
 
	update to max 6.1 SDK, april 2013
 */

// update to max 8.0 SDK, sept 2020



void *myObj_class;

typedef struct {
	t_pxobject x_obj;
	double	phase;    /* phase accumulator */
	double	a;		/* dsf parameter which controls spectral roll-off */
	double	sr, r_sr, freq;
	long	vs;				/* store vector size, for mem allocation */
	double	*numerator, *denom, *ramp, *aa;
	double	outgain;
	int		I_fcon;			/* signal connected to first inlet (freq) ? */
	int		I_acon;			/* is a signal connected to second inlet (a) ? */
} t_myObj;

void myObj_float(t_myObj *x, double input);
void myObj_int(t_myObj *x, long input);
void myObj_freq(t_myObj *x, double input);
void myObj_set_a(t_myObj *x, double aa);

void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, 
				 long maxvectorsize, long flags);
void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					 double **outs, long numouts, long sampleframes, long flags, void *userparam);
void myObj_perform64_sig(t_myObj *x, t_object *dsp64, double **ins, long numins, 
						 double **outs, long numouts, long sampleframes, long flags, void *userparam);

void myObj_free(t_myObj *x);
void *myObj_new(double freq, double aa);
void myObj_assist(t_myObj *x, void *b, long m, long a, char *s);


int C74_EXPORT main(void) {
	t_class *c;
	
	c = class_new("vb.gbuzz~", (method)myObj_new, (method)myObj_free, (short)sizeof(t_myObj), 0L, 
				  A_DEFFLOAT, A_DEFFLOAT, 0L);
	class_addmethod(c, (method)myObj_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c, (method)myObj_float, "float", A_FLOAT, 0);	
	class_addmethod(c, (method)myObj_int, "int", A_LONG, 0);
	class_addmethod(c, (method)myObj_assist, "assist", A_CANT,0);
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	myObj_class = c;
	
    post("vb.gbuzz~ - https://vboehm.net");
	
	return 0;
}


void myObj_float(t_myObj *x, double input) {
	switch( proxy_getinlet((t_object*)x) ) { 
		case 0: 
			myObj_freq(x, input);
			break;
		case 1:
			myObj_set_a(x, input);
			break;
	}
}

void myObj_int(t_myObj *x, long input) {
	switch( proxy_getinlet((t_object*)x) ) { 
		case 0: 
			myObj_freq(x, input);
			break;
		case 1:
			myObj_set_a(x, input);
			break;
	}
}


void myObj_freq(t_myObj *x, double input) {
	x->freq = fabs(input);
}


void myObj_set_a(t_myObj *x, double aa) {

	x->a = CLAMP( aa, 0., 0.99);

	if(x->a <=0.7) x->outgain = 0.5f;
	else {
		x->outgain = 0.5 - (x->a - 0.7) * 1.6555;
	}
}


#pragma mark ---------DSP functions-----------

void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, 
					 long maxvectorsize, long flags)
{
	x->I_fcon = count[0];	// first inlet
	x->I_acon = count[1];	// second inlet

	if(count[0] || count[1]) {
		object_method(dsp64, gensym("dsp_add64"), x, myObj_perform64_sig, 0, NULL);
	}
	else {
		object_method(dsp64, gensym("dsp_add64"), x, myObj_perform64, 0, NULL);
	}
		
	x->sr = samplerate;
	if(x->sr<=0) x->sr = 44100.0;
	x->r_sr = 1./x->sr;
	
	// if vector size changed, realloc memory
	if(x->vs != maxvectorsize) {
		x->vs = maxvectorsize;
		if(x->numerator) sysmem_freeptr(x->numerator);
		if(x->denom) sysmem_freeptr(x->denom);
		if(x->ramp) sysmem_freeptr(x->ramp);
		if(x->aa) sysmem_freeptr(x->aa);
		x->numerator = (double *)sysmem_newptr( x->vs  * sizeof(double));
		x->denom = (double *)sysmem_newptr( x->vs  * sizeof(double));
		x->ramp= (double *)sysmem_newptr( x->vs  * sizeof(double));
		x->aa = (double *)sysmem_newptr(sizeof(double)*x->vs);
	}
}


void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					 double **outs, long numouts, long sampleframes, long flags, void *userparam)
{
	t_double *out = outs[0];	
	int n = (int)sampleframes;
	double a, twoa, aap1, phase, inc;
	int num, offset, end;
	double *numerator = x->numerator;
	double *denom = x->denom;
	double *ramp = x->ramp;
	
	if (x->x_obj.z_disabled)
		return;
	if(x->freq<=0)
		goto zero;
		
	phase = x->phase;				// phase runs from 0 to π
	inc = x->freq*x->r_sr*TWOPI;		// cals sample increment
	
	/* create ramp from 0 to 2π at current frequency */
	num = (TWOPI-phase) / inc + 1;	// calc integer number of steps at current inc
	offset = 0;
	end = num;
	
	while(end < n) {
		vDSP_vrampD( &phase, &inc, ramp+offset, 1, num);		// fill ramp segment with num steps
		
		offset += num;		// incr offset
		phase = *(ramp+offset-1) + inc-TWOPI;		// get last phase and wrap it around 0-π
		num = (TWOPI-phase) / inc + 1;
		end = offset + num;	// incr end
	}
	// and the rest
	vDSP_vrampD( &phase, &inc, ramp+offset, 1, n-offset);
	phase = ramp[n-1] + inc;		/* increment phase once more and wrap it */
	if(phase > TWOPI)
		phase -= TWOPI;

	// ramp geht immer von 0 - 2π
	vvsin( numerator, ramp, &n);								// sin(2πft)
	vDSP_vsmulD( numerator, 1, &x->outgain, numerator, 1, n);		// scale the numerator by outgain
				   
	vvcos(denom, ramp, &n);								// cos(2πft)
	
	a = x->a;
	aap1 = 1 + a*a;			//  1 + a*a
	twoa = -2 * a;			// - 2a
	vDSP_vsmsaD( denom, 1, &twoa, &aap1, denom, 1, n);		//  * -2a + (1+a*a)
	
	vDSP_vdivD( denom, 1, numerator, 1, out, 1, n);			// divide numerator by denom --> store in out
	
	x->phase = phase;
	return;
	
zero:
	while (n--)  
		*out++ = 0.;	
}



void myObj_perform64_sig(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					 double **outs, long numouts, long sampleframes, long flags, void *userparam)
{
	t_double *in1 = ins[0];
	t_double *in2 = ins[1];
	t_double *out = outs[0];	
	int n = (int)sampleframes;		
	
	double freq;
	double a, twoa, aap1, phase, inc, freqscale;	//scale;
	int i, num, offset, end;
	double *numerator = x->numerator;
	double *denom = x->denom;
	double *ramp = x->ramp;
	double *aa = x->aa;
	
	if (x->x_obj.z_disabled)
		return;
	
	phase = x->phase;			// phase runs from 0 to 2π
	
	/*********  if freq signal not connected *********/
	if(!x->I_fcon) {		
		freq = x->freq;
		if(freq==0)
			goto zero;
		inc = freq*TWOPI * x->r_sr;	// calc sample increment
		
		/* create ramp from 0 to 2π at current frequency */
		num = (TWOPI-phase) / inc + 1;	// calc integer number of steps at current inc
		offset = 0;
		end = num;
		
		while(end < n) {
			vDSP_vrampD( &phase, &inc, ramp+offset, 1, num);		// fill ramp segement with num steps
			offset += num;		// incr offset
			phase = *(ramp+offset-1) + inc-TWOPI;		// get last phase and wrap it around 0-2π
			num = (TWOPI-phase) / inc + 1;
			end = offset + num;	// incr end
		}
		// and the rest
		vDSP_vrampD( &phase, &inc, ramp+offset, 1, n-offset);
		phase = ramp[n-1] + inc;		/* increment phase once more and wrap it */
		if(phase > TWOPI)
			phase -= TWOPI;
	}
	
	/******** freq signal connected! ********/
	else {
		freqscale = TWOPI*x->r_sr;
		vDSP_vabsD(in1, 1, in1, 1, n);		// calc absolute value, we dont want negative freq
		for(i=0; i<n; i++) {
			inc = in1[i] * freqscale;	// calc sample increment
			ramp[i] = phase;
			phase += inc;
			if(phase > TWOPI)
				phase -= TWOPI;
		}
	}
	
	// ramp contains phase values running from 0 to 2π
	vvsin( numerator, ramp, &n);							// sin(2πft)
	vvcos(denom, ramp, &n);								// cos(2πft)
	
	if(! x->I_acon) {		// a signal not connected (2nd inlet)
		a = x->a;
		aap1 = 1 + a*a;			//  1 + a*a
		twoa = -2 * a;			// - 2a
		vDSP_vsmsaD( denom, 1, &twoa, &aap1, denom, 1, n);		//  * -2a + (1+a*a)
		
		vDSP_vsmulD( numerator, 1, &x->outgain, numerator, 1, n);		// scale the numerator by outgain
	}
	else {				// a signal connected
		double zero = 0.;
		double limit = 0.99;
		double one = 1.;
		double mtwo = -2.;
		double a0;
		vDSP_vclipD( in2, 1, &zero, &limit, in2, 1, n);		// clip input values 'a'
		a0 = in2[0];
		/* denominator */
		// vector multiply and scalar add
		vDSP_vmsaD( in2, 1, in2, 1, &one, aa, 1, n);		// (a*a + 1)
		vDSP_vsmulD(in2, 1, &mtwo, in2, 1, n);			// -2a
		vDSP_vmulD( denom, 1, in2, 1, denom, 1, n);		// cos(2πft) * -2a
		vDSP_vaddD( denom, 1, aa, 1, denom, 1, n);		//  cos(2πft) * -2a + (1+a*a)
		
		// scale numerator depending on 'a'
		if(a0<=0.7) x->outgain = 0.5;		// check only first a-value of vector...???
		else  x->outgain = 0.5 - (a0 - 0.7) * 1.6555;

		vDSP_vsmulD( numerator, 1, &x->outgain, numerator, 1, n);	
	}
	
	vDSP_vdivD( denom, 1, numerator, 1, out, 1, n);			// divide numerator by denom --> store in
	x->phase = phase;
	return;
	
zero:
	while (n--)  
		*out++ = phase;
}



void *myObj_new(double freq, double aa) 
{
	t_myObj *x = object_alloc(myObj_class);
	dsp_setup((t_pxobject*)x, 2);				// two signals inlets, freq and rolloff (a)
	outlet_new((t_object *)x, "signal"); 
	
	x->sr = sys_getsr();
	if(x->sr <= 0)
		x->sr = 44100.f;
	x->r_sr = 1.0/sys_getsr();
	x->vs = sys_getblksize();
	
	x->numerator = (double *)sysmem_newptr(sizeof(double)*x->vs);
	x->denom = (double *)sysmem_newptr(sizeof(double)*x->vs);
	x->ramp = (double *)sysmem_newptr(sizeof(double)*x->vs);
	x->aa = (double *)sysmem_newptr(sizeof(double)*x->vs);
	
	x->phase = 0.0;
	x->outgain = 1.0;
	myObj_set_a(x, aa);	
	myObj_freq(x, freq);
	
	return x;
}


void myObj_assist(t_myObj *x, void *b, long m, long a, char *s) {
	if (m==1) {
		switch(a) {
			case 0: sprintf (s,"(signal, float) frequency"); break;
			case 1: sprintf (s,"(signal, float) rolloff (a)"); break;
		}
	}
	else
		sprintf (s,"(signal) buzz output");
}

void myObj_free(t_myObj *x) {
	dsp_free((t_pxobject *)x);
	if(x->numerator)
		sysmem_freeptr(x->numerator);
	if(x->denom)
		sysmem_freeptr(x->denom);
	if(x->ramp)
		sysmem_freeptr(x->ramp);
	if(x->aa)
		sysmem_freeptr(x->aa);
	
}
