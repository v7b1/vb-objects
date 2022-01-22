#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"
#include "ext_common.h"

/***********************
	an implementation of 
	Andrew Simper's Linear Trapezoidal Integrated State Variable Filter With Low Noise Optimisation
	http://www.cytomic.com/files/dsp/SvfLinearTrapOptimised.pdf
	vb, Feb. 2013
 
***********************/


void *myObj_class;

typedef struct {
	t_pxobject x_obj;
	double	r_sr;
	
	double	cf, k;		// cutoff, and reciprocal Q
	double	g1, g2, g3, g4;
	double	v0z, v1, v2;

} t_myObj;


void myObj_int(t_myObj *x, long input);
void myObj_float(t_myObj *x, double input);
void myObj_ft1(t_myObj *x, double input);
void myObj_ft2(t_myObj *x, double input);
void myObj_in1(t_myObj *x, long input);
void myObj_in2(t_myObj *x, long input);
void myObj_calcCoeffs(t_myObj *x);

// DSP methods
void myObj_dsp(t_myObj *x, t_signal **sp, short *count);
t_int *myObj_perform(t_int *w);

void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, 
				 long maxvectorsize, long flags);
void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					 double **outs, long numouts, long sampleframes, long flags, void *userparam);
//

void *myObj_new(double freq, double q);
void myObj_assist(t_myObj *x, void *b, long m, long a, char *s);


int C74_EXPORT main(void) {
	t_class *c;
	
	c = class_new("vb.simper.svf~", (method)myObj_new, (method)dsp_free, (short)sizeof(t_myObj), 0L, 
				  A_DEFFLOAT, A_DEFFLOAT, 0L);
	class_addmethod(c, (method)myObj_dsp, "dsp", A_CANT, 0);
	class_addmethod(c, (method)myObj_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c, (method)myObj_ft1, "ft1", A_FLOAT, 0);
	class_addmethod(c, (method)myObj_ft2, "ft2", A_FLOAT, 0);
	class_addmethod(c, (method)myObj_in1, "in1", A_LONG, 0);
	class_addmethod(c, (method)myObj_in2, "in2", A_LONG, 0);
	class_addmethod(c, (method)myObj_assist, "assist", A_CANT,0);
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	myObj_class = c;
	
	post("vb.simper.svf~ by vb, based on code by Andrew Simper\n");
	
	return 0;
}



void myObj_in1(t_myObj *x, long input) {
	// set cutoff freq
	myObj_ft1(x, input);
}
void myObj_in2(t_myObj *x, long input) {
	// set Q
	myObj_ft2(x, input);
}

void myObj_ft1(t_myObj *x, double input) {
	// set cutoff freq
	x->cf = CLAMP(input, 10, 18000); 
	myObj_calcCoeffs(x);
}

void myObj_ft2(t_myObj *x, double input) {
	// input: Q, k: recipr Q
	x->k = 1.0/CLAMP(input, 0.001, 1000);
	
	myObj_calcCoeffs(x);
}


void myObj_calcCoeffs(t_myObj *x) {
	double g = tan(M_PI*x->cf*x->r_sr);
	double k = x->k;
	double ginv = g / (1 + g * (g+k));
	
	x->g1 = ginv;
	x->g2 = 2 * (g+k) *ginv;
	x->g3 = g * ginv;
	x->g4 = 2 * ginv;
					   
}


void myObj_dsp(t_myObj *x, t_signal **sp, short *count) {

	dsp_add(myObj_perform, 7, x, sp[0]->s_vec, sp[1]->s_vec, 
			sp[2]->s_vec, sp[3]->s_vec, sp[4]->s_vec, sp[0]->s_n);
	
	if(sp[0]->s_sr<=0) x->r_sr = 1.0/44100.0;
	else x->r_sr = 1.0 / sp[0]->s_sr;
}


void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, 
				 long maxvectorsize, long flags) {
	object_method(dsp64, gensym("dsp_add64"), x, myObj_perform64, 0, NULL);
	
	if(samplerate<=0) x->r_sr = 1.0/44100.0;
	else x->r_sr = 1.0/samplerate;
	

}



t_int *myObj_perform(t_int *w) {
	
	t_myObj *x = (t_myObj*)(w[1]);
	float *in = (float *)(w[2]);
	float *out1 = (float *)(w[3]);	
	float *out2 = (float *)(w[4]);	
	float *out3 = (float *)(w[5]);	
	float *out4 = (float *)(w[6]);	
	int vs = (int)(w[7]);		
	
	
	if (x->x_obj.z_disabled)
		goto out;

	float low, high, band, notch;
	double v0, v3, v1z, v2z;
	double v1 = x->v1;
	double v2 = x->v2;
	double v0z = x->v0z;
	double k = x->k;
	double g1 = x->g1;
	double g2 = x->g2;
	double g3 = x->g3;
	double g4 = x->g4;
	

	while(vs--) {
		v0 = (*in++);
		v1z = v1;
		v2z = v2;
		v3 = v0 + v0z - 2 * v2z;
		
		v1 += g1*v3 - g2*v1z;
		v2 += g3*v3 + g4*v1z;
		v0z = v0;
		
		low = v2;
		band = v1;
		high = v0 - k*v1 - v2;
		notch = v0 - k*v1;
		
		*out1++ = low;
		*out2++ = high;
		*out3++ = band;
		*out4++ = notch;
	}
	
	x->v1 = v1;
	x->v2 = v2;
	x->v0z = v0z;
	
out:
	return w+8;	
	
}




// 64 bit signal input version
void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					   double **outs, long numouts, long sampleframes, long flags, void *userparam){
	
	t_double *in = ins[0];
	t_double *out1 = outs[0];
	t_double *out2 = outs[1];
	t_double *out3 = outs[2];
	t_double *out4 = outs[3];
	int vs = sampleframes;	
	
	double low, high, band, notch;
	double v0, v3, v1z, v2z;
	double v1 = x->v1;
	double v2 = x->v2;
	double v0z = x->v0z;
	double k = x->k;
	double g1 = x->g1;
	double g2 = x->g2;
	double g3 = x->g3;
	double g4 = x->g4;

	
	if (x->x_obj.z_disabled)
		return;

	while(vs--) {
		v0 = (*in++);
		v1z = v1;
		v2z = v2;
		v3 = v0 + v0z - 2 * v2z;
		
		v1 += g1*v3 - g2*v1z;
		v2 += g3*v3 + g4*v1z;
		v0z = v0;
		
		low = v2;
		band = v1;
		high = v0 - k*v1 - v2;
		notch = v0 - k*v1;
		
		*out1++ = low;
		*out2++ = high;
		*out3++ = band;
		*out4++ = notch;
	}
	
	x->v1 = v1;
	x->v2 = v2;
	x->v0z = v0z;

	return;

}





void *myObj_new(double freq, double q)
{
	t_myObj *x = object_alloc(myObj_class);
	
	if(x) {
		dsp_setup((t_pxobject*)x, 1);			// one signal inlet
		floatin(x, 2);
		floatin(x, 1);
		outlet_new((t_object *)x, "signal"); 
		outlet_new((t_object *)x, "signal"); 
		outlet_new((t_object *)x, "signal"); 
		outlet_new((t_object *)x, "signal"); 
		
		
		if(sys_getsr()<= 0)
			x->r_sr = 1.0/44100;
		else
			x->r_sr = 1.0/sys_getsr();
		
		
		x->v1 = x->v2 = x->v0z = 0;
		
		if (q>0) {
			x->k = 1/q;
		} else x->k = 1;
		
		if(freq>0)
			myObj_ft1(x, freq);
		else
			myObj_ft1(x, 440.0);
	}
	else {
		object_free(x);
		x = NULL;
	}
	
	return x;
}


void myObj_assist(t_myObj *x, void *b, long m, long a, char *s) {
	if (m==ASSIST_INLET) {
		switch(a) {
			case 0: sprintf (s,"(signal) audio input"); break;
			case 1: sprintf (s,"(float/int) cutoff freq"); break;
			case 2: sprintf (s,"(float/int) filter q"); break;
				
		}
	}
	else {
		switch(a) {
			case 0: sprintf (s,"(signal) lowpass signal out"); break;
			case 1: sprintf (s,"(signal) highpass signal out"); break;
			case 2: sprintf (s,"(signal) bandpass signal out"); break;
			case 3: sprintf (s,"(signal) notch signal out"); break;
		}
		
	}
}
