#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"
#include "ext_common.h"

/*
	a versatile ramp generator
	author: volker böhm
 
*/

// update to max sdk 6.1, vb -- 29.8.2013
// update to max sdk 6.1.4, vb -- 12.07. 2015


typedef struct {
	t_pxobject x_obj;
	long		count;
	int		loop;
	int		lup;			// only start looping if ramp is running
	float		last_in;
	double	period;
	double	reci;

} t_myObj;


static t_class *myObj_class;
void myObj_int(t_myObj *x, long input);
void myObj_period(t_myObj *x, double input);
void myObj_stop(t_myObj *x);

// DSP methods
void myObj_dsp(t_myObj *x, t_signal **sp, short *count);
t_int *myObj_perform(t_int *w);

void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, 
				 long maxvectorsize, long flags);
void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					 double **outs, long numouts, long sampleframes, long flags, void *userparam);

void *myObj_new( t_symbol *s, long argc, t_atom *argv );
void myObj_assist(t_myObj *x, void *b, long m, long a, char *s);


int C74_EXPORT main(void) {
	t_class *c;
	
	c = class_new("vb.ramp~", (method)myObj_new, (method)dsp_free, (short)sizeof(t_myObj), 0L, 
				  A_GIMME, 0L);
	class_addmethod(c, (method)myObj_dsp, "dsp", A_CANT, 0);
	class_addmethod(c, (method)myObj_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c, (method)myObj_period, "float", A_FLOAT, 0);	
	class_addmethod(c, (method)myObj_int, "int", A_LONG, 0);
	class_addmethod(c, (method)myObj_stop, "stop", 0);
	class_addmethod(c, (method)myObj_assist, "assist", A_CANT,0);
    
	CLASS_ATTR_CHAR(c,"loop", 0, t_myObj, loop);
	CLASS_ATTR_SAVE(c, "loop", 0);
	CLASS_ATTR_FILTER_CLIP(c,"loop",0,1);
	CLASS_ATTR_STYLE_LABEL(c, "loop", 0, "onoff", "loop on/off");
    
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	myObj_class = c;
	
	
	post("vb.ramp~ by volker böhm");
	
	return 0;
}


void myObj_int(t_myObj *x, long input)
{
    myObj_period(x, (double)input);
}


void myObj_period(t_myObj *x, double input)
{
	if(input>0) {
		x->period = input;
		x->reci = 1.0/x->period;
	}
	else {
		x->period = 0;
		x->reci = 0;
	}
}



void myObj_stop(t_myObj *x) {
	x->count = x->period;
}


// 32-bit dsp method
void myObj_dsp(t_myObj *x, t_signal **sp, short *count) {
	
	dsp_add(myObj_perform, 6, x, sp[0]->s_vec, sp[1]->s_vec, sp[2]->s_vec, sp[3]->s_vec, sp[0]->s_n);
}


//64-bit dsp method
void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, 
				 long maxvectorsize, long flags) {
	object_method(dsp64, gensym("dsp_add64"), x, myObj_perform64, 0, NULL);
}



t_int *myObj_perform(t_int *w) {
	
	t_myObj *x = (t_myObj*)(w[1]);
	float *in = (float *)(w[2]);
	float *in2 = (float *)(w[3]);
	float *out = (float *)(w[4]);	
	float *out2 = (float *)(w[5]);
	long vs = (long)w[6];
	
	if (x->x_obj.z_disabled)
		goto out;
	
	int		i;
	float		last_in = x->last_in;
	float		input;
	double	period = x->period;
	double	reci = x->reci;
	long		count = x->count;


	for(i=0; i<vs; i++) {
		input = in[i];
		if(input!=0 && last_in == 0) {
			count = 0;
			
			if(in2[i]!=0) {
				period = in2[i];
				reci =  (1.0/period);
				x->period = period;
				x->reci = reci;
			}
			else
				reci = period = x->period = x->reci = 0;
		}
		
		out2[i] = count * reci;
		out[i] = count;
		
		if(count<period) {
			count++;
			x->lup = x->loop;	// only start looping if ramp is running
		}
		else if(x->lup)
			count=0;
		
		last_in = input;
	}
	
	x->count = count;
	x->last_in = last_in;
	
out:
	return w+7;	
	
}




// 64 bit signal input version
void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					   double **outs, long numouts, long sampleframes, long flags, void *userparam){
	
	t_double *in = ins[0];
	t_double *in2 = ins[1];
	t_double *out = outs[0];	
	t_double *out2 = outs[1];	
	int vs = sampleframes;		

	if (x->x_obj.z_disabled)
		return;

	int i;
	t_double	last_in = x->last_in;
	t_double	input;
	t_double	period = x->period;
	t_double	reci = x->reci;
	long	count = x->count;
	
	
	for(i=0; i<vs; i++) {
		input = in[i];
		if(input!=0 && last_in == 0) {
			count = 0;
			
			if(in2[i]>=0) {
				period = in2[i];
				reci =  (1.0/period);
				x->period = period;
				x->reci = reci;
			}
			else {
				reci = period = x->period = x->reci = 0;
			}
		}
		
		out2[i] = count * reci;
		out[i] = count;
		
		if(count<period) {
			count++;
			x->lup = x->loop;	// only start looping if ramp is running
		}
		else if(x->lup)
			count=0;
		
		last_in = input;
	}
	
	x->count = count;
	x->last_in = last_in;

}




void *myObj_new( t_symbol *s, long argc, t_atom *argv )
{
    double period = 0;
	t_myObj *x = object_alloc(myObj_class);
	if(x) {
		dsp_setup((t_pxobject*)x, 2);			// two signal inlets
		outlet_new((t_object *)x, "signal"); 
		outlet_new((t_object *)x, "signal"); 
		
		x->count = 0;
		if(argc >= 1) {
			if(atom_gettype(argv)==A_LONG)
				period = atom_getlong(argv);
			else if (atom_gettype(argv)==A_FLOAT)
				period = atom_getfloat(argv);
		}
		x->period = period;
		if(period > 0) x->reci = 1/period;
		else x->reci = 0;
		x->loop = 0;
        
		attr_args_process(x, argc, argv);			// process attributes
        
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
			case 0: sprintf (s,"(signal) transition from zero to non-zero triggers ramp"); break;
			case 1: sprintf (s, "(signal/float/int) ramp time in samples"); break;
		}
	}
	else {
		switch(a) {
			case 0: sprintf (s,"(signal) index count output"); break;
			case 1: sprintf (s,"(signal) ramp out (0.-1.)"); break;
		}
		
	}
}
