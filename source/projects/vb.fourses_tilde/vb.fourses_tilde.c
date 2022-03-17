#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"
#include "ext_common.h"

/*
	a chaotic oscillator network
	based on descriptions of the 'fourses system' by ciat-lonbarde 
	www.ciat-lonbarde.net
 
	07.april 2013, volker böhm
*/

#define NUMFOURSES 4

void *myObj_class;

typedef struct {
	// this is a horse... basically a ramp generator
	double		val;
	double		inc;
	double		dec;
	double		adder;
	double		incy, incym1;		// used for smoothing
	double		decy, decym1;		// used for smoothing
} t_horse;

typedef struct {
	t_pxobject	x_obj;
	double		r_sr;
	t_horse		fourses[NUMFOURSES+2];	// four horses make a fourse...
	double		smoother;
} t_myObj;


// absolute limits
void myObj_hilim(t_myObj *x, double input);
void myObj_lolim(t_myObj *x, double input);

// up and down freqs for all oscillators
void myObj_upfreq(t_myObj *x, double freq1, double freq2, double freq3, double freq4);
void myObj_downfreq(t_myObj *x, double freq1, double freq2, double freq3, double freq4);

void myObj_smooth(t_myObj *x, double input);
void myObj_info(t_myObj *x);

// DSP methods
void myObj_dsp(t_myObj *x, t_signal **sp, short *count);
t_int *myObj_perform(t_int *w);
void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, 
				 long maxvectorsize, long flags);
void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					 double **outs, long numouts, long sampleframes, long flags, void *userparam);
//

void *myObj_new( t_symbol *s, long argc, t_atom *argv);
void myObj_assist(t_myObj *x, void *b, long m, long a, char *s);


void ext_main(void *r)  {
	t_class *c;
	
	c = class_new("vb.fourses~", (method)myObj_new, (method)dsp_free, (short)sizeof(t_myObj), 
				  0L, A_GIMME, 0L);
	class_addmethod(c, (method)myObj_dsp, "dsp", A_CANT, 0);
	class_addmethod(c, (method)myObj_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c, (method)myObj_smooth, "smooth", A_FLOAT, 0);	
	class_addmethod(c, (method)myObj_hilim, "hilim", A_FLOAT, 0);
	class_addmethod(c, (method)myObj_lolim, "lolim", A_FLOAT, 0);
	class_addmethod(c, (method)myObj_upfreq, "upfreq", A_FLOAT, A_FLOAT, A_FLOAT, A_FLOAT, 0);
	class_addmethod(c, (method)myObj_downfreq, "downfreq", A_FLOAT, A_FLOAT, A_FLOAT, A_FLOAT, 0);
	 
	class_addmethod(c, (method)myObj_info, "info", 0);
	class_addmethod(c, (method)myObj_assist, "assist", A_CANT,0);
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	myObj_class = c;
	
	
	post("vb.fourses~ by volker böhm\n");
	
}


void myObj_smooth(t_myObj *x, double input) {
	input = CLAMP(input, 0., 1.);
	x->smoother = 0.01 - pow(input,0.2)*0.01;
}

void myObj_hilim(t_myObj *x, double input) {
	x->fourses[0].val = input;		// store global high limit in fourses[0]
}
void myObj_lolim(t_myObj *x, double input) {	
	x->fourses[5].val = input;		// store global low limit in fourses[5]
}

void myObj_upfreq(t_myObj *x, double freq1, double freq2, double freq3, double freq4) {
	x->fourses[1].inc = fabs(freq1)*4*x->r_sr;
	x->fourses[2].inc = fabs(freq2)*4*x->r_sr;
	x->fourses[3].inc = fabs(freq3)*4*x->r_sr;
	x->fourses[4].inc = fabs(freq4)*4*x->r_sr;
}

void myObj_downfreq(t_myObj *x, double freq1, double freq2, double freq3, double freq4) {
	x->fourses[1].dec = fabs(freq1)*-4*x->r_sr;
	x->fourses[2].dec = fabs(freq2)*-4*x->r_sr;
	x->fourses[3].dec = fabs(freq3)*-4*x->r_sr;
	x->fourses[4].dec = fabs(freq4)*-4*x->r_sr;
}


#pragma mark 64bit dsp-loop ------------------

void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, 
				 long maxvectorsize, long flags) {
	object_method(dsp64, gensym("dsp_add64"), x, myObj_perform64, 0, NULL);
	
	if(samplerate<=0) x->r_sr = 1.0/44100.0;
	else x->r_sr = 1.0/samplerate;
	
	
}

void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					   double **outs, long numouts, long sampleframes, long flags, void *userparam){
	
	t_double **output = outs;
	int vs = sampleframes;	
	t_horse *fourses = x->fourses;
	double val, c, hilim, lolim;
	int i, n;
	
	if (x->x_obj.z_disabled)
		return;

	c = x->smoother;
	hilim = fourses[0].val;
	lolim = fourses[5].val;
	
	for(i=0; i<vs; i++) 
	 {
		for(n=1; n<=NUMFOURSES; n++) {
			// smoother
			fourses[n].incy = fourses[n].inc*c + fourses[n].incym1*(1-c);
			fourses[n].incym1 = fourses[n].incy;
			
			fourses[n].decy = fourses[n].dec*c + fourses[n].decym1*(1-c);
			fourses[n].decym1 = fourses[n].decy;
			
			val = fourses[n].val;
			val += fourses[n].adder;

			if(val <= fourses[n+1].val || val <= lolim ) {
				fourses[n].adder = fourses[n].incy;
			}
			else if( val >= fourses[n-1].val || val >= hilim ) {
				fourses[n].adder = fourses[n].decy;
			}
		
			output[n-1][i] = val;
			
			fourses[n].val = val;
		}
	 }
	
	return;
	
}



#pragma mark 32bit dsp-loop ------------------

void myObj_dsp(t_myObj *x, t_signal **sp, short *count) 
{	
	dsp_add(myObj_perform, 6, x, sp[0]->s_vec, sp[1]->s_vec, sp[2]->s_vec, sp[3]->s_vec, sp[0]->s_n);
	
	if(sp[0]->s_sr<=0) 
		x->r_sr = 1.0/44100.0;
	else x->r_sr = 1.0/sp[0]->s_sr;
	
	
}


t_int *myObj_perform(t_int *w) 
{	
	t_myObj *x = (t_myObj*)(w[1]);
	float *out1 = (float *)(w[2]);
	float *out2 = (float *)(w[3]);	
	float *out3 = (float *)(w[4]);
	float *out4 = (float *)(w[5]);	
	int vs = (int)(w[6]);		
	
	if (x->x_obj.z_disabled)
		goto out;
	
	t_horse *fourses = x->fourses;
	double val, c, hilim, lolim;
	int i, n;
	
	c = x->smoother;
	hilim = fourses[0].val;
	lolim = fourses[5].val;
	
	for(i=0; i<vs; i++) 
	 {
		for(n=1; n<=NUMFOURSES; n++) {
			// smoother
			fourses[n].incy = fourses[n].inc*c + fourses[n].incym1*(1-c);
			fourses[n].incym1 = fourses[n].incy;
			
			fourses[n].decy = fourses[n].dec*c + fourses[n].decym1*(1-c);
			fourses[n].decym1 = fourses[n].decy;
			
			val = fourses[n].val;
			val += fourses[n].adder;
			
			if(val <= fourses[n+1].val || val <= lolim ) {
				fourses[n].adder = fourses[n].incy;
			}
			else if( val >= fourses[n-1].val || val >= hilim ) {
				fourses[n].adder = fourses[n].decy;
			}
			
			fourses[n].val = val;
		}
		
		out1[i] = fourses[1].val;
		out2[i] = fourses[2].val;
		out3[i] = fourses[3].val;
		out4[i] = fourses[4].val;
		 
	 }
	
	
out:
	return w+7;	

}



void myObj_info(t_myObj *x) {
	int i;
	// only fourses 1 to 4 are used
	post("----- fourses.info -------");
	for(i=1; i<=NUMFOURSES; i++) {
		post("fourses[%ld].val = %f", i, x->fourses[i].val);
		post("fourses[%ld].inc = %f", i, x->fourses[i].inc);
		post("fourses[%ld].dec = %f", i, x->fourses[i].dec);
		post("fourses[%ld].adder = %f", i, x->fourses[i].adder);
	}	
	post("------ end -------");
}



void *myObj_new(t_symbol *s, long argc, t_atom *argv)
{
	t_myObj *x = object_alloc(myObj_class);
	dsp_setup((t_pxobject*)x, 0);			
	outlet_new((t_object *)x, "signal"); 
	outlet_new((t_object *)x, "signal"); 
	outlet_new((t_object *)x, "signal"); 
	outlet_new((t_object *)x, "signal"); 
	
	x->r_sr = 1.0/sys_getsr();
	if(sys_getsr() <= 0)
		x->r_sr = 1.0/44100.f;
	
	int i;
	for(i=1; i<=NUMFOURSES; i++) {
		x->fourses[i].val = 0.;
		x->fourses[i].inc = 0.01;
		x->fourses[i].dec = -0.01;
		x->fourses[i].adder = x->fourses[i].inc;
		
	}
	x->fourses[0].val = 1.;	// dummy 'horse' only used as high limit for fourses[1]
	x->fourses[5].val = -1.;	// dummy 'horse' only used as low limit for fourses[4]

	x->smoother = 0.01;	
	
	return x;
}


void myObj_assist(t_myObj *x, void *b, long m, long a, char *s) {
	if (m==1) {
		switch(a) {
			case 0: sprintf (s,"message inlet"); break;
		}
	}
	else {
		switch(a) {
			case 0: sprintf (s,"(signal) signal out osc1"); break;
			case 1: sprintf(s, "(signal) signal out osc2"); break;
			case 2: sprintf(s, "(signal) signal out osc3"); break;
			case 3: sprintf(s, "(signal) signal out osc4"); break;
		}
		
	}
}
