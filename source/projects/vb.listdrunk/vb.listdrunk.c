#include "ext.h"
#include "ext_obex.h"
#include "ext_common.h"

#include <limits.h>
#include "mt64.h"

/*******************************/
/**	a drunk object for lists of integers		**/
/**		© volker boehm, juli 2007		**/
/**		this is the new version, 2013		**/
/**		updated for max6.1.2 SDK, april 2013  **/
/**     Maerz 2018, new random generator, now uses mt19937 by Nishimura and Matsumoto **/
/******************************/

/** now we use XORSHIFT RNG by G. Marsaglia, even better? **/

#define MAXSIZE 4096

typedef struct {
	t_object	b_ob;
	void		*out;
	t_atom		*inList;				// input list
    t_atom_long *inListLong;
    double      *inListDouble;
	int			listSize;

	double		stepSize;
	double		lowerLimit;
	double		upperLimit;
	short		fmode;				// floatingpoint mode
} t_myObj;




/**********************/
/* function prototypes */
void myObj_list(t_myObj *x, t_symbol *mess, short argc, t_atom *argv);
void myObj_set(t_myObj *x, t_symbol *mess, short argc, t_atom *argv);
void myObj_reset(t_myObj *x);
void myObj_bang(t_myObj *x);
void myObj_int(t_myObj *x, long input);
void myObj_float(t_myObj *x, double input);
//void myObj_randinit(t_myObj *x, t_uint32 seed);
void myObj_ft1(t_myObj *x, double input);
void myObj_ft2(t_myObj *x, double input);

void myObj_info(t_myObj *x);
void *myObj_new( t_symbol *s, long argc, t_atom *argv );
void myObj_assist(t_myObj *x, void *b, long m, long a, char *s);
void *myObj_class;
void myObj_free(t_myObj *x);

double fold(double in, double lo, double hi);
double frand64();
double xor64();
unsigned long long rdtsc();

void *myObj_class;



// this will be global to all instances of this object
// so all objects will draw from the same RNG, which is ok, no?
static unsigned long long z = 88172645463325252LL;



void ext_main(void *r) {
	t_class *c;
	c = class_new("vb.listdrunk", (method)myObj_new, (method)myObj_free, (short)sizeof(t_myObj),
				  0L, A_GIMME, 0L);
	class_addmethod(c, (method)myObj_int, "int", A_LONG, 0);
	class_addmethod(c, (method)myObj_float, "float", A_FLOAT, 0);
	class_addmethod(c, (method)myObj_list, "list", A_GIMME, 0);
	class_addmethod(c, (method)myObj_set, "set", A_GIMME, 0);
	class_addmethod(c, (method)myObj_reset, "reset", 0);
	class_addmethod(c, (method)myObj_bang, "bang", 0);
	class_addmethod(c, (method)myObj_info, "info", 0);
	class_addmethod(c, (method)myObj_ft1, "ft1", A_FLOAT, 0);
	class_addmethod(c, (method)myObj_ft2, "ft2", A_FLOAT, 0);
	class_addmethod(c, (method)myObj_assist, "assist", A_CANT, 0L);
	
	class_register(CLASS_BOX, c);
	myObj_class = c;
	
	post("vb.listdrunk by volker böhm, april 2013");

}


// list input in left inlet
void myObj_list(t_myObj *x, t_symbol *mess, short argc, t_atom *argv) {
	myObj_set(x, mess, argc, argv);
	myObj_bang(x);
}


void myObj_set(t_myObj *x, t_symbol *mess, short argc, t_atom *argv) {
	int i;
	
	if(argc>MAXSIZE) argc = MAXSIZE;	
	x->listSize = argc;
    
    if(atom_gettype(argv) == A_LONG)
        atom_getlong_array(argc, argv, MAXSIZE, x->inListLong);
    else if(atom_gettype(argv) == A_FLOAT)
        atom_getdouble_array(argc, argv, MAXSIZE, x->inListDouble);
	
	// copy input list 
	for(i=0; i<argc; i++, argv++) {
		if(atom_gettype(argv) == A_LONG || atom_gettype(argv) == A_FLOAT ) 
			x->inList[i] = *argv; 
		else							
			object_error((t_object *)x, "only lists of ints or floats are accepted");
	}
	
}


void myObj_bang(t_myObj *x)
{
	long    i;
	double  valD;
    int     listSize = x->listSize;
    double  stepSize = x->stepSize;
    double  lowerLimit = x->lowerLimit;
    double  upperLimit = x->upperLimit;
    double  *inListDouble = x->inListDouble;
    t_atom_long *inListLong = x->inListLong;
	
    
    if(lowerLimit>upperLimit)
        return;     // don't do anything, if limits aren't set correctly
    
    else if(lowerLimit == upperLimit) {
        if (atom_gettype(&x->inList[0]) == A_LONG) {
            for(i=0; i<listSize; i++)
                inListLong[i] = (long)lowerLimit;
            atom_setlong_array(listSize, x->inList, MAXSIZE, inListLong);
        } else if (atom_gettype(&x->inList[0]) == A_FLOAT) {
            for(i=0; i<listSize; i++)
                inListDouble[i] = lowerLimit;
            atom_setdouble_array(listSize, x->inList, MAXSIZE, inListDouble);
        }
    }
    
    else {
        if(stepSize > 0) {
            
            // check list type: (real lazy, only checking first element...)
            if (atom_gettype(&x->inList[0]) == A_LONG) {
                for(i=0; i<listSize; i++) {
                    double r = xor64();     // //xor64() //frand64()
                    valD = inListLong[i] + r*stepSize;
                    inListLong[i] = (long)(fold(valD, lowerLimit, upperLimit)+0.5);
                }
                atom_setlong_array(listSize, x->inList, MAXSIZE, inListLong);
            }
            
            else if (atom_gettype(&x->inList[0]) == A_FLOAT) {
                for(i=0; i<listSize; i++) {
                    double r = xor64();
                    valD = inListDouble[i] + r*stepSize;
                    inListDouble[i] = fold(valD, lowerLimit, upperLimit);
                }
                atom_setdouble_array(listSize, x->inList, MAXSIZE, inListDouble);
            }
            
            else {
                object_error((t_object*)x, "works only with lists of numbers");
            }
        }
    }
	outlet_list(x->out, 0L, listSize, x->inList);
}



void myObj_int(t_myObj *x, long input) {
	x->stepSize = labs(input);
}

void myObj_float(t_myObj *x, double input) {
	x->stepSize = fabs(input);
}

void myObj_ft1(t_myObj *x, double input) {
	x->lowerLimit = input;
    if(input > x->upperLimit) {
        //x->upperLimit = input;
        object_warn((t_object*)x, "lower limit higher than upper limit!");
    }
}

void myObj_ft2(t_myObj *x, double input) {
	x->upperLimit = input;
    if(input < x->lowerLimit) {
        //x->lowerLimit = input;
        object_warn((t_object*)x, "upper limit lower than lower limit!");
    }
}



double fold(double in, double lo, double hi) 
{
	double x, c, range, range2;
	x = in - lo;
	
	// avoid the divide if possible
	if (in >= hi) {
		in = hi + hi - in;
		if (in >= lo) return in;
	} else if (in < lo) {
		in = lo + lo - in;
		if (in < hi) return in;
	} else return in;
	
	if (hi == lo) return lo;
	// ok do the divide
	range = hi - lo;
	range2 = range + range;
	c = x - range2 * floor(x / range2);
	if (c>=range) c = range2 - c;
	return c + lo;
}


#pragma mark random functions ---

double frand64() {
    // return a double rand val from -1.0 to +0.999...
    return ((genrand64_int64() >> 11) * (2.0/9007199254740991.0) - 1.0);
}


double xor64() {
    z ^= (z << 13);
    z ^= (z >> 7);
    z ^= (z << 17);
    
    return ( (double)z / LLONG_MAX ) - 1.0 ;
}



/*
t_uint32 trand(t_myObj *x) {
	// generate a random 32 bit number
	x->s1 = ((x->s1 &  -2) << 12) ^ (((x->s1 << 13) ^  x->s1) >> 19);
	x->s2 = ((x->s2 &  -8) <<  4) ^ (((x->s2 <<  2) ^  x->s2) >> 25);
	x->s3 = ((x->s3 & -16) << 17) ^ (((x->s3 <<  3) ^  x->s3) >> 11);
	return (x->s1 ^ x->s2 ^ x->s3);
}

double drand(t_myObj *x) {
	// return a double from 0.0 to 0.999...
#if BYTE_ORDER == BIG_ENDIAN
	union { struct { uint32 hi, lo; } i; double f; } du;
#else
	union { struct { uint32 lo, hi; } i; double f; } du;
#endif
	du.i.hi = 0x41300000; 
	du.i.lo = trand(x);
	return du.f - 1048576.;
}

t_int32 irand(t_myObj *x, t_int32 scale) {
	// return a int from -scale to +scale
	return (t_int32)floor((2. * scale + 1.) * drand(x) - scale);
}

float frand(t_myObj *x, double stepSize) {
	// return a float from -1.0 to +0.999... * stepSize
	// so actually only 32-bit randomness, but who cares...
	union { t_uint32 i; float f; } u;		// union for floating point conversion of result
	u.i = 0x40000000 | (trand(x) >> 9);
	return (u.f - 3.f) * stepSize;
}



void myObj_randinit(t_myObj *x, t_uint32 seed){
	// initialize seeds
	x->s1 = 1243598713U ^ seed; if (x->s1 <  2) x->s1 = 1243598713U;
	x->s2 = 3093459404U ^ seed; if (x->s2 <  8) x->s2 = 3093459404U;
	x->s3 = 1821928721U ^ seed; if (x->s3 < 16) x->s3 = 1821928721U;	
}
*/

void myObj_reset(t_myObj *x) 
{
	int     i;
    double  lowerLimit = x->lowerLimit;
    long    lowerLimitL = (long)x->lowerLimit;
	for(i=0; i<MAXSIZE; i++) {
		atom_setlong( x->inList+i, lowerLimit );
        x->inListLong[i] = lowerLimitL;
        x->inListDouble[i] = lowerLimit;
	}
}


void myObj_info(t_myObj *x) 
{
	object_post((t_object *)x, "stepSize: %f", x->stepSize);
	object_post((t_object *)x, "lowerLimit: %f", x->lowerLimit);
	object_post((t_object *)x, "upperLimit: %f", x->upperLimit);
}


void myObj_free(t_myObj *x) {
	if(x->inList)
		sysmem_freeptr(x->inList);
    if(x->inListLong)
        sysmem_freeptr(x->inListLong);
    if(x->inListDouble)
        sysmem_freeptr(x->inListDouble);
}



void *myObj_new( t_symbol *s, long argc, t_atom *argv ) {
	int i;
	t_myObj *x;
	x = object_alloc(myObj_class);
	
	floatin(x, 2);
	floatin(x, 1);
	
	x->out = listout(x);					// create a list outlet
	x->listSize = 10;
	
	
	// check arguments:
	if(argc>=1) {
		if(atom_gettype(argv)== A_LONG)
			x->stepSize = atom_getlong(argv);
		else if(atom_gettype(argv)== A_FLOAT)
			x->stepSize = atom_getfloat(argv);
		else {
			object_error((t_object *)x, "bad argument, setting stepSize to 1");
			x->stepSize = 1.0;
		}
	}
	else {
		x->stepSize = 1.0;
	}
	
	if(argc>=2) {
		if(atom_gettype(argv+1)== A_LONG)
			x->lowerLimit = atom_getlong(argv+1);
		else if(atom_gettype(argv+1)== A_FLOAT)
			x->lowerLimit = atom_getfloat(argv+1);
		else {
			object_error((t_object *)x, "bad argument, setting lowerLimit to 0");
			x->lowerLimit = 0.0;
		}
	}
	else {
		x->lowerLimit = 0.0;
	}
	
	if(argc>=3) {
		if(atom_gettype(argv+2)== A_LONG)
			x->upperLimit = atom_getlong(argv+2);
		else if(atom_gettype(argv+2)== A_FLOAT)
			x->upperLimit = atom_getfloat(argv+2);
		else {
			object_error((t_object *)x, "bad argument, setting upperLimit to 127");
			x->upperLimit = 127.0;
		}
	}
	else {
		x->upperLimit = 127.0;
	}
	
	
	if(x->lowerLimit >= x->upperLimit) {
		object_error((t_object *)x, "resetting min to 0 and max to 127");
		x->lowerLimit = 0.0;
		x->upperLimit = 127.0;
	}
	
	// alloc mem
	x->inList = (t_atom *) sysmem_newptr(MAXSIZE*sizeof(t_atom));
	for (i=0; i < MAXSIZE; i++)
		atom_setlong(x->inList+i, 0);
    
    x->inListLong = (t_atom_long *)sysmem_newptrclear(MAXSIZE * sizeof(t_atom_long));
    x->inListDouble = (double *)sysmem_newptrclear(MAXSIZE * sizeof(double));
    
//    z = rdtsc();    // seed the RNG
    //post("z: %llu", z);

	myObj_reset(x);		// initialize inList
	
	return x;
}


//unsigned long long rdtsc() {
//    unsigned int lo,hi;
//    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
//    return ((unsigned long long)hi << 32) | lo;
//}


void myObj_assist(t_myObj *x, void *b, long m, long a, char *s) {
	if (m==ASSIST_INLET) {
		switch (a) {
			case 0: sprintf(s,"(list) sets and outputs start-list, (int/float) sets step size"); break;
			case 1: sprintf(s,"(int/float) sets minimum"); break;
			case 2: sprintf(s,"(int/float) sets maximum");
		}
	}
	else {
		sprintf(s,"(list) drunk list output");
	}
}
