/* 
vb.gcd -- 
	calculate greates common divisor 
	and least common multiple 
	of two or more integers
	vb 2007
	
	lcm(a, b) = a*b/gcd(a,b)
 
	gcd of values a1... an:	gcd( gcd( gcd( a1, a2 ), a3 ), an )
	lcm of values a1... an:	lcm( lcm( lcm( a1, a2 ), a3 ), an )
	
 */

// update to Max SDK 6.1.4 -- Juli 2015, vb



#include "ext.h"
#include "ext_obex.h"
#include "ext_common.h"

#define MAXSIZE 256

typedef struct _myObj {
	struct object m_ob;
	int input[MAXSIZE];
	int listSize;
	void *out1;
	void *out2;
} t_myObj;

void *myObj_class;

void myObj_bang(t_myObj *x);
void myObj_int(t_myObj *x, long n);
void myObj_flt(t_myObj *x, double n);
void myObj_in1(t_myObj *x, long n);
void myObj_list(t_myObj *x, t_symbol *mess, short argc, t_atom *argv);
int euklid(long x, long y);
void myObj_assist(t_myObj *x, void *b, long m, long a, char *s);
void *myObj_new(long arg1);


void ext_main(void *r)
{
	t_class *c;
	c = class_new("vb.gcd_lcm", (method)myObj_new, 0, (short)sizeof(t_myObj), 0L, A_DEFLONG, 0L);
	class_addmethod(c, (method)myObj_bang, "bang", 0);
	class_addmethod(c, (method)myObj_int, "int", A_LONG, 0);
	class_addmethod(c, (method)myObj_flt, "float", A_FLOAT, 0);	
	class_addmethod(c, (method)myObj_in1, "in1", A_LONG, 0);
	class_addmethod(c, (method)myObj_list, "list", A_GIMME, 0);
	class_addmethod(c, (method)myObj_assist, "assist", A_CANT,0);
	
	class_register(CLASS_BOX, c);
	myObj_class = c;
	
	post("vb.gcd_lcm, volker böhm, 2007");
	
}



/*** recursively calculate gcd ***/
int euklid(long x, long y) 
{
	if(y==0) return x;
	else return euklid(y, x%y);
}


void myObj_int(t_myObj *x, long n)
{
	x->input[0] = n;
	x->listSize = 2;		// don't accidentally calculate all former list values
	myObj_bang(x);
}


void myObj_flt(t_myObj *x, double n) 
{
	int nn = (int)n;
	myObj_int(x, nn);
}

void myObj_in1(t_myObj *x, long n)
{
	x->input[1] = n;		// set second item in input list
	x->listSize = 2;
}


void myObj_list(t_myObj *x, t_symbol *mess, short argc, t_atom *argv) 
{
	int i;
	if(argc>MAXSIZE) argc = MAXSIZE;	
	x->listSize = argc;
	
	for (i = 0; i < argc; i++) {
		switch(atom_gettype(argv+i)) {
			case A_LONG: 
				x->input[i] = atom_getlong(argv+i);
				break;
			case A_FLOAT: 
				x->input[i] = atom_getfloat(argv+i);
				break;
			case A_SYM: 
				object_post((t_object *)x, "symbol (%s) not understood - only integers please!", atom_getsym(argv+i)->s_name);
				break;
			default:
				object_error((t_object *)x, "forbidden argument");
		}
	}

	myObj_bang(x);
}


/*** calc gcd and lcd ***/
void myObj_bang(t_myObj *x) 
{
	// initialize values
	long a, b, lcm, i;
	a = lcm = x->input[0];
	
	for(i=1; i<x->listSize; i++) {
		b = x->input[i];
		a = euklid(a, b);					// recursively calc gcd
		if(a!=0) 
			lcm = lcm*b/euklid(lcm, b);	// recursively calc lcm
		else {
			lcm = 0;
			break;
		}
	}
	outlet_int(x->out2, lcm);
	outlet_int(x->out1, a);
}



void myObj_assist(t_myObj *x, void *b, long m, long a, char *s)
{
	if (m==ASSIST_INLET) {
		switch (a) {
			case 0: sprintf(s,"(int, list) calculate gcd of left and right or list input"); break;
			case 1: sprintf(s,"(int) right input"); break;
		}
	}
	else {
		switch(a) {
			case 0: sprintf(s,"greates common divisor"); break;
			case 1: sprintf(s, "least common multiple"); break;
		}
	}
}

void *myObj_new(long arg1)
{
	t_myObj *x = object_alloc(myObj_class);
	
	if(x) {
		intin(x, 1);	
		x->out2 = intout(x);
		x->out1 = intout(x);
		
		int i;
		for(i=0; i<MAXSIZE; i++) {
			x->input[i] = 0;
		}
		x->listSize = 2;
		
		x->input[1] = arg1;
		//x->a = 0;
	}
	else {
		object_free(x);
		x = NULL;
	}
	
	
	return x;
}

