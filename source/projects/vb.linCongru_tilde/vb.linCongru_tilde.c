#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"
#include "ext_common.h"

/***********************************
	 linear congruential noise generator
	 with adjustable a, c and M parameters
	 for extra funky noise-loops....
	 xn+1 = ( a*x + c ) % m 
	 by volker boehm, june 2007
 
	8/27/2013 - update to maxsdk 6.1
 ***********************************/

// 22.02.2016 -- bug fixing...
// crashes in max5, giving up...


typedef struct {
	t_pxobject x_obj;	

	t_uint32	val;
	t_uint32	a;
	t_uint32	c;
	t_uint32	m;
	
	short	proc;
	short	inouts[3];

} t_myObj;




static t_class *myObj_class;

void myObj_int(t_myObj *x, long a);
void myObj_seta(t_myObj *x, long a);
void myObj_setc(t_myObj *x, long c);
void myObj_setM(t_myObj *x, long m);
void myObj_proc(t_myObj *x, long p);
//t_int *myObj_perform(t_int *w);
//void myObj_dsp(t_myObj *x, t_signal **sp, short *count);

// 64-bit DSP methods
void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, 
				 long maxvectorsize, long flags);
void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					 double **outs, long numouts, long sampleframes, long flags, void *userparam);

void *myObj_new( t_symbol *s, long argc, t_atom *argv);
void myObj_assist(t_myObj *x, void *b, long m, long a, char *s);


void ext_main(void *r) {
	t_class *c;
	
	c = class_new("vb.linCongru~", (method)myObj_new, (method)dsp_free,
				  (short)sizeof(t_myObj), 0L, A_GIMME, 0L);
	//class_addmethod(c, (method)myObj_dsp, "dsp", A_CANT, 0);
	class_addmethod(c, (method)myObj_dsp64, "dsp64", A_CANT, 0);
	
	class_addmethod(c, (method)myObj_int, "int", A_LONG, 0);
	/*
	class_addmethod(c, (method)myObj_seta, "int", A_LONG, 0);
	class_addmethod(c, (method)myObj_setc, "in1", A_LONG, 0);
	class_addmethod(c, (method)myObj_setM, "in2", A_LONG, 0);
	 */
	class_addmethod(c, (method)myObj_proc, "proc", A_LONG, 0);
	
	class_addmethod(c, (method)myObj_assist, "assist", A_CANT,0);
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	myObj_class = c;
	
	
	// attributes ==== noch nicht ganz fertig
	/*
	CLASS_ATTR_LONG(c,"interp", 0, t_myObj, interpol);
	CLASS_ATTR_DEFAULT_SAVE(c, "interp", 0, "1");		// default geht nicht.
	CLASS_ATTR_SAVE(c, "interp", 0);
	CLASS_ATTR_MIN(c, "interp", 0, "0");
	CLASS_ATTR_MAX(c, "interp", 0, "1");
	*/
	
    object_post(NULL, "vb.linCongru~ by volker böhm");
	
}


void myObj_int(t_myObj *x, long a) 
{
	switch (proxy_getinlet((t_object *)x)) {
		case 0: 
			myObj_seta(x, a); break;
		case 1:
			myObj_setc(x, a); break;
		case 2:
			myObj_setM(x, a); break;
	}
}


void myObj_proc(t_myObj *x, long p) {
	if(p!=0) x->proc = 1;
	else x->proc = 0;
}


void myObj_seta(t_myObj *x, long a) {
	x->a = a;
}
void myObj_setc(t_myObj *x, long c) {
	x->c = c;
}
void myObj_setM(t_myObj *x, long m) {
	x->m = m;
	if(x->m==0) x->m = 1;
}


// 32-bit dsp method
/*
void myObj_dsp(t_myObj *x, t_signal **sp, short *count) {
	int i;
	for(i=0; i<3; i++)
		x->inouts[i] = count[i];
	dsp_add(myObj_perform, 4, x, sp[0]->s_vec, sp[1]->s_vec, sp[0]->s_n );
}*/


//64-bit dsp method
void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, 
				 long maxvectorsize, long flags) {
	int i;
	for(i=0; i<3; i++)
		x->inouts[i] = count[i];
	object_method(dsp64, gensym("dsp_add64"), x, myObj_perform64, 0, NULL);
}


/* geht derzeit nicht mehr (copy from vb.fbosc2~) */
/*
t_int *myObj_perform(t_int *w) {
	
	t_myObj *x = (t_myObj *)(w[1]);
	t_float *in1 = (t_float *)(w[2]);
	t_float *out = (t_float *)(w[3]);
	int n = (int)(w[4]);
	int i;
		
	if (x->x_obj.z_disabled)
		goto out;
	if(!x->proc)
		goto zero;

	t_uint32 _val = x->val;
	t_uint32 _a = x->a;
	t_uint32 _c = x->c;
	t_uint32 _m = x->m;
	float _2over_m = 2.f/_m;	
	
	if(!x->inouts[0]) {	// if first signal input is not connected
		while(n--) {
			_val = (_val * _a + _c) % _m;
			*out++ = _val * _2over_m -1;
		}
	} else {	// first signal input connected
		for(i=0; i<n; i++) {
			_val = (_val * (t_uint32)in1[i] + _c) % _m;
			*out++ = _val * _2over_m -1;
		}
	}

	x->val = _val;
	
out:
	return w+5;
	
zero:
	while (n--)  
		*out++ = 0.f;
	return w+5;
}
*/

// 64 bit signal input version
void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					   double **outs, long numouts, long sampleframes, long flags, void *userparam) {

	t_double *in1 = ins[0];
	t_double *out = outs[0];	
	int n = sampleframes;
	int i;
	
	if (x->x_obj.z_disabled)
		return;
	if(!x->proc)
		goto zero;

	t_uint32	_val = x->val;
	t_uint32	_a = x->a;
	t_uint32	_c = x->inouts[1] ? (t_uint32)ins[1][0] : x->c;
	t_uint32	_m;		//= x->inouts[2] ? (t_uint32)ins[2][0]: x->m;
	if(x->inouts[2]) {
		_m = (t_uint32)(ins[2][0]);
		if(_m==0) _m = 1;
	} else _m = x->m;
	t_double	_2over_m = 2.0/_m;	
	
	if(!x->inouts[0]) {	// if first signal input is not connected
		while(n--) {
			_val = (_val * _a + _c) % _m;
			*out++ = _val * _2over_m -1;
		}
	} else {	// first signal input connected
		for(i=0; i<n; i++) {
			_val = (_val * (t_uint32)in1[i] + _c) % _m;
			*out++ = _val * _2over_m -1;
		}
	}
	
	x->val = _val;

	return;
	
zero:
	while (n--)  
		*out++ = 0.;
}




void *myObj_new(t_symbol *s, long argc, t_atom *argv)
{
	t_myObj *x = object_alloc(myObj_class);
	if(x) {
		//intin((t_object *)x, 2);
		//intin((t_object *)x, 1);
		dsp_setup((t_pxobject*)x, 3); 
		outlet_new((t_pxobject*)x, "signal"); 
		
		x->a = 150359821;
		x->c = 1;
		x->m = 0x7FFFFFFF;
		x->val = 0xDEADBEEF;
		x->proc = 1;
		
		//attr_args_process(x, argc, argv);			// process attributes
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
			case 0: sprintf (s,"(signal/int) a"); break;
			case 1: sprintf (s,"(signal/int) c"); break;
			case 2: sprintf (s,"(signal/int) m"); break;
		}
	}
	else {
		switch(a) {
			case 0: sprintf (s,"(signal) noise out"); break;
		}
		
	}
}
