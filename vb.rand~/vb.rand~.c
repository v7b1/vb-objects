#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"
#include "ext_common.h"

#define RAND_SCALE 2.0/RAND_MAX


void *myObj_class;

typedef struct {
	t_pxobject x_obj;
	char	interpol;
	double	target;
	double	frac;
	double	sr, r_sr;
	double	freq;
	double	incr;
	double	y0, y2, y3;
} t_myObj;

void myObj_set_interpol(t_myObj *x, long input);
void myObj_freq(t_myObj *x, double input);
void myObj_int(t_myObj *x, long input);
double getRand();

void myObj_dsp(t_myObj *x, t_signal **sp, short *count);
t_int *myObj_perform(t_int *w);
t_int *myObj_perform_sig(t_int *w);

void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, 
				 long maxvectorsize, long flags);
void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					 double **outs, long numouts, long sampleframes, long flags, void *userparam);
void myObj_perform64_sig(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					 double **outs, long numouts, long sampleframes, long flags, void *userparam);

double interpolate(double x, double y0, double y1, double y2, double y3);
void *myObj_new( t_symbol *s, long argc, t_atom *argv);
void myObj_assist(t_myObj *x, void *b, long m, long a, char *s);


int C74_EXPORT main(void) {
	t_class *c;
	
	c = class_new("vb.rand~", (method)myObj_new, (method)dsp_free, (short)sizeof(t_myObj), 0L, 
				  A_GIMME, 0L);
	class_addmethod(c, (method)myObj_dsp, "dsp", A_CANT, 0);
	class_addmethod(c, (method)myObj_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c, (method)myObj_freq, "float", A_FLOAT, 0);	
	class_addmethod(c, (method)myObj_int, "int", A_LONG, 0);
	class_addmethod(c, (method)myObj_assist, "assist", A_CANT,0);
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	myObj_class = c;
	
	
	// attributes
	CLASS_ATTR_CHAR(c,"interp", 0, t_myObj, interpol);
	CLASS_ATTR_SAVE(c, "interp", 0);
    CLASS_ATTR_STYLE_LABEL(c, "interp", 0, "onoff", "interpolation on/off");
	
	
	post("vb.rand~ by volker böhm");
	
	return 0;
}


void myObj_set_interpol(t_myObj *x, long input) {
	if(input!=0) x->interpol = 1;
	else x->interpol = 0;
}


void myObj_int(t_myObj *x, long input) {
	myObj_freq(x, (double)input);
}

void myObj_freq(t_myObj *x, double input) {
	x->freq = CLAMP(input, 0., x->sr*0.5);	//fabs(input);
	x->incr = x->freq / x->sr;
}

double getRand() {
	return ((double)rand() * RAND_SCALE) -1;
}


void myObj_dsp(t_myObj *x, t_signal **sp, short *count) {
	if(count[0])
		dsp_add(myObj_perform_sig, 4, x, sp[0]->s_vec, sp[1]->s_vec, sp[0]->s_n);
	else
		dsp_add(myObj_perform, 3, x, sp[1]->s_vec, sp[0]->s_n);
	
	x->sr = sp[0]->s_sr;
	if(x->sr<=0) x->sr = 44100.0;
	x->r_sr = 1.0 / x->sr;
}


void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, 
				 long maxvectorsize, long flags) {
	if(count[0])
		object_method(dsp64, gensym("dsp_add64"), x, myObj_perform64_sig, 0, NULL);
	else
		object_method(dsp64, gensym("dsp_add64"), x, myObj_perform64, 0, NULL);
	
	x->sr = samplerate;
	if(x->sr<=0) x->sr = 44100.0;
	x->r_sr = 1.0 / x->sr;
}


t_int *myObj_perform(t_int *w) {
	
	t_myObj *x = (t_myObj*)(w[1]);
	float *out = (float *)(w[2]);	
	int vs = (int)(w[3]);		
	double	frac, target, incr;
	double	y0, y2, y3;
	
	
	if (x->x_obj.z_disabled)
		goto out;
	if(x->freq==0)
		goto zero;
	
	frac = x->frac;
	incr = x->incr;
	target = x->target;
	y0 = x->y0;
	y2 = x->y2; 
	y3 = x->y3;

	if(x->interpol) {
		while(vs--) {
			if(frac>=1) {
				frac-=1;
				x->y0 = y0 = target;
				x->target = target = y2;
				x->y2 = y2 = y3;
				x->y3 = y3 = getRand();
			}
			
			frac += incr;
			*out++ = interpolate(frac, y0, target, y2, y3) * 0.815;	// keep values in range -1/1
		}
	}
	else {
		while(vs--) {
			if(frac>=1) {
				frac-=1;
				x->target = target = getRand();
			}
	
			frac += incr;
			*out++ = target;
		}
	}
	
	x->frac = frac;

out:
	return w+4;	
	
zero:
	while (vs--)  
		*out++ = 0.f;
	return w+4;	
}


// signal input version
t_int *myObj_perform_sig(t_int *w) {
	
	t_myObj *x = (t_myObj*)(w[1]);
	float *in = (float *)(w[2]);
	float *out = (float *)(w[3]);	
	int vs = (int)(w[4]);		
	double	frac, target, incr, r_sr;
	double	y0, y2, y3;
	
	
	if (x->x_obj.z_disabled)
		goto out;
	if(in[0]==0)		// check only first sample of input vector
		goto zero;
	
	frac = x->frac;
	r_sr = x->r_sr;
	target = x->target;
	y0 = x->y0;
	y2 = x->y2; 
	y3 = x->y3;
	
	if(x->interpol) {
		while(vs--) {
			incr = fabsf(*in++) * r_sr;
			if(frac>=1) {
				frac-=1;
				x->y0 = y0 = target;
				x->target = target = y2;
				x->y2 = y2 = y3;
				x->y3 = y3 = getRand();
			}
			
			frac += incr;
			
			*out++ = interpolate(frac, y0, target, y2, y3) * 0.815;	// keep values in range -1/1
		}
	}
	else {
		while(vs--) {
			incr = fabsf(*in++)* r_sr;
			if(frac>=1) {
				frac-=1;
				x->target = target = getRand();
			}
			
			frac += incr;
			
			*out++ = target;
		}
	}
	
	x->frac = frac;
	
out:
	return w+5;	
	
zero:
	while (vs--)  
		*out++ = 0.f;
	return w+5;	
}


// 64-bit version
void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					 double **outs, long numouts, long sampleframes, long flags, void *userparam) {
	
	t_double *out = outs[0];	
	int vs = sampleframes;		
	double	frac, target, incr;
	double	y0, y2, y3;
	
	
	if (x->x_obj.z_disabled)
		return;
	if(x->freq==0)
		goto zero;
	
	frac = x->frac;
	incr = x->incr;
	target = x->target;
	y0 = x->y0;
	y2 = x->y2; 
	y3 = x->y3;
	
	if(x->interpol) {
		while(vs--) {
			if(frac>=1) {
				frac-=1;
				x->y0 = y0 = target;
				x->target = target = y2;
				x->y2 = y2 = y3;
				x->y3 = y3 = getRand();
			}
			
			frac += incr;
			*out++ = interpolate(frac, y0, target, y2, y3) * 0.815;	// keep values in range -1/1
		}
	}
	else {
		while(vs--) {
			if(frac>=1) {
				frac-=1;
				x->target = target = getRand();
			}
			
			frac += incr;
			*out++ = target;
		}
	}
	
	x->frac = frac;
	return;
zero:
	while (vs--)  
		*out++ = 0.;

}


// 64 bit signal input version
void myObj_perform64_sig(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					   double **outs, long numouts, long sampleframes, long flags, void *userparam){
	
	t_double *in = ins[0];
	t_double *out = outs[0];	
	int vs = sampleframes;		
	double	frac, target, incr, r_sr;
	double	y0, y2, y3;
	
	
	if (x->x_obj.z_disabled)
		return;
	if(in[0]==0)		// check only first sample of input vector
		goto zero;
	
	frac = x->frac;
	r_sr = x->r_sr;
	target = x->target;
	y0 = x->y0;
	y2 = x->y2; 
	y3 = x->y3;
	
	if(x->interpol) {
		while(vs--) {
			incr = fabs(*in++) * r_sr;
			if(frac>=1) {
				frac-=1;
				x->y0 = y0 = target;
				x->target = target = y2;
				x->y2 = y2 = y3;
				x->y3 = y3 = getRand();
			}
			
			frac += incr;
			
			*out++ = interpolate(frac, y0, target, y2, y3) * 0.815;	// keep values in range -1/1
		}
	}
	else {
		while(vs--) {
			incr = fabs(*in++)* r_sr;
			if(frac>=1) {
				frac-=1;
				x->target = target = getRand();
			}
			
			frac += incr;
			
			*out++ = target;
		}
	}
	
	x->frac = frac;
	return;
zero:
	while (vs--)  
		*out++ = 0.;
}


// 4-point, 3rd-order Hermite 
inline double interpolate(double x, double y0, double y1, double y2, double y3) { 
	double c0 = y1;
	double c1 = 0.5f * (y2 - y0);
	double c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
	double c3 = 1.5f * (y1 - y2) + 0.5f * (y3 - y0);
	return ((c3 * x + c2) * x + c1) * x + c0;
}


//void *myObj_new(double freq) 
void *myObj_new(t_symbol *s, long argc, t_atom *argv)
{
	t_myObj *x = object_alloc(myObj_class);
	dsp_setup((t_pxobject*)x, 1);			
	outlet_new((t_object *)x, "signal"); 
	
	x->sr = sys_getsr();
	if(x->sr <= 0)
		x->sr = 44100.f;
	x->r_sr = 1.0 / x->sr;
	
	x->interpol = 1;		// interpolation on by default;
	x->frac = 0.;
	x->target = getRand();
	x->y0 = 0;
	x->y2 = getRand();
	x->y3 = getRand();

	
	t_atom *ap;
	ap = argv;		// only get first argument
	switch (atom_gettype(ap)) { 
		case A_LONG:
			myObj_freq(x, (float)	atom_getlong(ap)); break;
		case A_FLOAT: 
			myObj_freq(x, atom_getfloat(ap)); break;
		case A_SYM:
			myObj_freq(x, 0.f);	// if no argument, set freq to zero
	}
	
	
	//post("message selector is %s",s->s_name); post("there are %ld arguments",argc);
	/*
	for (i = 0, ap = argv; i < argc; i++, ap++) {
		switch (atom_gettype(ap)) { 
			case A_LONG:
				post("%ld: %ld",i+1,atom_getlong(ap)); break;
			case A_FLOAT: 
				post("%ld: %.2f",i+1,atom_getfloat(ap)); break;
			case A_SYM: 
				post("%ld: %s",i+1, atom_getsym(ap)->s_name); break;
			default: 
				post("%ld: unknown atom type (%ld)", i+1, atom_gettype(ap)); break;
		}
	}*/
	
	attr_args_process(x, argc, argv);			// process attributes
	
	return x;
}


void myObj_assist(t_myObj *x, void *b, long m, long a, char *s) {
	if (m==1) {
		switch(a) {
			case 0: sprintf (s,"(signal/float/int) frequency"); break;
		}
	}
	else {
		switch(a) {
			case 0: sprintf (s,"(signal) random values output"); break;
			case 1: sprintf(s, "(signal) s & h random values out"); break;
		}
		
	}
}
