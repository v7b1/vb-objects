#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"

#include <Accelerate/Accelerate.h>


// a chebyshev filter
// volker böhm, november 08

// based on Steven Smith's DSP-guide

// update to max6 SDK, august 2012
// update to max6.1 SDK, april 2013
// update to max6.1.4 SDK, feb. 2015


#define MAXPOLES 20
#define TWOTANHALF (2 * tan(0.5))



typedef struct {
	t_pxobject b_ob;
	double		a[MAXPOLES*3/2];
	double		b[MAXPOLES*3/2];
	int			poles, mode;			// # of poles, mode: lowpass (0), hipass (1)
	double		cfreq;			// cutoff freq in Hz
	double		cf;				// normalized cutoff frequency
	double		ripple;			// amount of passband ripple in % (0 - 29%)
	double		r_sr;				// reciprocal sr
	int			blocksize;			// signal vector size
	void			*stages_out;
	double		*infilt, *outfilt;
	double		coeffs[MAXPOLES*5/2];
	double		xms[MAXPOLES];
	double		yms[MAXPOLES];
	t_atom		stages[MAXPOLES*5/2];
	double		xm1, xm2, ym1, ym2;
	short		inputconnected;
} t_myObj;



void myObj_cf(t_myObj *x, double input);			// set center frequency
void myObj_ripple(t_myObj *x, double input);
void myObj_poles(t_myObj *x, int input);
void myObj_mode(t_myObj *x, int input);
void myObj_print_coeffs(t_myObj *x);
void myObj_clear(t_myObj *x);
void myObj_dsp(t_myObj *x, t_signal **sp, short *count);
void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, 
				 long maxvectorsize, long flags);
void calcCoeffs(t_myObj *x);
void calc(t_myObj *x, int j, double *returns);
t_int *myObj_perform(t_int *w);
void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					 double **outs, long numouts, long sampleframes, long flags, void *userparam);
int memalloc(t_myObj *x, int n);
void *myObj_new(t_symbol *s, short argc, t_atom *argv);
void myObj_assist(t_myObj *x, void *b, long m, long a, char *s);
void *myObj_class;


int C74_EXPORT main(void) {
	t_class *c;

	c = class_new("vb.cheby~", (method)myObj_new, (method)dsp_free, (short)sizeof(t_myObj), 0L, 
				  A_GIMME, 0);

	class_addmethod(c, (method)myObj_dsp, "dsp", A_CANT, 0);	
	class_addmethod(c, (method)myObj_dsp64, "dsp64", A_CANT, 0);	
	class_addmethod(c, (method)myObj_cf, "ft1", A_FLOAT, 0);
	class_addmethod(c, (method)myObj_ripple, "ripple", A_FLOAT, 0);
	class_addmethod(c, (method)myObj_poles, "poles", A_LONG, 0);
	class_addmethod(c, (method)myObj_mode, "mode", A_LONG, 0);
	class_addmethod(c, (method)myObj_clear, "clear", 0L);
	class_addmethod(c, (method)myObj_print_coeffs, "info", 0L);
	class_addmethod(c, (method)myObj_assist, "assist", A_CANT,0);
	
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	myObj_class = c;
	
	post("vb.cheby~ by volker böhm, v.1.0.6");
	
	return 0;
}


void myObj_cf(t_myObj *x, double input) {	
	//myObj_clear(x);
	if(input>=33) {
		x->cfreq = input;
		x->cf = input * x->r_sr;			// cf: normalized frequency
	}
	else {
		x->cfreq = 33;
		x->cf = 33 * x->r_sr;
	}
	calcCoeffs(x);
}

void myObj_ripple(t_myObj *x, double input) {
	x->ripple = CLAMP(input, 0, 30);
	calcCoeffs(x);
}

void myObj_poles(t_myObj *x, int input) {
	if(input>=2 && input<=20) {
		if(input%2==0) {		// #poles must be even
			x->poles = input;
		} else {
			x->poles = --input;
			object_post((t_object *)x, "number of poles must be even\n\tsetting poles to %ld", x->poles);
		}
		myObj_clear(x);
		calcCoeffs(x);
	} else
		object_error((t_object *)x, "# poles out of range!");
}

void myObj_mode(t_myObj *x, int input) 
{
	x->mode = CLAMP(input, 0, 1);
	myObj_clear(x);
	calcCoeffs(x);

}

void myObj_clear(t_myObj *x) 
{
	int i;
	for(i=0; i<MAXPOLES; i++) {
		x->xms[i] = x->yms[i] = 0;
	}
}


void myObj_print_coeffs(t_myObj *x) 
{
	int i, j;
	char mode[6];
	if(x->mode) strcpy(mode, "hpass");
	else strcpy(mode, "lpass");
	
	post("---------------------------------");
	object_post((t_object *)x, "#poles: %ld -- mode: %s -- ripple: %2.2f", x->poles, mode, x->ripple);

	for(i=0; i<x->poles/2; i++) {
		post("a[%d]: %e", 0, x->coeffs[i*5]);
		for(j=1; j<3; j++)
			post("a[%d]: %e b[%d]: %e", j, x->coeffs[i*5+j], j, -x->coeffs[i*5+j+2]);
		post("----");
	}
}

void calcCoeffs(t_myObj *x) 
{
	int i, j;
	double results[5];
	double sa, sb, rgain;
	int poles2 = x->poles/2;			// poles must be even

	for(j=0; j<poles2; j++) {
		calc(x, j, results);
		
		// normalize gain
		sa = 0;
		sb = 0;
		for(i=0; i<3; i++) {
			if(x->mode == 0) {
				sa += results[i];
			} else {
				sa += results[i]*pow(-1, i);
			}
		}
		if(x->mode == 0)
			sb = results[3]+results[4];
		else
			sb = -results[3]+results[4];
		
		rgain = (1-sb) / sa;
		
		double val;
		for(i=0; i<3; i++) {
			val = results[i]*rgain;
			//atom_setfloat(x->stages+(j*5+i), val);
			x->coeffs[j*5+i] = val;
		}
		for(i=0; i<2; i++) {
			val= -results[i+3];
			//atom_setfloat(x->stages+(j*5+3+i), val);
			x->coeffs[j*5+3+i] = val;
		}
		
	}
	atom_setdouble_array((5*poles2), x->stages, (MAXPOLES*5/2), x->coeffs);
	outlet_list(x->stages_out, 0L, 5*poles2, x->stages);

}


void calc(t_myObj *x, int j, double *returns) 
{
	double rp, ip, es, vx, kx, t, w, m, d, k, x0, x1, x2, y1, y2, tt, kk;
	rp = ip = es = vx = kx = t = w = m = d = k = x0 = x1 = x2 = y1 = y2 = 0;
	
	// calculate the pole location on the unit circle
	rp = -cos( PI/(x->poles*2) + j * PI/x->poles );
	ip = sin( PI/(x->poles*2) + j * PI/x->poles );
	
	// wrap from a circle to an ellipse
	if(x->ripple > 0) {
		es = sqrt( (100.0 / (100.0-x->ripple)) *  (100.0 / (100.0-x->ripple)) -1);
		vx = (1.0/x->poles) * log( (1.0/es) + sqrt( (1.0/(es*es))+1) );
		kx = (1.0/x->poles) * log( (1.0/es) + sqrt( (1.0/(es*es))-1) );
		kx = (exp(kx) + exp(-kx))*0.5;
		rp = rp * ( (exp(vx) - exp(-vx)) *0.5 ) / kx;
		ip = ip * ( (exp(vx) + exp(-vx)) *0.5 ) / kx;
	}
	
	// s-domain to z-domain conversion
	t = TWOTANHALF;	//2 * tan(0.5);
	tt = t*t;
	w = TWOPI * x->cf;
	m = rp*rp + ip*ip;
	d = 4 - 4*rp*t + m*tt;
	x0 = tt / d;
	x1 = 2 * x0;
	x2 = x0;
	y1 = (8 - 2*m*tt) / d;
	y2 = (-4 - 4*rp*t - m*tt) / d;
	
	
	// LP to LP, or LP to HP Transform
	if(x->mode ==0) 
		k = sin(0.5-w*0.5) / sin(0.5+w*0.5);
	else
		k = -cos(w*0.5 + 0.5) / cos(w*0.5-0.5);
	kk = k*k;
	d = 1+ y1*k - y2*kk;
	returns[0] = (x0 - x1*k + x2*kk) / d;
	returns[1] = (-2*x0*k + x1 + x1*kk - 2*x2*k) / d;
	returns[2] = (x0*kk - x1*k + x2) / d;
	returns[3] = (2*k + y1 + y1*kk - 2*y2*k) / d;
	returns[4] = ( -(kk) - y1*k + y2) / d;
	if(x->mode ==1) {
		returns[1] = -returns[1];
		returns[3] = -returns[3];
	}
}


void myObj_dsp(t_myObj *x, t_signal **sp, short *count) 
{	
	if(sp[0]->s_n != x->blocksize) {
		memalloc(x, sp[0]->s_n);
	}
	x->r_sr = 1.0/sp[0]->s_sr;
	myObj_cf(x, x->cfreq);
	
	x->inputconnected = count[0];
	dsp_add(myObj_perform, 4, x, sp[0]->s_vec, sp[1]->s_vec, sp[0]->s_n);
}


void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
	x->r_sr = 1.0/samplerate;
	myObj_cf(x, x->cfreq);
	if (maxvectorsize != x->blocksize) {
		memalloc(x, maxvectorsize);
	}

	x->inputconnected = count[0];
	object_method(dsp64, gensym("dsp_add64"), x, myObj_perform64, 0, NULL);
}



t_int *myObj_perform(t_int *w) {
	t_myObj *x = (t_myObj*)(w[1]);
	float	*in= (float *)(w[2]);
	float	*out= (float *)(w[3]);
	int		vs = w[4];
	int		k;
	double	*outfilt = x->outfilt;
	double	*infilt = x->infilt;
	double	*xms = x->xms;
	double	*yms = x->yms;
	int		poles2 = x->poles >> 1; 	//each biquad can calc 2 poles (and 2 zeros)
	
	
	if(!x->inputconnected || x->b_ob.z_disabled)
		return w+5;
	
	// copy input and use outfilt for that (this is so the recursion works)
	/*
	for(i=0; i<vs; i++) {
		outfilt[i+2] = in[i];
	}*/
	vDSP_vspdp(in, 1, outfilt+2, 1, vs);
	
	for(k=0; k<poles2; k++) {
		
		// restore last two input samps 
		infilt[0] = xms[k*2];	
		infilt[1] = xms[k*2+1];
		// copy from outfilt to infilt (recursion)
		memcpy(infilt+2, outfilt+2, vs*sizeof(double));
		
		// restore last two output samps
		outfilt[0] = yms[k*2];
		outfilt[1] = yms[k*2+1];
		
		// do the biquad!
		vDSP_deq22D(infilt, 1, x->coeffs+(k*5), outfilt, 1, vs);
		
		// save last two input & output samples for next vector
		xms[k*2] = infilt[vs];
		xms[k*2+1] = infilt[vs+1];
		yms[k*2] = outfilt[vs];
		yms[k*2+1] = outfilt[vs+1];
	}
	
	// TODO: check for denormals???
	/*
	for(i=0; i<vs; i++) {
		out[i] = outfilt[i+2];
	}*/
	vDSP_vdpsp(outfilt+2, 1, out, 1, vs);
	
	return w+5;		
}



void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					 double **outs, long numouts, long sampleframes, long flags, void *userparam)
{
	t_double *in= ins[0];
	t_double *out= outs[0];
	int vs = sampleframes;
	int		k;
	double	*outfilt = x->outfilt;
	double	*infilt = x->infilt;
	double	*xms = x->xms;
	double	*yms = x->yms;
	int		poles2 = x->poles >> 1; 	//each biquad can calc 2 poles (and 2 zeros)

	
	if(!x->inputconnected || x->b_ob.z_disabled)
		return;
	
	// copy input and use outfilt for that (this is so the recursion works)
	memcpy(outfilt+2, in, vs*sizeof(double));		// dest, source, len

	for(k=0; k<poles2; k++) {
		
		// restore last two input samps 
		infilt[0] = xms[k*2];	
		infilt[1] = xms[k*2+1];
		// copy from outfilt to infilt (recursion)
		memcpy(infilt+2, outfilt+2, vs*sizeof(double));
		
		// restore last two output samps
		outfilt[0] = yms[k*2];
		outfilt[1] = yms[k*2+1];
		
		// do the biquad!
		vDSP_deq22D(infilt, 1, x->coeffs+(k*5), outfilt, 1, vs);
		
		// save last two input & output samples for next vector
		xms[k*2] = infilt[vs];
		xms[k*2+1] = infilt[vs+1];
		yms[k*2] = outfilt[vs];
		yms[k*2+1] = outfilt[vs+1];
	}
	
	// TODO: check for denormals???
	memcpy(out, outfilt+2, sizeof(double)*vs);
	
	
	// old version
	/*
	for(i=0; i<vs; i++) {
		
		input = *in++;
		output = 0;
		
		for(k=0; k<poles2; k++) {
			k2 = k<<1; k3 = k*3;
			
			 //output = input * a[k3] + xx[k2]*a[k3+1] + xx[k2+1]*a[k3+2]
			 //+ yy[k2]*b[k3+1] + yy[k2+1]*b[k3+2];	// biquad
			 
			ffw = input * a[k3] + xx[k2]*a[k3+1] + xx[k2+1]*a[k3+2];		// zeros
			fb =  yy[k2]*b[k3+1] + yy[k2+1]*b[k3+2];					// poles
			FIX_DENORM_DOUBLE(fb);
			output = ffw + fb;
			xx[k2+1] = xx[k2];
			xx[k2] = input;
			
			yy[k2+1] = yy[k2];
			yy[k2] = output;
			input = output;
			
		}
		*out++ = output;
	}*/
	
}


int memalloc(t_myObj *x, int n) {
	if(x->infilt) x->infilt = (double*)sysmem_resizeptr(x->infilt, (n+2)*sizeof(double));
	else x->infilt = (double *)sysmem_newptrclear((n+2)*sizeof(double));
	if(!x->infilt) {
		object_error((t_object *)x, "memory allocation failed!");
		return 1;
	}
	
	if(x->outfilt) x->outfilt = (double *)sysmem_resizeptr(x->outfilt, (n+2)*sizeof(double));
	else x->outfilt = (double *)sysmem_newptrclear((n+2)*sizeof(double));
	if(!x->outfilt) {
		object_error((t_object *)x, "memory allocation failed!");
		return 1;
	}
	
	x->blocksize = n;
	
	return 0;
}


void *myObj_new(t_symbol *s, short argc, t_atom *argv) {
	int i;
	t_myObj *x = object_alloc(myObj_class);
	
	if(x) {
		x->stages_out = listout(x);
		
		dsp_setup((t_pxobject*)x, 1); 
		outlet_new((t_pxobject *)x, "signal"); 

		floatin(x, 1);		// cf input
		
		
		// initialize paramters
		x->xm1 = x->xm2 = x->ym1 = x->ym2 = 0;
		for(i=0; i<MAXPOLES; i++) {
			x->a[i] = 0;
			x->b[i] = 0;
			x->xms[i] = 0;
			x->yms[i] = 0;
		}
		
		x->mode = 0;			// 0: lowpass, 1: highpass
		x->ripple = 0.5;
		x->poles = 4;
		x->r_sr = 1.0/sys_getsr();
		//x->blocksize = sys_getblksize();
		memalloc(x, sys_getblksize());
		
		x->cfreq = 4410.;
		
		// parse arguments
		if(argc==1) {
			switch(argv->a_type) {							// test type of argument
				case A_LONG:	
					x->cfreq =  argv->a_w.w_long;
					break;
				case A_FLOAT:
					x->cfreq =  argv->a_w.w_float;
					break;
			}
		} 
		else if(argc==2) {
			switch(argv->a_type) {							// test type of argument
				case A_LONG:	
					x->cfreq =  argv->a_w.w_long;
					break;
				case A_FLOAT:
					x->cfreq =  argv->a_w.w_float;
					break;
			}
			argv++;
			switch(argv->a_type) {							// test type of argument
				case A_LONG:	
					myObj_poles(x, argv->a_w.w_long);
					break;
				case A_FLOAT:
					myObj_poles(x, (int)argv->a_w.w_float);
					break;
			}
		} 
		else if(argc==3) {
			switch(argv->a_type) {							// test type of argument
				case A_LONG:	
					x->cfreq =  argv->a_w.w_long;
					break;
				case A_FLOAT:
					x->cfreq =  argv->a_w.w_float;
					break;
			}
			argv++;
			switch(argv->a_type) {							// test type of argument	
				case A_LONG:	
					myObj_poles(x, argv->a_w.w_long);
					break;
				case A_FLOAT:
					myObj_poles(x, (int)argv->a_w.w_float);
					break;
			}
			argv++;
			switch(argv->a_type) {							// test type of argument
				case A_LONG:	
					myObj_mode(x, argv->a_w.w_long);
					break;
				case A_FLOAT:
					myObj_mode(x, (float)argv->a_w.w_float);
					break;
			}
		} 
		else if(argc==4) {
			switch(argv->a_type) {							// test type of argument
				case A_LONG:	
					x->cfreq =  argv->a_w.w_long;
					break;
				case A_FLOAT:
					x->cfreq =  argv->a_w.w_float;
					break;
			}
			argv++;
			switch(argv->a_type) {							// test type of argument
				case A_LONG:	
					myObj_poles(x, argv->a_w.w_long);
					break;
				case A_FLOAT:
					myObj_poles(x, (int)argv->a_w.w_float);
					break;
			}
			argv++;
			switch(argv->a_type) {							// test type of argument
				case A_LONG:	
					myObj_mode(x, argv->a_w.w_long);
					break;
				case A_FLOAT:
					myObj_mode(x, (float)argv->a_w.w_float);
					break;
			}
			argv++;
			switch(argv->a_type) {							// test type of argument
				case A_LONG:	
					myObj_ripple(x, argv->a_w.w_long);
					break;
				case A_FLOAT:
					myObj_ripple(x, argv->a_w.w_float);
					break;
			}
		} 
		
		myObj_cf(x, x->cfreq);
	}
	
	else {
		object_free(x);
		x = NULL;
	}
	
	
	return x;
}

void myObj_assist(t_myObj *x, void *b, long m, long a, char *s) {
	if (m==1) {
		switch(a) {
			case 0:
				sprintf (s,"(signal) audio in");
				break;
			case 1:
				sprintf (s,"(float) filter cutoff freq");
				break;
		}
	}
	else
		switch(a) {
			case 0:
				sprintf (s,"(signal) audio output");
				break;
			case 1:
				sprintf (s,"(list) cascaded biquad coefficients");
				break;
		}
}