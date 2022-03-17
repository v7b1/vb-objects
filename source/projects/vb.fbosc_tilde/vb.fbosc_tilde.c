#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"


//	sinewave-oscillator + feedback
//	volker böhm, september 06

//	update to max6, september 2012



#define WSIZE 4096				// size of wave-table
#define WSIZE2 4098
#define DELTIME 4096



typedef struct {
	t_pxobject b_ob;
	double	current, last, hp_x, hp_y;
	double	r_sr, fb, incr, a0, b1, hp_a0, hp_b1;
	long	writer, del;			// write-pointer of delay line
	double	*wtable;			// wavetable
	double	*delbuffer;			// array to write and read sample data from
} t_myObj;

void myObj_float(t_myObj *x, double f);
void myObj_freq(t_myObj *x, double input);		// set freq of oscillator
void myObj_fb(t_myObj *x, double input);		// set feedback
void myObj_cf(t_myObj *x, double input);			// set cutoff freq of lowpass filter
void myObj_del(t_myObj *x, long input);			// set delay time

void myObj_dsp(t_myObj *x, t_signal **sp, short *count);
t_int *myObj_perform(t_int *w);

void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, 
				 long maxvectorsize, long flags);
void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					 double **outs, long numouts, long sampleframes, long flags, void *userparam);

void myObj_assist(t_myObj *x, void *b, long m, long a, char *s);
void *myObj_new(t_symbol *s, short argc, t_atom *argv);
void *myObj_class;
void myObj_free(t_myObj *x);

void ext_main(void *r)  {
	t_class *c;
	c = class_new("vb.fbosc~", (method)myObj_new, (method)myObj_free, (short)sizeof(t_myObj), 0L, 0L);

	class_addmethod(c, (method)myObj_dsp, "dsp", A_CANT, 0);
	class_addmethod(c, (method)myObj_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c, (method)myObj_float, "float", A_FLOAT, 0);
	class_addmethod(c, (method)myObj_del, "int", A_LONG, 0);
	class_addmethod(c, (method)myObj_assist,"assist",A_CANT,0);
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	myObj_class = c;
	
	post("vb.fbosc~ by volker böhm, version 1.0.1");

}

void myObj_float(t_myObj *x, double f) {
	
	switch (proxy_getinlet((t_object *)x)) {
		case 0: 
			myObj_freq(x, f); break;
		case 1:
			myObj_fb(x, f); break;
		case 2:
			myObj_cf(x, f); break;
	}
}

void myObj_freq(t_myObj *x, double input) {
	x->incr = input * x->r_sr;				// calculate pointer increment from input freq
	if(input<0.0) {
		x->incr *= -1;
	}		
}

void myObj_fb(t_myObj *x, double input) {
	if(input>=-1 && input<1)
		x->fb = input;
}

void myObj_cf(t_myObj *x, double input) {	

	CLIP_ASSIGN(input, 10, 0.43/x->r_sr);
	x->b1 = exp(-2*PI*input * x->r_sr);
	x->a0 = 1 - x->b1;
}

void myObj_del(t_myObj *x, long input) {
	if(input>=1 && input<=DELTIME) {
		x->del = input;
	}		
	else {
		object_post((t_object*)x, "deltime out of range.");
	}
}

void myObj_dsp(t_myObj *x, t_signal **sp, short *count) {		
	dsp_add(myObj_perform, 7, x, sp[0]->s_vec, sp[1]->s_vec, sp[2]->s_vec, sp[3]->s_vec, 
			sp[4]->s_vec, sp[0]->s_n);

	if(sp[0]->s_sr>0)
		x->r_sr = 1.0 / sp[0]->s_sr;	
	else x->r_sr = 1.0 / 44100.0;
}


void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, 
				 long maxvectorsize, long flags) {

	object_method(dsp64, gensym("dsp_add64"), x, myObj_perform64, 0, NULL);
	
	if(samplerate>0)
		x->r_sr = 1.0 / samplerate;	
	else x->r_sr = 1.0 / 44100.0;
}


t_int *myObj_perform(t_int *w) {
	t_myObj *x = (t_myObj*)(w[1]);
	float *in = (float *)(w[2]);
	float *out= (float *)(w[6]);
	int vs = w[7];
	
	int i, trunc;
	double curr, filt, last;		// curr: current read-pointer value
	double weight, index, hp, samp;
	long reader, writer, delay;
	double tempIncr, tempA0, tempB1, tempFb, pointer, tempX, tempY;
	double hp_a0, hp_b1;
	double hpx, hpy;
	double *sintab = x->wtable;
	double *delbuf = x->delbuffer;
	
	
	// copy variables from object struct
	tempIncr = x->incr;			
	tempA0 = x->a0;
	tempB1 = x->b1;
	tempFb = x->fb;
	delay = x->del;
	pointer = x->current;
	writer = x->writer;
	last = x->last;
	hpx = x->hp_x;
	hpy = x->hp_y;
	hp_a0 = x->hp_a0;
	hp_b1 = x->hp_b1;
	
	reader =  writer - delay;		// read pointer is writepointer - delay time
	if(reader < 0)					// shouldn't be smaller than zero...
		reader += DELTIME;
	
	for(i=0; i<vs; i++) {
		
		pointer += tempIncr;		// increment internal pointer
		if(pointer > 1. )		// this is only good for positive freq
			pointer  -= 1.;
		
		
		curr = pointer + delbuf[reader] * tempFb  + in[i];	// read values from delay line
		
		// keep everything in range
		if(curr>=1) 
			curr -= (int)curr;
		else if(curr<0)
			curr = curr - (int)curr + 1;
		
		index = curr*WSIZE;
		trunc = (int)index;
		tempX = sintab[trunc];
		tempY = sintab[(trunc+1)];		// now uses guard point (don't need modulo)
		
		weight = index - trunc;			
		samp = (tempY-tempX)*weight + tempX;			// read wave table (with interpolation)
		
		filt = tempA0*samp + tempB1*last;		// onepole lowpass filter in feedback path
		delbuf[writer] = filt;			// write current filtered out-value to delay
		last = filt;					// store last filtered output sample
		
		hp = (samp - hpx)*hp_a0 + hpy*hp_b1;		// apply onepole highpass to output values
		hpx = samp;
		hpy = hp;
		
		out[i] = hp;			// highpass filtered output
		
		writer++;			// increment write pointer of delay line
		writer -= (writer >> 12) << 12;		// limit to 0 -- 4095 (DELTIME-1)
		reader++;
		reader -= (reader >> 12) << 12;	
		
	}	
	// update variables in struct
	x->current = pointer;	
	x->writer = writer;
	x->last = last;
	x->hp_x = hpx;
	x->hp_y = hpy;
	
	return w+8;		
}



// 64-bit version
void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					 double **outs, long numouts, long sampleframes, long flags, void *userparam) 
{
	t_double *in = ins[0];
	t_double *out= outs[0];
	int vs = sampleframes;
	
	int i, trunc;
	double curr, filt, last;		// curr: current read-pointer value
	double weight, index, hp, samp;
	long reader, writer, delay;
	double tempIncr, tempA0, tempB1, tempFb, pointer, tempX, tempY;
	double hp_a0, hp_b1;
	double hpx, hpy;
	double *sintab = x->wtable;
	double *delbuf = x->delbuffer;
	
	
	// copy variables from object struct
	tempIncr = x->incr;			
	tempA0 = x->a0;
	tempB1 = x->b1;
	tempFb = x->fb;
	delay = x->del;
	pointer = x->current;
	writer = x->writer;
	last = x->last;
	hpx = x->hp_x;
	hpy = x->hp_y;
	hp_a0 = x->hp_a0;
	hp_b1 = x->hp_b1;
	
	
	reader =  writer - delay;		// read pointer is writepointer - delay time
	if(reader < 0)					// shouldn't be smaller than zero...
		reader += DELTIME;
	
	for(i=0; i<vs; i++) {
		
		pointer += tempIncr;		// increment internal pointer
		if(pointer > 1. )		// this is only good for positive freq
			pointer  -= 1.;
		
		
		curr = pointer + delbuf[reader] * tempFb  + in[i];	// read values from delay line
		
		// keep everything in range
		if(curr>=1) 
			curr -= (int)curr;
		else if(curr<0)
			curr = curr - (int)curr + 1;
		
		index = curr*WSIZE;
		trunc = (int)index;
		tempX = sintab[trunc];
		tempY = sintab[(trunc+1)];		// now uses guard point (don't need modulo)
		
		weight = index - trunc;			
		samp = (tempY-tempX)*weight + tempX;			// read wave table (with interpolation)
		
		filt = tempA0*samp + tempB1*last;		// onepole lowpass filter in feedback path
		delbuf[writer] = filt;			// write current filtered out-value to delay
		last = filt;					// store last filtered output sample
		
		hp = (samp - hpx)*hp_a0 + hpy*hp_b1;		// apply onepole highpass to output values
		hpx = samp;
		hpy = hp;
		
		out[i] = hp;			// highpass filtered output
		
		writer++;			// increment write pointer of delay line
		writer -= (writer >> 12) << 12;		// limit to 0 -- 4095 (DELTIME-1)
		reader++;
		reader -= (reader >> 12) << 12;	
	}	
	// update variables in struct
	x->current = pointer;	
	x->writer = writer;
	x->last = last;
	x->hp_x = hpx;
	x->hp_y = hpy;
	
		
}




void *myObj_new(t_symbol *s, short argc, t_atom *argv) {
	t_myObj *x = object_alloc(myObj_class);
	int i;
	dsp_setup((t_pxobject*)x, 4); 
	outlet_new((t_pxobject *)x, "signal"); 

	// initialize variables
	x->current = 0;
	x->incr = 0.;
	x->last = 0.;
	x->fb = 0.;
	x->a0 = 0.5;
	x->b1 = 0.5;
	x->writer = 0;
	x->del = 1;
	if(sys_getsr()>0)
		x->r_sr = 1.0 / sys_getsr();	
	else x->r_sr = 1.0 / 44100.0;	
	
	
	// high pass filter coeffs
	x->hp_y = x->hp_x = 0;
	x->hp_b1 = exp(-2*PI*25*x->r_sr);		// cf = 25 Hz
	x->hp_a0 = (1 + x->hp_b1) * 0.5;

	
	x->delbuffer = (double *)sysmem_newptrclear(sizeof(double)*DELTIME);
	x->wtable = (double *)sysmem_newptrclear(sizeof(double)*WSIZE2);
	for(i=0; i<WSIZE2; i++) {				// fill wavetable (+ guardpoint)
		x->wtable[i] = cos(2*PI*i/WSIZE);
	}
	
	
	return x;
}


void myObj_assist(t_myObj *x, void *b, long m, long a, char *s) {
	if (m==ASSIST_INLET) {
		switch (a) {
			case 0: sprintf(s,"(sig): phase input, (float) frequency in Hz"); break;
			case 1: sprintf(s,"(float) feedback"); break;
			case 2: sprintf(s,"(float) cf of lowpass in Hz"); break;
			case 3: sprintf(s,"(int) delay time in samples"); break;
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
}
