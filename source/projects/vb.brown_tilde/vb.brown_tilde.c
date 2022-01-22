#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"
#include "ext_common.h"

/************************************
 vb.brown~ © volker boehm, 2007
 a brownian-noise-generator (or random walk) with adjustable step size
 based on Moore's book p.443
 ***********************************/

/*
	update to max sdk 6.1, vb, 28.8.2013
 */
/*
	update to max sdk 6.1.4 - vb, feb.2015
 */

#define SCALERAND (1.0 / 0x7FFFFFFF)

typedef struct {
	t_pxobject x_obj;
	double		sr;
	double		rlast, step;
	t_uint32	val;
	short		proc, sig_connected;
} t_myObj;


static t_class *myObj_class;

void myObj_seed(t_myObj *x, long s);
void myObj_step(t_myObj *x, double s);
void myObj_proc(t_myObj *x, long p);

// DSP methods
void myObj_dsp(t_myObj *x, t_signal **sp, short *count);
t_int *myObj_perform(t_int *w);

void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, 
				 long maxvectorsize, long flags);
void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					 double **outs, long numouts, long sampleframes, long flags, void *userparam);
//

void *myObj_new( double arg );
void myObj_assist(t_myObj *x, void *b, long m, long a, char *s);


int C74_EXPORT main(void) {
	t_class *c;
	c = class_new("vb.brown~", (method)myObj_new, (method)dsp_free, (short)sizeof(t_myObj), 
				  0L, A_DEFFLOAT, 0L);
	class_addmethod(c, (method)myObj_dsp, "dsp", A_CANT, 0);
	class_addmethod(c, (method)myObj_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c, (method)myObj_step, "float", A_FLOAT, 0);	
	class_addmethod(c, (method)myObj_seed, "seed", A_LONG, 0);
	class_addmethod(c, (method)myObj_proc, "proc", A_LONG, 0);
	class_addmethod(c, (method)myObj_assist, "assist", A_CANT,0);
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	myObj_class = c;
	
	post("vb.brown~ by volker böhm");
	
	return 0;
}


void myObj_proc(t_myObj *x, long p) {
	x->proc = (p != 0);
}

void myObj_seed(t_myObj *x, long s) {
	x->val = s;
}

void myObj_step(t_myObj *x, double s) {
	x->step = CLAMP(s, 0., 1.);
}


// 32-bit dsp method
void myObj_dsp(t_myObj *x, t_signal **sp, short *count) {
	x->sig_connected = count[0];
	dsp_add(myObj_perform, 4, x, sp[0]->s_vec, sp[1]->s_vec, sp[0]->s_n);
	
}

//64-bit dsp method
void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, 
				 long maxvectorsize, long flags) {
	x->sig_connected = count[0];
	object_method(dsp64, gensym("dsp_add64"), x, myObj_perform64, 0, NULL);
}


t_int *myObj_perform(t_int *w) {
	
	t_myObj *x = (t_myObj*)(w[1]);
	float *in = (float *)(w[2]);
	float *out = (float *)(w[3]);	
	int vs = (int)(w[4]);		
	
	t_uint32 val = x->val;
	double	r, fran, step, input;
	
	if (x->x_obj.z_disabled)
		goto out;
	if(!x->proc)
		goto zero;

	r = x->rlast;
	
	if( x->sig_connected ) {
		while (vs--) {
			input = (*in++);
			val = val * 40359821 + 1;	// generate a random number (lin congru - should i bother?)
			step = CLAMP(input, 0, 1);
			fran = (SCALERAND * val -1) * step;
			r += fran;
			
			// check boundries
			while(r>1.) r -= 2*fran;
			while(r<-1) r -= 2*fran;
			
			*out++ = r;
		}
	}
	else {
		step = x->step;
		while (vs--) {
			val = val * 40359821 + 1;
			fran = (SCALERAND * val -1) * step;
			r += fran;
			
			// check boundries
			while(r>1.) r -= 2*fran;
			while(r<-1) r -= 2*fran;
			
			*out++ = r;
		}
	}
	
	x->val = val;
	x->rlast = r;
	
out:
	return w+5;	
	
zero:
	while (vs--)  
		*out++ = 0.f;
	return w+5;	
}



// 64 bit signal input version
void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					   double **outs, long numouts, long sampleframes, long flags, void *userparam){
	
	t_double *in = ins[0];
	t_double *out = outs[0];	
	int vs = sampleframes;		

	t_uint32 val = x->val;
	double	r, fran, step, input;
	
	if (x->x_obj.z_disabled)
		return;
	if(!x->proc)
		goto zero;

	r = x->rlast;
	
	if( x->sig_connected ) {
		while (vs--) {
			input = (*in++);
			val = val * 40359821 + 1;
			step = CLAMP(input, 0, 1);
			fran = (SCALERAND * val -1) * step;
			r += fran;
			
			// check boundries
			while(r>1.) r -= 2*fran;
			while(r<-1) r -= 2*fran;
			
			*out++ = r;
		}
	}
	else {
		step = x->step;
		while (vs--) {
			val = val * 40359821 + 1;
			fran = (SCALERAND * val -1) * step;
			r += fran;
			
			// check boundries
			while(r>1.) r -= 2*fran;
			while(r<-1) r -= 2*fran;
			
			*out++ = r;
		}
	}
	
	x->val = val;
	x->rlast = r;

	return;
	
zero:
	while (vs--)  
		*out++ = 0.;
}



void *myObj_new(double arg)
{
	t_myObj *x = object_alloc(myObj_class);
	
	if(x) {
		dsp_setup((t_pxobject*)x, 1);			// one signal inlet
		outlet_new((t_object *)x, "signal"); 

		if(arg != 0) myObj_step(x, arg);
		else x->step = 0.01;
		x->rlast = 0;
		x->proc = 1;
		
		//use ticks and object address to create a unique seed for every instance of vb.brown~
		t_uint32 ticks = systime_ticks();
		t_uint32 objAddr = (long)x;
		x->val = ticks+objAddr;
		//post("seed: %ld", x->val);
	}
	
	else {
		object_free(x);
		x = NULL;
	}
	
	return x;
}


void myObj_assist(t_myObj *x, void *b, long m, long a, char *s) {
	if (m == ASSIST_INLET) {
		switch(a) {
			case 0: sprintf (s,"(sig/float) step size (0.-->1.)"); break;
		}
	}
	else {
		switch(a) {
			case 0: sprintf (s,"(signal) brownian noise out"); break;
		}
	}
}
