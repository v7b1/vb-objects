#include "ext.h"
#include "ext_obex.h"
#include "ext_common.h"

/*******************************/
/** take a list of values and output only those that have changed **/
/** output index and value **/
/** © volker boehm, mai 2007 **/
/******************************/

/* try a version with longer list... */

//#define MAXSIZE 4096

typedef struct {
	t_object b_ob;
	void    *out;
	void    *out_pos;
	void    *m_proxy;
	t_atom  *inList;				// input list
    long    maxsize;
	
} t_myObj;

void *myObj_class;


/**********************/
/* function prototypes */
void myObj_list(t_myObj *x, t_symbol *mess, short argc, t_atom *argv);
void myObj_reset(t_myObj *x);
//void *myObj_new( t_symbol *s, long argc, t_atom *argv );
void *myObj_new(long maxsize);
void myObj_assist(t_myObj *x, void *b, long m, long a, char *s);
void myObj_free(t_myObj *x);
long check_inlet(t_myObj *x);


int C74_EXPORT main(void) {
	t_class *c;
	c = class_new("vb.listchange", (method)myObj_new, (method)myObj_free, (short)sizeof(t_myObj),
				  0L, A_DEFLONG, 0L);
	class_addmethod(c, (method)myObj_list, "list", A_GIMME, 0);
	class_addmethod(c, (method)myObj_reset, "reset", 0);
	class_addmethod(c, (method)myObj_assist, "assist", 0);
	
	class_register(CLASS_BOX, c);
	myObj_class = c;
	
	post("vb.listchange, by volker böhm, 2013");
	
	return 0;
}



//void *myObj_new( t_symbol *s, long argc, t_atom *argv ) {
void *myObj_new( long maxsize ) {
	//int i;
	t_myObj *x;
	x = object_alloc(myObj_class);
	
	long m_inletNumber;
	x->m_proxy = proxy_new(x, 1, &m_inletNumber);	//create proxy inlet
	
	x->out = outlet_new(x, 0);		// right outlet: value that has changed
	x->out_pos = intout(x);			// left outlet: index of value
    
    if(maxsize > 0)
        x->maxsize = maxsize;
    else
        x->maxsize = 4096;
	
	x->inList = (t_atom *) sysmem_newptrclear(x->maxsize*sizeof(t_atom));
	//for (i=0; i < x->maxsize; i++)
		//atom_setlong(x->inList+i, 0);
	
	return x;
}



void myObj_list(t_myObj *x, t_symbol *mess, short argc, t_atom *argv) {
	int i;
	long	invali;
	double	invalf;
	
	if(argc > x->maxsize) argc = x->maxsize;
	
	if( check_inlet(x) ) {						// store list from second inlet
		// copy input list 
		for(i=0; i<argc; i++, argv++) {
			if(atom_gettype(argv) == A_LONG || atom_gettype(argv) == A_FLOAT ) 
				x->inList[i] = *argv; 
			else							
				object_error((t_object *)x, "only lists of ints or floats are accepted");
		}
	}
	else {								// search for changes
		for(i=0; i<argc; i++) {
			switch(atom_gettype(argv+i)) {
				case A_LONG:	
					invali = atom_getlong(argv+i);
					if( invali != atom_getlong(x->inList+i) ) {
						x->inList[i] = argv[i];
						outlet_int(x->out, invali);
						outlet_int(x->out_pos, i);
					}
					break;
				case A_FLOAT:
					invalf = atom_getfloat(argv+i);	
					if( invalf != atom_getfloat(x->inList+i) ) {
						x->inList[i] = argv[i]; 
						outlet_float(x->out, invalf);
						outlet_int(x->out_pos, i);
					}
					break;
				default:
					object_error((t_object *)x,"this list contains a symbol - we don't process these... \n"); break;
			}
		}
	}
}

void myObj_reset(t_myObj *x) {
	int i;
	for(i=0; i<x->maxsize; i++) {
		atom_setlong(x->inList+i, 0);
	}
}


long check_inlet(t_myObj *x) {
	return proxy_getinlet((t_object *)x);		// this is from chikashi (z.b. lesson13)
}



void myObj_assist(t_myObj *x, void *b, long m, long a, char *s) {
	if (m==ASSIST_INLET) {
		switch(a) {
			case 0: sprintf (s,"(list) input list to compare to last input list"); break;
			case 1: sprintf (s,"(list) set list to compare to"); break;
		}
	}
	else {
		switch(a) {
			case 0: sprintf (s,"(int) index of changed value"); break;
			case 1: sprintf (s,"(int/float) changed value"); break;
		}
	}
	
}


void myObj_free(t_myObj *x) {
	if(x->m_proxy)
		freeobject(x->m_proxy);
	
	 if(x->inList)
		 sysmem_freeptr(x->inList);
	 
}
