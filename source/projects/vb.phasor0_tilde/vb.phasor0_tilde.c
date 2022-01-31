#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"

/* 
 a phasor (ramp generator) that reacts to frequency changes only at phase 0
 © volker boehm, mai 2007
 */

/*
 10/30/2010 -- small bug fixes and changes: (thanks to alex harker for pointing these out!)
 - slightly changed behaviour if input freq changes sign
 - guard against misreported sample rate, when having audio driver issues
 - correct behaviour if input freq lies outside +/- SR
 */

/*
 05/10/2013 -- update to max SDK 6.1
 */

typedef struct {
	t_pxobject x_obj;
	double	incr;			// increment, calculated from input
	double	freq;
	double	samp;			// sample value
	double	act;				// hold actual sample increment
	double	sr;
	double	one_over_sr;
} t_myObj;


void myObj_freq(t_myObj *x, double input);
void myObj_int(t_myObj *x, long input);
void myObj_phas(t_myObj *x, double input);
void myObj_bang(t_myObj *x);
void *myObj_new(double freq);
void myObj_assist(t_myObj *x, void *b, long m, long a, char *s);
static t_class *myObj_class;

// dsp methods
void myObj_dsp(t_myObj *x, t_signal **sp, short *count);
t_int *myObj_perform(t_int *w);
t_int *myObj_perform_noSig(t_int *w);

void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, 
				 long maxvectorsize, long flags);
void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					 double **outs, long numouts, long sampleframes, long flags, void *userparam);
void myObj_perform64_noSig(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					 double **outs, long numouts, long sampleframes, long flags, void *userparam);


void ext_main(void *r) {
	t_class *c;
	
	c = class_new("vb.phasor0~", (method)myObj_new, (method)dsp_free, (short)sizeof(t_myObj), 
				  0L, A_DEFFLOAT, 0L);
	
	class_addmethod(c, (method)myObj_dsp, "dsp", A_CANT, 0);
	class_addmethod(c, (method)myObj_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c, (method)myObj_phas, "ft1", A_FLOAT, 0);
	class_addmethod(c, (method)myObj_freq, "float", A_FLOAT, 0);	
	class_addmethod(c, (method)myObj_int, "int", A_LONG, 0);
	class_addmethod(c, (method)myObj_bang, "bang", 0);
	class_addmethod(c, (method)myObj_assist, "assist", A_CANT, 0);
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	myObj_class = c;
	
	post("vb.phasor0~ by volker böhm, version 1.05");
	
}


void myObj_phas(t_myObj *x, double input) {
	while(input < 0) input += 1.0;
	while(input >1) input -= 1.0;
	x->samp = input;
}

void myObj_freq(t_myObj *x, double input) {
	// wrap input around +/-sr
	while(input>x->sr) input -= x->sr;
	while(input<-x->sr) input += x->sr;
	x->freq = input;
	x->incr = input * x->one_over_sr;		// calculate increment
}

void myObj_bang(t_myObj *x) {
	x->act = x->incr;						// a bang forces new frequency
}

void myObj_int(t_myObj *x, long input) {
	myObj_freq(x, input);
}



#pragma mark 32-bit dsp methods ----

void myObj_dsp(t_myObj *x, t_signal **sp, short *count) {	
	x->sr = sp[0]->s_sr;
	if(x->sr <= 0 ) x->sr = 44100;
	x->one_over_sr = 1./sp[0]->s_sr;
	x->samp = 0;
	myObj_freq(x, x->freq);
	
	if (count[0]) 
		dsp_add(myObj_perform, 4, x, sp[0]->s_vec, sp[1]->s_vec, sp[0]->s_n);
	else 
		dsp_add(myObj_perform_noSig, 3, x, sp[1]->s_vec, sp[0]->s_n);		
}



t_int *myObj_perform(t_int *w) {
	t_myObj *x = (t_myObj*)(w[1]);
	float *in = (float *)(w[2]);
	float *out1 = (float *)(w[3]);	
	long vs = (long)w[4];
	int i;
	double _samp = x->samp;
	double inc = x->act;
	
	for(i=0; i<vs; i++) {
		x->incr = in[i] * x->one_over_sr;		
		
		if(_samp>=1.0) {
			inc = x->incr;
			if(inc>0) 
				while(_samp>=1.0) 
					_samp -= 1.0;
			else _samp = 1.0;				//reset if incr changes from pos to neg 
		}
		else if(_samp <= 0.0 ) {
			inc = x->incr;
			if(inc<0) 
				while(_samp<=0)
					_samp += 1.0;		
			else _samp = 0;					//reset if incr changes from neg to pos
		}		
		out1[i] = _samp;
		_samp += inc;						// increment sample value
	}
	x->samp = _samp;
	x->act = inc;
	
	return w+5;	
}


t_int *myObj_perform_noSig(t_int *w) {
	t_myObj *x = (t_myObj*)(w[1]);
	float *out1 = (float *)(w[2]);	
	long vs = (long)w[3];
	int i;
	double _samp = x->samp;
	double inc = x->act;
	
	for(i=0; i<vs; i++) {
		
		if(_samp>=1.0) {
			inc = x->incr;
			if(inc>0) _samp -= 1.0;		
			else _samp = 1.0;				//reset if incr changes from pos to neg 
		}
		else if(_samp < 0.0 ) {
			inc = x->incr;
			if(inc<0) _samp += 1.0;
			else _samp = 0;					//reset if incr changes from neg to pos
		}		
		out1[i] = _samp;
		_samp += inc;						// increment sample value
	}
	x->samp = _samp;
	x->act = inc;
	
	return w+4;	
}




#pragma mark 64 bit dsp methods ----

//64-bit dsp method
void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, 
				 long maxvectorsize, long flags) 
{
	x->sr = samplerate;
	if(samplerate <=0) x->sr = 44100.0;
	x->one_over_sr = 1./samplerate;
	x->samp = 0;
	myObj_freq(x, x->freq);
	
	if (count[0]) object_method(dsp64, gensym("dsp_add64"), x, myObj_perform64, 0, NULL);
	else object_method(dsp64, gensym("dsp_add64"), x, myObj_perform64_noSig, 0, NULL);
	
}



void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					 double **outs, long numouts, long sampleframes, long flags, void *userparam){
	
	t_double *in = ins[0];
	t_double *out = outs[0];	
	int vs = sampleframes;		
	int i;
	double _samp = x->samp;
	double inc = x->act;
	
	if (x->x_obj.z_disabled)
		return;
	
	
	for(i=0; i<vs; i++) {
		x->incr = in[i] * x->one_over_sr;		
		
		if(_samp>=1.0) {
			inc = x->incr;
			if(inc>0) 
				while(_samp>=1.0) 
					_samp -= 1.0;
			else _samp = 1.0;				//reset if incr changes from pos to neg 
		}
		else if(_samp <= 0.0 ) {
			inc = x->incr;
			if(inc<0) 
				while(_samp<=0)
					_samp += 1.0;		
			else _samp = 0;					//reset if incr changes from neg to pos
		}		
		out[i] = _samp;
		_samp += inc;						// increment sample value
	}
	x->samp = _samp;
	x->act = inc;
	
	return;
}


void myObj_perform64_noSig(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					 double **outs, long numouts, long sampleframes, long flags, void *userparam)
{	
	t_double *out = outs[0];	
	int vs = sampleframes;		
	int i;
	double _samp = x->samp;
	double inc = x->act;
	
	if (x->x_obj.z_disabled)
		return;
	
	for(i=0; i<vs; i++) 
	 {
		if(_samp >= 1.0) {
			inc = x->incr;
			if(inc>0) _samp -= 1.0;		
			else _samp = 1.0;				//reset if incr changes from pos to neg 
		}
		else if(_samp < 0.0 ) {
			inc = x->incr;
			if(inc<0) _samp += 1.0;
			else _samp = 0;					//reset if incr changes from neg to pos
		}		
		out[i] = _samp;
		_samp += inc;						// increment sample value
	}
	x->samp = _samp;
	x->act = inc;
	
	return;
}






void *myObj_new(double freq) 
{
	t_myObj *x = NULL;
	x = object_alloc(myObj_class);
	
	if(x) {
		dsp_setup((t_pxobject*)x, 1);			// one signal inlet
		floatin(x, 1);
		outlet_new((t_pxobject *)x, "signal"); 
		
		x->sr = sys_getsr();
		//post("sr: %d", x->sr);
		if(x->sr <= 0 ) x->sr = 44100;
		x->one_over_sr = 1./x->sr;
		myObj_freq(x, freq);
		x->samp = 0;
		x->act = x->incr;
		//x->freq = freq;
	}
	
	return x;
}


void myObj_assist(t_myObj *x, void *b, long m, long a, char *s) {
	if (m==1) {
		switch(a) {
			case 0: sprintf (s,"(float, signal) frequency, (bang) forces new frequency"); break;
			case 1: sprintf (s, "(float) phase"); break;
		}
	}
	else
		sprintf (s,"(signal) ramp output");
}
