#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"
#include "ext_common.h"
#include <math.h>

#include "vb.jonverb.h"


/*
	vb.jonverb~
 
	an inexpensive artificial reverberator, 
	designed after an article by Jon Dattorro (Effect Design Part1)
	J. Audio Eng. Soc, Vol. 45 No 9, 1997
 
	author: Volker Böhm, April 2013
	https://vboehm.net
 
 */

#define DIFFORDER 4
#define NOISEINJECT 32			// noise injection period in samples

typedef struct {
	t_pxobject	b_ob;
	double		rate;
	double		inputbandwidth;
	g_damper	*inputDamper;
	g_diffuser	**inputDiffusers;
	g_diffuser	**decayDiffusers1;
	g_diffuser	**decayDiffusers2;
	g_tapdelay	**tapdelays;
	double		fbL, fbR, decay, erfl_gain, tail_gain;
} t_myObj;


void myObj_set_damping(t_myObj *x, double a);
void myObj_set_decay(t_myObj *x, double a);
void myObj_clear(t_myObj *x);
// custom attr setter
t_max_err myObj_inputbw_set(t_myObj *x, t_object *attr, long argc, t_atom *argv);

void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, 
				 long maxvectorsize, long flags);
void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, double **outs, 
					 long numouts, long sampleframes, long flags, void *userparam);

void myObj_assist(t_myObj *x, void *b, long m, long a, char *s);
void *myObj_new(t_symbol *s, long argc, t_atom *argv);
void *myObj_class;
void myObj_free(t_myObj *x);



void ext_main(void *r) {
	t_class *c;
	c = class_new("vb.jonverb~", (method)myObj_new, (method)myObj_free, 
				  (short)sizeof(t_myObj), 0L, A_GIMME, 0L);
	
	class_addmethod(c, (method)myObj_set_damping, "ft2", A_FLOAT, 0);
	class_addmethod(c, (method)myObj_set_decay, "ft1", A_FLOAT, 0);
	class_addmethod(c, (method)myObj_clear, "clear", 0);
	class_addmethod(c, (method)myObj_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c, (method)myObj_assist,"assist", A_CANT,0);
	class_dspinit(c);
	
	// attributes ==== 
	CLASS_ATTR_DOUBLE(c,"inputbw", 0, t_myObj, inputbandwidth);
	CLASS_ATTR_SAVE(c, "inputbw", 0);
	CLASS_ATTR_ACCESSORS(c, "inputbw", 0, myObj_inputbw_set);	// <-- custom setter!
	CLASS_ATTR_FILTER_CLIP(c,"inputbw", 0, 1); 
	CLASS_ATTR_LABEL(c,"inputbw",0, "input bandwidth");
	
	CLASS_ATTR_DOUBLE(c,"erfl", 0, t_myObj, erfl_gain);
	CLASS_ATTR_SAVE(c, "erfl", 0);
	CLASS_ATTR_FILTER_CLIP(c,"erfl", 0, 1); 
	CLASS_ATTR_LABEL(c,"erfl", 0, "early reflections");
	
	CLASS_ATTR_DOUBLE(c,"tail", 0, t_myObj, tail_gain);
	CLASS_ATTR_SAVE(c, "tail", 0);
	CLASS_ATTR_FILTER_CLIP(c,"tail", 0, 1); 
	CLASS_ATTR_LABEL(c,"tail", 0, "reverb tail");
	
	// display order
	CLASS_ATTR_ORDER(c, "inputbw",	0, "1");
	CLASS_ATTR_ORDER(c, "erfl",	0, "2");
	CLASS_ATTR_ORDER(c, "tail", 0, "3");
	
	class_register(CLASS_BOX, c);
	myObj_class = c;
	
	
	post("vb.jonverb~ by volker böhm - version 1.0.1 -- https://vboehm.net");

}


#pragma mark attr setter -------------
// custom attribute setter
t_max_err myObj_inputbw_set(t_myObj *x, t_object *attr, long argc, t_atom *argv)
{
	double bw = atom_getfloat(argv);
	x->inputbandwidth = bw;				
	x->inputDamper->damping = 1.0 - CLAMP(bw, 0., 0.997);
	
	return 0;
}


#pragma mark methods --------------
/*~~~~~~~~~~~~~~~~~~ METHODS ~~~~~~~~~~~~~~~~~~*/

// right inlet
void myObj_set_damping(t_myObj *x, double a) {
	// first two tapdelays in the reverb tail have a onepole filter at the feedback output.
	x->tapdelays[0]->damping = CLAMP(a, 0., 1.);
	x->tapdelays[1]->damping = CLAMP(a, 0., 1.);
}

// middle inlet
void myObj_set_decay(t_myObj *x, double a) {
	x->decay = CLAMP(a, 0., 1.);
}


void myObj_clear(t_myObj *x) {
	int i;
	for(i=0; i<DIFFORDER; i++) {
		memset(x->tapdelays[i]->buf, 0, x->tapdelays[i]->size*sizeof(double));
	}
	x->fbL = x->fbR = 0.;
	
	memset(x->decayDiffusers1[0]->buf, 0, x->decayDiffusers1[0]->size*sizeof(double));
	memset(x->decayDiffusers1[1]->buf, 0, x->decayDiffusers1[1]->size*sizeof(double));
	memset(x->decayDiffusers2[0]->buf, 0, x->decayDiffusers2[0]->size*sizeof(double));
	memset(x->decayDiffusers2[1]->buf, 0, x->decayDiffusers2[1]->size*sizeof(double));
}



#pragma mark dsp routines -----------

void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, 
				 long maxvectorsize, long flags) {
	object_method(dsp64, gensym("dsp_add64"), x, myObj_perform64, 0, NULL);
	
	x->rate = samplerate;
	if(x->rate<=0) x->rate = 44100.0;
	
}


void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, double **outs, 
					 long numouts, long sampleframes, long flags, void *userparam){
	
	t_double *in = ins[0];
	t_double *outL = outs[0];	
	t_double *outR = outs[1];
	int vs = (int)sampleframes;
	int i, k;
	double input, sumL, sumR, fbL, fbR, fbholdL;
	double decay, erfl_gain, tail_gain;
	
	if (x->b_ob.z_disabled)
		return;

	fbL = x->fbL;
	fbR = x->fbR;
	erfl_gain = x->erfl_gain;
	tail_gain = x->tail_gain;
	tail_gain *= tail_gain;	// make it a little softer...
	decay = x->decay;
	sumL = sumR = 0.;
	g_damper *inputDamper = x->inputDamper;
	g_diffuser **inputDiffs = x->inputDiffusers;
	g_diffuser **decayDiffs1 = x->decayDiffusers1;
	g_diffuser **decayDiffs2 = x->decayDiffusers2;
	g_tapdelay **tapDelays = x->tapdelays;
	
	// kick in some noise to keep denormals away
	i=0;
	while(i<vs) {
		in[i] += DBL_EPSILON;
		i += NOISEINJECT;
	}
	
	damper_do_block(inputDamper, in, vs);				// input damping
	
	for(k=0; k<DIFFORDER; k++) {						// diffusors (allpass filters)
		diffuser_do_block(inputDiffs[k], in, vs);
	}
	
	// tail
	for(i=0; i<vs; i++) 
	 {
		sumL = sumR = 0.;
		input = in[i];
		fbL += input;		// cross channel feedback
		fbR += input;
		
		// first decay defuser (should be modulated in the end)
		fbL = diffuser_do(decayDiffs1[0], fbL);
		fbR = diffuser_do(decayDiffs1[1], fbR);
		
		// first fixed delay with tap outputs (+ freq dependent damping)
		fbL = tapdelay1_do_left(tapDelays[0], fbL, &sumL, &sumR);
		fbR = tapdelay1_do_right(tapDelays[1], fbR, &sumL, &sumR);
		
		// freq independent feedback control
		fbL *= decay;						
		fbR *= decay;
		
		// second decay diffusers + tap outputs
		fbL = diffuser_do_decay(decayDiffs2[0], fbL, &sumL, &sumR);
		fbR = diffuser_do_decay(decayDiffs2[1], fbR, &sumL, &sumR);
		
		// second fixed delay with tap outputs
		fbL = tapdelay2_do_left(tapDelays[2], fbL, &sumL, &sumR);
		fbR = tapdelay2_do_right(tapDelays[3], fbR, &sumL, &sumR);
		
		
		// levels and output
		input *= erfl_gain;
		*(outL++) = sumL*tail_gain + input;
		*(outR++) = sumR*tail_gain + input;
		
		
		// cross channel feedback
		fbholdL = fbL;
		fbL = fbR;		
		fbR = fbholdL;
	}
	
	x->fbL = fbL;
	x->fbR = fbR;
	
}




void myObj_assist(t_myObj *x, void *b, long m, long a, char *s) {
	if (m == ASSIST_INLET) {
		switch(a) {
			case 0: sprintf (s,"(signal) audio input"); break;
			case 1: sprintf (s,"(float) decay/reverb time (0.--1.)"); break;
			case 2: sprintf (s,"(float) reverb damping (0.--1.)"); break;
		}
	}
	else {
		switch(a) {
			case 0: sprintf (s,"(signal) audio out left"); break;
			case 1: sprintf(s, "(signal) audio out right"); break;
		}
		
	}
}



void *myObj_new(t_symbol *s, long argc, t_atom *argv) 
{
    // original sampling rate as stated in Dattoros paper was: 29761 Hz
    // delay times:
    // @ 29761 Hz -> @ 44100 kHz    // TODO: make this current SR dependent
    // --- left out
    // 266  ->  394     node: 48
    // 2974 ->  4407    node: 48
    // 1913 ->  2835        node: 55_59
    // 1996 ->  2958    node: 59
    // 1990 ->  2949    node: 24
    // 187  ->  277         node: 31_33
    // 1066 ->  1580    node: 33
    // --- right out
    // 353  ->  523     node: 24
    // 3627 ->  5375    node: 24
    // 1228 ->  1820        node: 31_33
    // 2673 ->  3961    node: 33
    // 2111 ->  3128    node: 48
    // 335  ->  496         node: 55_59
    // 121  ->  179     node: 59
    
	//fixed delay taps
    int taps0[4] = {6598, 2949, 523, 5375}; // node: 24_30
    int taps1[4] = {6249, 394, 4407, 3128}; // node: 48_54
    int taps2[4] = {5512, 1580, 3961, 0};   // node: 33
    int taps3[4] = {4687, 2958, 179, 0};    // node: 59
	
	// allpass delay taps
	int difftaps0[3] = {210, 0, 0};
	int difftaps1[3] = {159, 0, 0};
	int difftaps2[3] = {562, 0, 0};
	int difftaps3[3] = {410, 0, 0};
	int difftaps4[3] = {996, 0, 0};
	int difftaps5[3] = {1345, 0, 0};
    int difftaps6[3] = {2667, 277, 1820};   // node: 31_33
    int difftaps7[3] = {3936, 2835, 496};   // node: 55_59
	
	t_myObj *x = NULL;
	
    x = (t_myObj *)object_alloc(myObj_class);
    
	if( x ) {
		
		dsp_setup((t_pxobject*)x, 1);				// one signal inlet
		outlet_new((t_pxobject *)x, "signal");		// two signal outlets
		outlet_new((t_pxobject *)x, "signal"); 
		
		floatin(x, 2);			// damping
		floatin(x, 1);			// decay
		
		// initialize variables
		if(sys_getsr()>0)
			x->rate = sys_getsr();	
		else x->rate = 44100.0;	

		x->erfl_gain = 0.5;
		x->tail_gain = 0.2;
		x->decay = 0.75;
		x->inputbandwidth = 0.5;
		x->inputDamper = damper_make(1.0-x->inputbandwidth);
		x->inputDiffusers = (g_diffuser **)calloc(DIFFORDER, sizeof(g_diffuser *));
		x->inputDiffusers[0] = diffuser_make(1024, difftaps0, 0.75);
		x->inputDiffusers[1] = diffuser_make(1024, difftaps1, 0.75);
		x->inputDiffusers[2] = diffuser_make(1024, difftaps2, 0.625);
		x->inputDiffusers[3] = diffuser_make(1024, difftaps3, 0.625);
		
		// first left and right diffusers in tail
		x->decayDiffusers1 = (g_diffuser **)calloc(2, sizeof(g_diffuser *));
		x->decayDiffusers1[0] = diffuser_make(4096, difftaps4, -0.7);
		x->decayDiffusers1[1] = diffuser_make(4096, difftaps5, -0.7);
		
		// tapdelays in tail
		x->tapdelays = (g_tapdelay **)calloc(4, sizeof(g_tapdelay *));
		x->tapdelays[0] = tapdelay_make(8192, taps0);
		x->tapdelays[1] = tapdelay_make(8192, taps1);
		x->tapdelays[2] = tapdelay_make(8192, taps2);
		x->tapdelays[3] = tapdelay_make(8192, taps3);
				
		// second left and right diffusers in tail
		x->decayDiffusers2 = (g_diffuser **)calloc(2, sizeof(g_diffuser *));
		x->decayDiffusers2[0] = diffuser_make(4096, difftaps6, -0.5);
		x->decayDiffusers2[1] = diffuser_make(4096, difftaps7, -0.5);
		
		x->fbL = x->fbR = 0.;
		
		
		// ----- parse arguments: first arg --> "decay", second arg --> "damping" 
		if(argc>=1) {
			double decay=0;
			switch(atom_gettype(argv)) {
				case A_LONG: 
					decay = atom_getlong(argv);
					break;
				case A_FLOAT: 
					decay = atom_getfloat(argv);
					break;
			}
			x->decay = CLAMP(decay, 0., 1.);
		}
		if(argc>=2) {
			double damping=0;
			switch(atom_gettype(argv+1)) {
				case A_LONG: 
					damping = atom_getlong(argv+1);
					break;
				case A_FLOAT: 
					damping = atom_getfloat(argv+1);
					break;
			}
			x->tapdelays[0]->damping = CLAMP(damping, 0., 1.);
			x->tapdelays[1]->damping = CLAMP(damping, 0., 1.);
		}
		
		
		attr_args_process(x, argc, argv);			// process attributes
		
	}	
	else {
		freeobject((t_object *)x);
		x = NULL;
	}

	return x;
}


void myObj_free(t_myObj *x) {
	int i;
	dsp_free((t_pxobject *)x);
	damper_free(x->inputDamper);
	for(i = 0; i < DIFFORDER; i++) {
		tapdelay_free(x->tapdelays[i]);
		//damper_free(p->fdndamps[i]);
		diffuser_free(x->inputDiffusers[i]);
	}
	diffuser_free(x->decayDiffusers1[0] );
	diffuser_free(x->decayDiffusers1[1] );
	diffuser_free(x->decayDiffusers2[0] );
	diffuser_free(x->decayDiffusers2[1] );
}
