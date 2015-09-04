#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"
#include "ext_common.h"

#include <Accelerate/Accelerate.h>	// speed things up, using vector optimization

#define WSIZE 4096
#define WSIZE2 4098
#define DELTIME 4096



//	sinewave-oscillator + feedback
//	author: volker böhm
//	all rights reserved, september 06


// update to max6, september 2012
// new version with signal inlets and update to SDK 6.1.2, april 2013
// update to SDK 6.1.4, vb, 12.07.2015


typedef struct {
	t_pxobject b_ob;
	double	phasor, last, hp_x, hp_y;
	double	r_sr, incr, cf, hp_a0, hp_b1;
	t_uint16	writer, vs;				
	t_double	*vecA, *vecB, *delTime, *fb;
	double	*wtable;			// wavetable
	double	*delbuffer;
	short	inouts[4];
} t_myObj;


void myObj_int(t_myObj *x, long f);
void myObj_float(t_myObj *x, double f);
void myObj_freq(t_myObj *x, double input);		// set freq of oscillator
void myObj_fb(t_myObj *x, double input);		// set feedback
void myObj_cf(t_myObj *x, double input);			// set cutoff freq of lowpass filter
void myObj_del(t_myObj *x, double input);		// set delay time

void myObj_dsp(t_myObj *x, t_signal **sp, short *count);
t_int *myObj_perform(t_int *w);

void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, 
				 long maxvectorsize, long flags);
void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					 double **outs, long numouts, long sampleframes, long flags, void *userparam);

void myObj_assist(t_myObj *x, void *b, long m, long a, char *s);
int memalloc(t_myObj *x, int n);
void *myObj_new(t_symbol *s, short argc, t_atom *argv);
void *myObj_class;
void myObj_free(t_myObj *x);

int C74_EXPORT main(void) {
	t_class *c;
	c = class_new("vb.fbosc2~", (method)myObj_new, (method)myObj_free, 
				  (short)sizeof(t_myObj), 0L, 0L);

	class_addmethod(c, (method)myObj_dsp, "dsp", A_CANT, 0);
	class_addmethod(c, (method)myObj_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c, (method)myObj_float, "float", A_FLOAT, 0);
	class_addmethod(c, (method)myObj_int, "int", A_FLOAT, 0);
	class_addmethod(c, (method)myObj_assist,"assist", A_CANT,0);
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	myObj_class = c;
	
	post("vb.fbosc2~ by volker böhm, version 1.0.1");
	
	return 0;
}

void myObj_float(t_myObj *x, double f) {
	switch (proxy_getinlet((t_object *)x)) {
		case 0: 
			myObj_freq(x, f); break;
		case 1:
			myObj_fb(x, f); break;
		case 2:
			myObj_cf(x, f); break;
		case 3:
			myObj_del(x, f); break;
	}
}

void myObj_int(t_myObj *x, long f) {
	switch (proxy_getinlet((t_object *)x)) {
		case 0: 
			myObj_freq(x, f); break;
		case 1:
			myObj_fb(x, f); break;
		case 2:
			myObj_cf(x, f); break;
		case 3:
			myObj_del(x, f); break;
	}
}

void myObj_freq(t_myObj *x, double input) {
	x->incr = input * x->r_sr;				
	if(input<0.0) x->incr *= -1;	
}

void myObj_fb(t_myObj *x, double input) {
	int i, vs = x->vs;
	for(i=0; i<vs; i++)
		x->fb[i] = input;
}

void myObj_cf(t_myObj *x, double input) {	
	x->cf = CLAMP(input, 0., 1.);
}

void myObj_del(t_myObj *x, double input) {
	input = CLAMP(input, 1, DELTIME);
	int i, vs = x->vs;
	for(i=0; i<vs; i++)
		x->delTime[i] = input;
}


void myObj_dsp(t_myObj *x, t_signal **sp, short *count) {		
	int i;
	for(i=0; i<4; i++)
		x->inouts[i] = count[i];
	
	dsp_add(myObj_perform, 7, x, sp[0]->s_vec, sp[1]->s_vec, sp[2]->s_vec, sp[3]->s_vec, 
			sp[4]->s_vec, sp[0]->s_n);

	if(sp[0]->s_sr>0)
		x->r_sr = 1.0 / sp[0]->s_sr;	
	else x->r_sr = 1.0 / 44100.0;
	
	if(x->vs != sp[0]->s_n) {
		x->vs = sp[0]->s_n;
		if( memalloc(x, x->vs) )
			object_error((t_object*)x, "mem alloc failed! this is bad...");
	}
}



t_int *myObj_perform(t_int *w) 
{
	t_myObj *x = (t_myObj*)(w[1]);
	t_float *in1 = (float *)(w[2]);			// freq in
	t_float *in2 = (float *)(w[3]);			// fb
	t_double cf = x->inouts[2] ? CLAMP(*(float *)(w[4]), 0, 1) : x->cf;		// lowpass cutoff
	t_float *in4 = (float *)(w[5]);			// delay time
	float *out= (float *)(w[6]);
	int vs = w[7];
	
	t_double *inFb;
	t_double *delTime;
	
	int i, trunc;
	double	curr, filt, last;		
	double	weight, index, samp, one;
	t_uint16	reader, writer;
	double	tempA0, tempB1, tempX, tempY;
	double	hp_a0, hp_b1, hpx, hpy;
	double	*sintab = x->wtable;
	double	*delbuf = x->delbuffer;
	double	*vecA = x->vecA;
	double	*vecB = x->vecB;

	writer = x->writer;
	one = 1.0;
	last = x->last;
	hpx = x->hp_x;
	hpy = x->hp_y;
	hp_a0 = x->hp_a0;
	hp_b1 = x->hp_b1;
	
		
	// calc onepole coeffs	(only recalc every signal vector)
	tempA0 = cf*cf;		
	tempB1 = 1.-tempA0;

	
	
	//------------------ if freq signal connected --------------------
	if(x->inouts[0]) {		
		vDSP_vspdp(in1, 1, vecA, 1, vs);
		vDSP_vabsD(vecA, 1, vecA, 1, vs);
		vDSP_vsmulD(vecA, 1, &x->r_sr, vecB, 1, vs);
		
		double adder = vecB[0] + x->phasor;
		vDSP_vrsumD(vecB, 1, &one, vecA, 1, vs);
		vDSP_vsaddD(vecA, 1, &adder, vecA, 1, vs);
		
		vDSP_vfracD(vecA, 1, vecA, 1, vs);
		x->phasor = vecA[vs-1];
	}
	else {
		vDSP_vrampD(&x->phasor, &x->incr, vecA, 1, vs);
		
		vDSP_vfracD(vecA, 1, vecA, 1, vs);
		x->phasor = vecA[vs-1]+x->incr;
	}
	
	
	// handle feedback values
	if(x->inouts[1])
		vDSP_vspdp(in2, 1, x->fb, 1, vs);
	inFb = x->fb;		
	
	
	// handle delay time
	if(x->inouts[3])
		vDSP_vspdp(in4, 1, x->delTime, 1, vs);
	delTime = x->delTime;

	
	
	
	for(i=0; i<vs; i++) {
		
		reader =  writer - (int)delTime[i];
		reader -= (reader >> 12) << 12;
		
		curr = vecA[i] + delbuf[reader] * inFb[i];

		
		// keep everything in range
		if(curr>=1) 
			curr-= (int)curr;
		else if(curr<0)
			curr = curr- (int)curr + 1;
		
		index = curr*WSIZE;
		trunc = (int)index;
		tempX = sintab[trunc];
		tempY = sintab[(trunc+1)];
		
		weight = index - trunc;			
		samp = (tempY-tempX)*weight + tempX;
		
		// onepole lowpass filter in feedback path
		filt = tempA0*samp + tempB1*last;		
		delbuf[writer] = filt;
		last = filt;
		
		// apply onepole highpass to output values
		vecB[i] = (samp - hpx)*hp_a0 + hpy*hp_b1;		
		hpx = samp;
		hpy = vecB[i];
		
		
		writer++;
		writer -= (writer >> 12) << 12;
		
	}	
	
	vDSP_vdpsp(vecB, 1, out, 1, vs);
	
	x->writer = writer;
	x->last = last;
	x->hp_x = hpx;
	x->hp_y = hpy;
	
	return w+8;		
}




void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, 
				 long maxvectorsize, long flags) {
	int i;
	for(i=0; i<4; i++)
		x->inouts[i] = count[i];
	
	object_method(dsp64, gensym("dsp_add64"), x, myObj_perform64, 0, NULL);
	
	if(samplerate>0) x->r_sr = 1.0 / samplerate;	
	else x->r_sr = 1.0 / 44100.0;
	
	if(x->vs != maxvectorsize) {
		x->vs = maxvectorsize;
		if( memalloc(x, x->vs))
			object_error((t_object*)x, "mem alloc failed! this is bad...");
	}
}


#pragma mark 64-bit dsp loop -----
// 64-bit version
void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					 double **outs, long numouts, long sampleframes, long flags, void *userparam) 
{
	t_double *in1 = ins[0];		// freq
	t_double *inFb = x->inouts[1] ? ins[1] : x->fb;		// feedback
	t_double cf = x->inouts[2] ? CLAMP(ins[2][0], 0, 1) : x->cf;		// lowpass cutoff
	t_double *delTime = x->inouts[3] ? ins[3] : x->delTime;
	t_double *out= outs[0];
	int vs = sampleframes;
	
	int i, trunc;
	double	curr, filt, last;
	double	weight, index, samp, one;
	t_uint16 reader, writer;
	double	tempA0, tempB1, tempX, tempY;
	double	hp_a0, hp_b1, hpx, hpy;
	double	*sintab = x->wtable;
	double	*delbuf = x->delbuffer;
	double	*vecA = x->vecA;
	double	*vecB = x->vecB;

	
	writer = x->writer;
	one = 1.0;
	last = x->last;
	hpx = x->hp_x;
	hpy = x->hp_y;
	hp_a0 = x->hp_a0;
	hp_b1 = x->hp_b1;
	
	// calc onepole coeffs	(only recalc every signal vector)
	tempA0 = cf*cf;
	tempB1 = 1.-tempA0;
	
	
	//------------------ if freq signal connected --------------------
	if(x->inouts[0]) {		
		vDSP_vabsD(in1, 1, vecA, 1, vs);	
		vDSP_vsmulD(vecA, 1, &x->r_sr, vecB, 1, vs);
		
		double adder = vecB[0] + x->phasor;
		vDSP_vrsumD(vecB, 1, &one, vecA, 1, vs);
		vDSP_vsaddD(vecA, 1, &adder, vecA, 1, vs);
		
		vDSP_vfracD(vecA, 1, vecA, 1, vs);
		x->phasor = vecA[vs-1];
	}
	else {
		vDSP_vrampD(&x->phasor, &x->incr, vecA, 1, vs);
		
		vDSP_vfracD(vecA, 1, vecA, 1, vs);
		x->phasor = vecA[vs-1]+x->incr;
	}
	

	for(i=0; i<vs; i++) {
		
		reader =  writer - (int)delTime[i];
		reader -= (reader >> 12) << 12;
	
		curr = vecA[i] + delbuf[reader] * inFb[i];
		
		// keep everything in range
		if(curr>=1) 
			curr-= (int)curr;
		else if(curr<0)
			curr = curr- (int)curr + 1;
		
		index = curr*WSIZE;
		trunc = (int)index;
		tempX = sintab[trunc];
		tempY = sintab[(trunc+1)];	

		weight = index - trunc;			
		samp = (tempY-tempX)*weight + tempX;
		
		// onepole lowpass filter in feedback path
		filt = tempA0*samp + tempB1*last;		
		delbuf[writer] = filt;
		last = filt;
		
		// apply onepole highpass to output values
		hpy= (samp - hpx)*hp_a0 + hpy*hp_b1;		
		hpx = samp;
		out[i] = hpy;
		

		writer++;			
		writer -= (writer >> 12) << 12;
		
	}	
	
	
	x->writer = writer;
	x->last = last;
	x->hp_x = hpx;
	x->hp_y = hpy;
	
}



int memalloc(t_myObj *x, int n) {
	//post("realloc!\n");
	x->vecA = (double*)sysmem_resizeptrclear(x->vecA, n*sizeof(double));
	if(x->vecA==NULL)
		return 1;
	x->vecB = (double*)sysmem_resizeptrclear(x->vecB, n*sizeof(double));
	if(x->vecB==NULL)
		return 1;
	x->delTime = (double*)sysmem_resizeptrclear(x->delTime, n*sizeof(double));
	if(x->delTime==NULL)
		return 1;
	x->fb = (double*)sysmem_resizeptrclear(x->fb, n*sizeof(double));
	if(x->fb==NULL)
		return 1;
	
	return 0;
}


void *myObj_new(t_symbol *s, short argc, t_atom *argv) {
	t_myObj *x = object_alloc(myObj_class);
	int i;

	dsp_setup((t_pxobject*)x, 4); 
	outlet_new((t_pxobject *)x, "signal"); 
	
	// initialize variables
	x->phasor = 0;
	x->incr = 0.;
	x->last = 0.;
	x->writer = 0;
	
	if(sys_getsr()>0)
		x->r_sr = 1.0 / sys_getsr();	
	else x->r_sr = 1.0 / 44100.0;	
	
	x->vs = sys_getblksize();
	if(x->vs<=0)
		x->vs = 64;		// just assume something...
	
	myObj_cf(x, 0.25);
	
	x->hp_y = x->hp_x = 0;
	x->hp_b1 = exp(-2*PI*25*x->r_sr);		// cf = 25 Hz
	x->hp_a0 = (1 + x->hp_b1) * 0.5;
	
	// alloc mem;
	x->vecA = (double*)sysmem_newptrclear(x->vs*sizeof(double));
	x->vecB = (double*)sysmem_newptrclear(x->vs*sizeof(double));
	x->delTime = (double*)sysmem_newptrclear(x->vs*sizeof(double));
	x->fb = (double*)sysmem_newptrclear(x->vs*sizeof(double));
	x->delbuffer = (double *)sysmem_newptrclear(sizeof(double)*DELTIME);
	x->wtable = (double *)sysmem_newptrclear(sizeof(double)*WSIZE2);
	for(i=0; i<WSIZE2; i++) {				
		x->wtable[i] = cos(2*PI*i/WSIZE);
	}
	
	
	return x;
}


void myObj_assist(t_myObj *x, void *b, long m, long a, char *s) {
	if (m==ASSIST_INLET) {
		switch (a) {
			case 0: sprintf(s,"(signal, float): frequency in Hz"); break;
			case 1: sprintf(s,"(signal, float) feedback"); break;
			case 2: sprintf(s,"(signal, float) lowpass cf (0-1)"); break;
			case 3: sprintf(s,"(signal, float) delay time in samples"); break;
		}
	}
	else {
		sprintf(s,"(signal) fbosc out");
	}
}

void myObj_free(t_myObj *x) {
	dsp_free((t_pxobject *)x);
	if(x->delbuffer)
		sysmem_freeptr(x->delbuffer);
	if(x->wtable)
		sysmem_freeptr(x->wtable);
	if(x->vecA)
		sysmem_freeptr(x->vecA);
	if(x->vecB)
		sysmem_freeptr(x->vecB);
	if(x->fb)
		sysmem_freeptr(x->fb);
	if(x->delTime)
		sysmem_freeptr(x->delTime);
}