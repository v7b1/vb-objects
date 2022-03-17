#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"
//#include "ext_common.h"
//#include <math.h>

#include <Accelerate/Accelerate.h>


//  allpole filter (only recursive stages) for LPC modelling
// vb, sept. 08

// 26/05/2013 -- update to max SDK 6.1
/* 
 * 15/02/2015 -- update to max SDK 6.1.4
 * check out denormal and NANs 
 */

#define MAXLIST 256
#define NOISEINJECT 32			// noise injection period

typedef struct {
	t_pxobject x_obj;
	int order, offset;
	double sr;
	double coeffs[MAXLIST];
    double coeffsB[MAXLIST];
    double coeffsC[MAXLIST];
	double z[MAXLIST];
} t_myObj;


void myObj_setCoeffs(t_myObj *x, t_symbol *mess, short argc, t_atom *argv);
void myObj_getCoeffs(t_myObj *x);
void myObj_reset(t_myObj *x);
void *myObj_new(t_symbol *s, short argc, t_atom *argv);
void myObj_assist(t_myObj *x, void *b, long m, long a, char *s);
static t_class *myObj_class;


// dsp methods
void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, 
				 long maxvectorsize, long flags);
void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					 double **outs, long numouts, long sampleframes, long flags, void *userparam);


void ext_main(void *r) {
	t_class *c;
	
	c = class_new("vb.allpole~", (method)myObj_new, (method)dsp_free, (short)sizeof(t_myObj), 
				  0L, A_GIMME, 0L);
	class_addmethod(c, (method)myObj_dsp64, "dsp64", A_CANT, 0);
	//class_addmethod(c, (method)myObj_float, "float", A_FLOAT, 0);	
	//class_addmethod(c, (method)myObj_int, "int", A_LONG, 0);
	
	class_addmethod(c, (method)myObj_setCoeffs, "list", A_GIMME, 0);
	class_addmethod(c, (method)myObj_getCoeffs, "getCoeffs", 0);
	class_addmethod(c, (method)myObj_reset, "reset", 0);
	class_addmethod(c, (method)myObj_assist, "assist", A_CANT,0);
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	myObj_class = c;
	
    object_post(NULL, "vb.allpole~ (+interpolation) by volker böhm\n");

}



void myObj_setCoeffs(t_myObj *x, t_symbol *mess, short argc, t_atom *argv) {	
	int i;
	if(argc>MAXLIST) argc = MAXLIST;
	x->order = argc;		// update filter order
	
	// TODO: coeffs in umgekehrter reihenfolge ins array schreiben!
	for(i=0; i<argc; i++) {
		switch(atom_gettype(&argv[i])) {
			case A_FLOAT:
				x->coeffsB[argc-i-1] = atom_getfloat(&argv[i]);
				break;
			default:
				object_error((t_object *)x, "vb.allpole~: coeffs list must be float");
				break;
		}
	}
}

void myObj_getCoeffs(t_myObj *x) {
	int i;
	object_post((t_object*)x, "order: %d", x->order);
	for(i=0; i<x->order; i++)
		post("coeff[%d]: %f", i, x->coeffsB[i]);
}


void myObj_reset(t_myObj *x) {
	int i;
	for(i=0; i<MAXLIST; i++) {
		x->coeffsB[i] = 0.0;
		x->z[i] = 0.0;
	}
	x->order = 10;
}



#pragma mark 64-bit DSP method ------

void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, 
				 long maxvectorsize, long flags) {
	object_method(dsp64, gensym("dsp_add64"), x, myObj_perform64, 0, NULL);
	
	x->sr = samplerate;
	if(x->sr<=0) x->sr = 44100.0;
	
}


void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					 double **outs, long numouts, long sampleframes, long flags, void *userparam){
	
	t_double *in = ins[0];
	t_double *out = outs[0];	
	int vs = sampleframes;		
	int i, len;
	double *coeffs = x->coeffs;
    double *coeffsB = x->coeffsB;
    double *coeffsC = x->coeffsC;
	double *z = x->z;
	int offset = x->offset;
	int order = x->order;
	double output = 0;
	double sum = 0;
	
	if (x->x_obj.z_disabled)
		return;
	
	// kick in some noise to keep denormals away
	i=0;
	while(i<vs) {
		in[i] += DBL_EPSILON;
		i += NOISEINJECT;
	}
    
    /*
     for (n = 0; n < N; ++n)
     D[n] = A[n] + C[0] * (B[n] - A[n]);
     */
    
    double frac = 0.0;
    double inc = 1.0 / (vs-1);
    /*
    for(i=0; i<vs; i++) {
        vDSP_vintbD(coeffs, 1, coeffsB, 1, &frac, coeffsC, 1, order);
        frac += inc;
    }
    
    memcpy(coeffs, coeffsB, order * sizeof(double));
     */
	
	for(i=0; i<vs; i++) {
		
		if(offset >= order) {
			offset = 0;
		}
		len = order - offset;

		/*
		output = in[i];
		//post("z[%d]: %f", offset, z[offset]);
		k = 0;
		for(j=offset; j<order; j++, k++)
			output += z[j] * coeffs[k];	
		post("outputA1: %f", output);
		for(j=0; j<offset; j++, k++)					// kann ich das nicht einfach umdrehn? und dann vDSP?
			output += z[j] * coeffs[k];
		post("outputA2: %f", output);
		z[offset] = output;
		for(p=0; p<order; p++)
			post("z[%d] = %f", p, z[p]);
		*/
        
        vDSP_vintbD(coeffs, 1, coeffsB, 1, &frac, coeffsC, 1, order);
        frac += inc;

		output = in[i];
		vDSP_dotprD(z+offset, 1, coeffsC, 1, &sum, len);
		output += sum;
		sum=0;		// must reinitialize to zero again!
		vDSP_dotprD(z, 1, coeffsC + len, 1, &sum, offset);
		output += sum;
		z[offset] = output;
		
		// check for nans
		if (IS_NAN_DOUBLE(output)) {
			//post("--> NAN!");
			object_error((t_object *)x, "filter explosion! -resetting... please watch your coeffs");
			myObj_reset(x);
			output = 0;
		}
		
		out[i] = output;
		offset++;
	}
    
    memcpy(coeffs, coeffsB, order * sizeof(double));

	x->offset = offset;

	
	return;
	
}




void *myObj_new(t_symbol *s, short argc, t_atom *argv) {
	int i;
	t_myObj *x;
	x = object_alloc(myObj_class);
	if(x) {
		dsp_setup((t_pxobject*)x, 1); 
		outlet_new((t_pxobject *)x, "signal"); 
		
		// initialize variables
		x->offset = 0;
		x->order = 10;
		x->sr = sys_getsr();		
		if(x->sr <= 0) x->sr = 44100;
		
		for(i=0; i<MAXLIST; i++) {
			x->coeffs[i] = 0.0;
			x->z[i] = 0.0;
			//x->z2[i] = 0.0;
		}
	}
	
	return x;
}

void myObj_assist(t_myObj *x, void *b, long m, long a, char *s) {
	if (m==ASSIST_INLET) {
		switch(a) {
			case 0:
				sprintf (s,"(signal) audio in, (list) list of filter coeffs");
				break;
			default:
				post("nada niente");
				break;
		}
	}
	else
		sprintf (s,"(signal) filtered audio output");
}
