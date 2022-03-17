#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"


/*
	like thresh~ but with settable gate time
*/




typedef struct {
	t_pxobject x_obj;
	t_double	sr;
	t_double	lothresh, hithresh;
	t_uint8		state;
	t_uint32	gtime, count;

} t_myObj;


static t_class *myObj_class;

//void myObj_float(t_myObj *x, double input);
void myObj_lothresh(t_myObj *x, double input);
void myObj_hithresh(t_myObj *x, double input);
void myObj_gatetime(t_myObj *x, double input);
void myObj_info(t_myObj *x);

// DSP methods
void myObj_dsp(t_myObj *x, t_signal **sp, short *count);
t_int *myObj_perform(t_int *w);

void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, 
				 long maxvectorsize, long flags);
void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					 double **outs, long numouts, long sampleframes, long flags, void *userparam);
//

//void *myObj_new( t_symbol *s, long argc, t_atom *argv);
void *myObj_new(double lothresh, double hithresh, double gtime);
void myObj_assist(t_myObj *x, void *b, long m, long a, char *s);


void ext_main(void *r) {
	t_class *c;
	
	c = class_new("vb.thresh~", (method)myObj_new, (method)dsp_free, (short)sizeof(t_myObj), 
				  0L, A_DEFFLOAT, A_DEFFLOAT, A_DEFFLOAT, 0L);
	class_addmethod(c, (method)myObj_dsp,       "dsp", A_CANT, 0);
	class_addmethod(c, (method)myObj_dsp64,     "dsp64", A_CANT, 0);
	class_addmethod(c, (method)myObj_lothresh,  "ft1", A_FLOAT, 0);
	class_addmethod(c, (method)myObj_hithresh,  "ft2", A_FLOAT, 0);
	class_addmethod(c, (method)myObj_gatetime,  "gatetime", A_FLOAT, 0);
    class_addmethod(c, (method)myObj_info,      "info", 0);
	class_addmethod(c, (method)myObj_assist,    "assist", A_CANT,0);
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	myObj_class = c;
	
	
	post("vb.thresh~ by volker böhm");
	
}



void myObj_lothresh(t_myObj *x, double input) {
	x->lothresh = input;
	
	if( x->lothresh > x->hithresh) 
		x->hithresh = x->lothresh;

	//object_post((t_object *)x, "lo: %f\thi: %f", x->lothresh, x->hithresh);
}

void myObj_hithresh(t_myObj *x, double input) {
	x->hithresh = input;
	if(x->hithresh < x->lothresh) 
		x->hithresh = x->lothresh;
	
	//object_post((t_object *)x, "lo: %f\thi: %f", x->lothresh, x->hithresh);
}


void myObj_gatetime(t_myObj *x, double input) {
	if(input >= 0.) x->gtime = input * x->sr * 0.001;
	else x->gtime = 0;
}


void myObj_info(t_myObj *x) {
    object_post((t_object *)x, "lowthresh: %f -- hithresh: %f -- gtime: %f",
                x->lothresh, x->hithresh, ((double)x->gtime)/(x->sr*0.001));
}


void myObj_dsp(t_myObj *x, t_signal **sp, short *count) 
{
	dsp_add(myObj_perform, 4, x, sp[0]->s_vec, sp[1]->s_vec, sp[0]->s_n);
	
	x->sr = sp[0]->s_sr;
	if(x->sr<=0) x->sr = 44100.0;
}


void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, 
				 long maxvectorsize, long flags) 
{
	object_method(dsp64, gensym("dsp_add64"), x, myObj_perform64, 0, NULL);
	
	x->sr = samplerate;
	if(x->sr<=0) x->sr = 44100.0;
}



t_int *myObj_perform(t_int *w) {
	
	t_myObj *x = (t_myObj*)(w[1]);
	float *in = (float *)(w[2]);
	float *out = (float *)(w[3]);	
	int vs = (int)(w[4]);		
	
	t_uint8		state = x->state;
	t_uint32	gtime = x->gtime;
	t_uint32	count = x->count;
	t_double	lothresh = x->lothresh;
	t_double	hithresh = x->hithresh;
    float       input;
	
	if (x->x_obj.z_disabled)
		goto out;

	
	while (vs--)
    {
        input = *in++;
        
		if(count > gtime) {
			if(state) {	// we're hi look for low samples
				if( input < lothresh ) {
					state = 0;
					count = 0;
				}
			}
			else {
				if ( input >= hithresh ) {
					state = 1;
					count = 0;
				}
			}
		}
		count++;
		*out++ = state;
	}
	
	x->count = count;
	x->state = state;
	
out:
	return w+5;	
	
}




// 64 bit signal input version
void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					   double **outs, long numouts, long sampleframes, long flags, void *userparam){
	
	t_double *in = ins[0];
	t_double *out = outs[0];	
	int vs = sampleframes;		
	
	t_uint8		state = x->state;
	t_uint32	gtime = x->gtime;
	t_uint32	count = x->count;
	t_double	lothresh = x->lothresh;
	t_double	hithresh = x->hithresh;
    t_double    input;
	
	if (x->x_obj.z_disabled)
		return;

	while (vs--)
    {
        input = *in++;
        
		if(count > gtime) {
			if(state) {	// we're hi look for low samples
				if( input < lothresh ) {
					state = 0;
					count = 0;
				}
			}
			else {
				if ( input >= hithresh ) {
					state = 1;
					count = 0;
				}
			}
		}
		count++;
		*out++ = state;
	}
	
	x->count = count;
	x->state = state;

	return;
	
}





void *myObj_new(double lothresh, double hithresh, double gtime)
{
	t_myObj *x = object_alloc(myObj_class);
	dsp_setup((t_pxobject*)x, 1);			// one signal inlet
	outlet_new((t_object *)x, "signal"); 
	
	floatin(x, 2);
	floatin(x, 1);
	
	x->sr = sys_getsr();
	if(x->sr <= 0)
		x->sr = 44100.;
	
	if(lothresh != 0) myObj_lothresh(x, lothresh);
	else x->lothresh = 0.;
	if(hithresh != 0) myObj_hithresh(x, hithresh);
	else x->hithresh = 0.5;
	
	if(gtime <= 0) gtime = 100;
	myObj_gatetime(x, gtime);
	
	x->count = 0;
	x->state = 0;

	return x;
}


void myObj_assist(t_myObj *x, void *b, long m, long a, char *s) {
	if (m==ASSIST_INLET) {
		switch(a) {
			case 0: sprintf (s,"(signal) input"); break;
			case 1: sprintf (s,"(float/int) low threshold"); break;
			case 2: sprintf (s,"(float/int) high threshold"); break;
		}
	}
	else {
		switch(a) {
			case 0: sprintf (s,"(signal) output"); break;
		}
		
	}
}
