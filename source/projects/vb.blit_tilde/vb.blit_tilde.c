#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"
#include <Accelerate/Accelerate.h>	// speed things up, using vector optimization

/* 
 BLIT DSF - band limited impulse train (discrete summation formula)
 Volker Böhm, oktober 2007

 based on Dodge&Jerse: Computer Music
	
 */

/* update to SDK 6.1.1, mai 2013 */
/* update to SDK 6.1.4, januar 2015 */


typedef struct {
	t_pxobject x_obj;
	double	phase;		/* phase accumulator */
	double	curcps;		/* current frequency, updated once per cycle */
	int		twoN;		/* # partials * 2 */
	double	sr, r_sr;
	int		not_zero;	/* flag to check whether freq input is zero */
	int		calcN;
	int		vs;			
	double	*alpha, *beta, *ramp;
} t_myObj;


static t_class *myObj_class;

void myObj_freq(t_myObj *x, double input);
void myObj_set_N(t_myObj *x, long aa);
void myObj_dsp(t_myObj *x, t_signal **sp, short *count);

t_int *myObj_perform(t_int *w);
t_int *myObj_perform_sig(t_int *w);
void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, 
				 long maxvectorsize, long flags);
void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					 double **outs, long numouts, long sampleframes, long flags, void *userparam);
void myObj_perform64_sig(t_myObj *x, t_object *dsp64, double **ins, long numins, 
						 double **outs, long numouts, long sampleframes, long flags, void *userparam);

void myObj_free(t_myObj *x);
void *myObj_new(double freq, long a);
void myObj_assist(t_myObj *x, void *b, long m, long a, char *s);


void ext_main(void *r)  {
	t_class *c;
	
	c = class_new("vb.blit~", (method)myObj_new, (method)myObj_free, (short)sizeof(t_myObj), 
				  0L, A_DEFFLOAT, A_DEFLONG, 0L);
	class_addmethod(c, (method)myObj_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c, (method)myObj_dsp, "dsp", A_CANT, 0);
	class_addmethod(c, (method)myObj_freq, "float", A_FLOAT, 0);	
	class_addmethod(c, (method)myObj_set_N, "N", A_LONG, 0);
	class_addmethod(c, (method)myObj_assist, "assist", A_CANT,0);
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	myObj_class = c;
	
	post("vb.blit~ by volker böhm, version 1.0.5 - http://vboehm.net");
	
}



void myObj_freq(t_myObj *x, double input) {
	input = fabs(input);
	if(input!=0.0) {
		x->curcps = input * x->r_sr;		// calculate increment
		
		// calc N only if x->N is zero or negativ
		if(x->calcN) {
			x->twoN = 2*(int)(x->sr*0.5/input);			// # of partials up to nyquist
		}
		//post("number of partials: %d %f", x->N, x->sr);
		x->not_zero = 1;
	}
	else
		x->not_zero = 0;
}


void myObj_set_N(t_myObj *x, long aa) {
	
	if(aa > 0) {			// if user sets N to a nonzero value, don't calc N from freq input
		x->twoN = 2*aa;
		x->calcN = 0;
	} else {
		x->twoN = (int)(1/x->curcps);
		x->calcN = 1;
	}
}


#pragma mark 32-bit DSP methods ------------
void myObj_dsp(t_myObj *x, t_signal **sp, short *count) {			
	if(count[0]) {
		dsp_add(myObj_perform_sig, 4, x, sp[0]->s_vec, sp[1]->s_vec, sp[0]->s_n);
	} else
		dsp_add(myObj_perform, 3, x, sp[1]->s_vec, sp[0]->s_n);
	
	x->sr = sp[0]->s_sr;
	if(x->sr<=0) x->sr = 44100.0;
	x->r_sr = 1./x->sr;
	
	// if vector size changed, realloc memory
	if(x->vs != sp[0]->s_n) {
		x->vs = sp[0]->s_n;
		if(x->alpha) x->alpha = (double *)sysmem_resizeptr(x->alpha, x->vs * sizeof(double));
		if(x->beta) x->beta = (double *)sysmem_resizeptr(x->beta, x->vs * sizeof(double));
		if(x->ramp) x->ramp= (double *)sysmem_resizeptr(x->ramp, x->vs * sizeof(double));
	}
	
	x->phase = 0;
}


t_int *myObj_perform(t_int *w) {
	t_myObj *x = (t_myObj*)(w[1]);
	float *out = (float *)(w[2]);	
	int n = (int)(w[3]);		
	double N2p1, r_n2, neg_r_n2, phase, inc;
	int num, offset, end;
	double *alpha = x->alpha;
	double *beta = x->beta;
	double *ramp = x->ramp;
	double thresh = FLT_MIN;
	
	if (x->x_obj.z_disabled)
		goto out;
	
	phase = x->phase;			// phase runs from 0 to π
	
	N2p1 = (x->twoN + 1);		// 2N + 1
	r_n2 = 1.0/(x->twoN);		// reciprocal of 2N
	neg_r_n2 = -r_n2;			// negative of 1/2N
	inc = x->curcps*PI;			// double to float and scale by π
	
	if(x->not_zero) {
		/* create ramp from 0 to π at current frequency */
		num = (PI-phase) / inc + 1;	// calc integer number of steps at current inc
		offset = 0;
		end = num;
		
		while(end < n) {
			vDSP_vrampD( &phase, &inc, ramp+offset, 1, num);		// fill ramp segement with num steps
			
			offset += num;		// incr offset
			phase = *(ramp+offset-1) + inc-PI;		// get last phase and wrap it around 0-π
			num = (PI-phase) / inc + 1;
			end = offset + num;	// incr end
		}
		// and the rest
		vDSP_vrampD( &phase, &inc, ramp+offset, 1, n-offset);
		 
		// increment phase once more and wrap it //
		phase = ramp[n-1] + inc;	
		if(phase > PI)
			phase -= PI;
		
		/*
		 // alternative: eleganter aber langsamer... //
		vDSP_vramp( &phase, &inc2, ramp, 1, n);		// make ramp
		vDSP_vfrac(ramp, 1, ramp, 1, n);				// wrap it to 0. --> 1.
		
		phase = ramp[n-1] + inc2;	
		if(phase > 1.f)
			phase -= 1.f;
		vDSP_vsmul(ramp, 1, &pi, ramp, 1, n);		// multiply by π
		*/
		
		// ramp geht immer von 0 - π
		vDSP_vsmulD( ramp, 1, &N2p1, alpha, 1, n); 
		vvsin( alpha, alpha, &n);
		vvsin(beta, ramp, &n);
		vDSP_vthrD(beta, 1, &thresh, beta, 1, n);			// make sure beta is never zero!
		vDSP_vdivD( beta, 1, alpha, 1, alpha, 1, n);		// divide alpha by beta --> store in alpha
		vDSP_vsmsaD( alpha, 1, &r_n2, &neg_r_n2, alpha, 1, n);		// * r_n2 - r_n2
		
		vDSP_vdpsp(alpha, 1, out, 1, n);		// convert 64 to 32 bit
		x->phase = phase;
	}
	else {
		while (n--)  
			*out++ = 0.f;
	}
out:
	return w+4;	
}



t_int *myObj_perform_sig(t_int *w) {
	t_myObj *x = (t_myObj*)(w[1]);
	float *in = (float *)(w[2]);
	float *out = (float *)(w[3]);	
	int n = (int)(w[4]);		
	
	double phase;
	int i, twoN, calcN;
	double N2p1, r_n2, neg_r_n2, pi, freq;
	double *alpha = x->alpha;
	double *beta = x->beta;
	double *ramp = x->ramp;
	double thresh = FLT_MIN;
	
	if (x->x_obj.z_disabled)
		goto out;
	
	phase = x->phase;
	calcN = x->calcN;
	pi = PI;
	
	freq = fabsf(in[0]);
	if(freq==0)
		goto zero;			// as a test, only take first value of input vector
	
	if(calcN) twoN = 2*(int)(x->sr*0.5/freq);	
	else twoN = x->twoN;
	N2p1 = (twoN + 1);
	r_n2 = 1.0/twoN;
	neg_r_n2 = -r_n2;

	vDSP_vspdp(in, 1, alpha, 1, n);			// convert from 32 to 64 bit
		// input vector is now in alpha
	vDSP_vabsD(alpha, 1, alpha, 1, n);
	vDSP_vsmulD(alpha, 1, &x->r_sr, alpha, 1, n);		// calc increment (curcps)
	for(i=0; i<n; i++) {					// build ramp ( do i really have to do this myself?)
		ramp[i] = phase;
		phase +=alpha[i];
	}
	if(phase>1.0)
		phase -= 1.0;
	vDSP_vfracD(ramp, 1, ramp, 1, n);				// wrap it to 0. --> 1.
	vDSP_vsmulD(ramp, 1, &pi, ramp, 1, n);			// multiply by π
	
	// ramp geht immer von 0 - π
	vDSP_vsmulD( ramp, 1, &N2p1, alpha, 1, n); 
	vvsin( alpha, alpha, &n);
	vvsin(beta, ramp, &n);
	vDSP_vthrD(beta, 1, &thresh, beta, 1, n);			// make sure beta is never zero!
	vDSP_vdivD( beta, 1, alpha, 1, alpha, 1, n);			// divide alpha by beta --> store in alpha
	vDSP_vsmsaD( alpha, 1, &r_n2, &neg_r_n2, alpha, 1, n);		// * r_n2 - r_n2
	
	vDSP_vdpsp(alpha, 1, out, 1, n);		// convert 64 to 32 bit
	x->phase = phase;

out:
	return w+5;	
	
zero:
	while (n--)
		*out++ = 0.f;
	return w+5;	
}



#pragma mark 64-bit DSP methods ------------
void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, 
				 long maxvectorsize, long flags)
{
	if(count[0]) {
		object_method(dsp64, gensym("dsp_add64"), x, myObj_perform64_sig, 0, NULL);
	} else
		object_method(dsp64, gensym("dsp_add64"), x, myObj_perform64, 0, NULL);
	
	x->sr = samplerate;
	if(x->sr<=0) x->sr = 44100.0;
	x->r_sr = 1./x->sr;
	
	// if vector size changed, realloc memory
	if(x->vs != maxvectorsize) {
		x->vs = maxvectorsize;
		if(x->alpha) x->alpha = (double *)sysmem_resizeptr(x->alpha, x->vs * sizeof(double));
		if(x->beta) x->beta = (double *)sysmem_resizeptr(x->beta, x->vs * sizeof(double));
		if(x->ramp) x->ramp= (double *)sysmem_resizeptr(x->ramp, x->vs * sizeof(double));
	}
	
	x->phase = 0;
}


void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					 double **outs, long numouts, long sampleframes, long flags, void *userparam) 
{
	t_double *out = outs[0];	
	int n = sampleframes;		
	double N2p1, r_n2, neg_r_n2, phase, inc;
	int num, offset, end;
	double *alpha = x->alpha;
	double *beta = x->beta;
	double *ramp = x->ramp;
	double thresh = FLT_MIN;
	
	if (x->x_obj.z_disabled)
		return;
	
	phase = x->phase;			// phase runs from 0 to π
	
	N2p1 = (x->twoN + 1);		// 2N + 1
	r_n2 = 1.0/(x->twoN);		// reciprocal of 2N
	neg_r_n2 = -r_n2;			// negative of 1/2N
	inc = x->curcps*PI;			// double to float and scale by π
	//inc2 = x->curcps;
	
	if(x->not_zero) {
		/* create ramp from 0 to π at current frequency */
		num = (PI-phase) / inc + 1;	// calc integer number of steps at current inc
		offset = 0;
		end = num;
		
		while(end < n) {
			vDSP_vrampD( &phase, &inc, ramp+offset, 1, num);		// fill ramp segement with num steps
			
			offset += num;		// incr offset
			phase = *(ramp+offset-1) + inc-PI;		// get last phase and wrap it around 0-π
			num = (PI-phase) / inc + 1;
			end = offset + num;	// incr end
		}
		// and the rest
		vDSP_vrampD( &phase, &inc, ramp+offset, 1, n-offset);
		
		// increment phase once more and wrap it //
		phase = ramp[n-1] + inc;	
		if(phase > PI)
			phase -= PI;
		
		// ramp geht immer von 0 - π
		vDSP_vsmulD( ramp, 1, &N2p1, alpha, 1, n); 
		vvsin( alpha, alpha, &n);
		vvsin(beta, ramp, &n);
		vDSP_vthrD(beta, 1, &thresh, beta, 1, n);			// make sure beta is never zero!
		vDSP_vdivD( beta, 1, alpha, 1, alpha, 1, n);		// divide alpha by beta --> store in alpha
		vDSP_vsmsaD( alpha, 1, &r_n2, &neg_r_n2, out, 1, n);		// * r_n2 - r_n2
		
		x->phase = phase;
	}
	else {
		while (n--)  
			*out++ = 0.;
	}
	return;
}



void myObj_perform64_sig(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					 double **outs, long numouts, long sampleframes, long flags, void *userparam)
{
	t_double *in = ins[0];
	t_double *out = outs[0];
	int n = sampleframes;		
	
	double phase;
	int i, twoN, calcN;
	double N2p1, r_n2, neg_r_n2, pi, freq;
	double *alpha = x->alpha;
	double *beta = x->beta;
	double *ramp = x->ramp;
	double thresh = FLT_MIN;
	
	if (x->x_obj.z_disabled)
		return;
	
	phase = x->phase;
	calcN = x->calcN;
	pi = PI;
	
	freq = fabs(in[0]);
	if(freq==0)
		goto zero;			// as a test, only take first value of input vector
	
	if(calcN) twoN = 2*(int)(x->sr*0.5/freq);	
	else twoN = x->twoN;
	N2p1 = (twoN + 1);
	r_n2 = 1.0/twoN;
	neg_r_n2 = -r_n2;
	

	vDSP_vabsD(in, 1, in, 1, n);
	vDSP_vsmulD(in, 1, &x->r_sr, in, 1, n);		// calc increment (curcps)
	for(i=0; i<n; i++) {					// build ramp ( do i really have to do this myself?)
		ramp[i] = phase;
		phase +=in[i];
	}
	if(phase>1.0)
		phase -= 1.0;
	vDSP_vfracD(ramp, 1, ramp, 1, n);				// wrap it to 0. --> 1.
	vDSP_vsmulD(ramp, 1, &pi, ramp, 1, n);			// multiply by π
	
	// ramp geht immer von 0 - π
	vDSP_vsmulD( ramp, 1, &N2p1, alpha, 1, n); 
	vvsin( alpha, alpha, &n);
	vvsin(beta, ramp, &n);
	vDSP_vthrD(beta, 1, &thresh, beta, 1, n);			// make sure beta is never zero!
	vDSP_vdivD( beta, 1, alpha, 1, alpha, 1, n);			// divide alpha by beta --> store in alpha
	vDSP_vsmsaD( alpha, 1, &r_n2, &neg_r_n2, out, 1, n);		// * r_n2 - r_n2

	x->phase = phase;
	return;
	
zero:
	while (n--)  
		*out++ = 0.;
	
}



void *myObj_new(double freq, long a) 
{
	t_myObj *x = object_alloc(myObj_class);
	dsp_setup((t_pxobject*)x, 1); 

	outlet_new((t_object *)x, "signal"); 
	
	x->sr = sys_getsr();
	if(x->sr <= 0)
		x->sr = 44100.;

	x->r_sr = 1.0/x->sr;
	x->vs = sys_getblksize();
	
	x->alpha = (double *)sysmem_newptr(sizeof(double)*x->vs);
	x->beta = (double *)sysmem_newptr(sizeof(double)*x->vs);
	x->ramp = (double *)sysmem_newptr(sizeof(double)*x->vs);
	
	x->phase = 0.0;
	
	myObj_freq(x, freq);
	myObj_set_N(x, a);
	
	
	return x;
}


void myObj_assist(t_myObj *x, void *b, long m, long a, char *s) {
	if (m==1) {
		switch(a) {
			case 0: sprintf (s,"(signal, float) frequency"); break;
				//case 1: sprintf (s, "(float) phase"); break;
		}
	}
	else
		sprintf (s,"(signal) impulse train output");
}

void myObj_free(t_myObj *x) {
	dsp_free((t_pxobject *)x);
	if(x->alpha)
		sysmem_freeptr(x->alpha);
	if(x->beta)
		sysmem_freeptr(x->beta);
	if(x->ramp)
		sysmem_freeptr(x->ramp);
	
}
