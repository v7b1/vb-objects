/* 
	a light weight signal indicator
	author: volker boehm, november 2015
*/

#include "ext.h"							// standard Max include, always required
#include "ext_obex.h"						// required for new style Max object
#include "z_dsp.h"
#include "ext_common.h"
#include "jpatcher_api.h"
#include "jgraphics.h"

#include <Accelerate/Accelerate.h>



typedef struct _myObj 
{
	t_pxjbox u_box;
	t_jrgba	u_outline;
	t_jrgba	u_background;
	t_double	sum, envval;
	t_double	level, lastlevel, lastval;
	t_double	slidedown, decr, thresh;
	long			count;
	long			maxcount;
	void			*p_clock, *p_clock2;
	void			*out;
} t_myObj;

void *myObj_new(t_symbol *s, long argc, t_atom *argv);
void myObj_free(t_myObj *x);
void myObj_assist(t_myObj *x, void *b, long m, long a, char *s);
void myObj_paint(t_myObj *x, t_object *patcherview);
t_max_err myObj_thresh_set(t_myObj *x, void *attr, long ac, t_atom *av);
t_max_err myObj_thresh_get(t_myObj *x, void *attr, long *ac, t_atom **av);

void myObj_bang(t_myObj *x);
void myObj_tick(t_myObj *x);
void myObj_outputEnv(t_myObj *x);

void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate,
				 long maxvectorsize, long flags);
void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins,
					 double **outs, long numouts, long sampleframes, long flags, void *userparam);


static t_class *s_myObj_class;

void ext_main(void *r)
{	
	t_class *c;
		
	c = class_new("vb.sigi~", (method)myObj_new, (method)myObj_free, sizeof(t_myObj), 0L, A_GIMME, 0);

	c->c_flags |= CLASS_FLAG_NEWDICTIONARY;
	jbox_initclass(c, JBOX_FIXWIDTH);
	
	class_addmethod(c, (method)myObj_dsp64,		"dsp64", A_CANT, 0);
	class_addmethod(c, (method)myObj_paint,		"paint",	A_CANT, 0);
	class_addmethod(c, (method)myObj_assist,		"assist",	A_CANT, 0);  

	class_addmethod(c, (method)myObj_bang,			"bang", 0);
	
	// attributes
	CLASS_ATTR_DOUBLE(c,"decrement", 0, t_myObj, decr);
	CLASS_ATTR_SAVE(c, "decrement", 0);
	CLASS_ATTR_FILTER_CLIP(c,"decrement",0.01, 0.5);
	CLASS_ATTR_LABEL(c,"decrement",0, "speed of fade out");
	
	CLASS_ATTR_DOUBLE(c,"threshold", 0, t_myObj, thresh);
	CLASS_ATTR_SAVE(c, "threshold", 0);
	CLASS_ATTR_FILTER_CLIP(c,"threshold", -90., -20.);
	CLASS_ATTR_LABEL(c,"threshold",0, "silence threshold (dB)");
	// override default accessors
	CLASS_ATTR_ACCESSORS(c, "threshold", (method)myObj_thresh_get,
						 (method)myObj_thresh_set);
	
	CLASS_ATTR_DOUBLE(c,"slide", 0, t_myObj, slidedown);
	CLASS_ATTR_SAVE(c, "slide", 0);
	CLASS_ATTR_FILTER_CLIP(c,"slide", 0.1, 1.);
	CLASS_ATTR_LABEL(c,"slide",0, "env slide down smoothing");

	CLASS_ATTR_DEFAULT(c,"patching_rect",0, "0. 0. 20. 20.");
	
	class_dspinitjbox(c);
	class_register(CLASS_BOX, c);
	s_myObj_class = c;

}

// custom attr setter and getter
t_max_err myObj_thresh_set(t_myObj *x, void *attr, long ac, t_atom *av) {
	if (ac && av) {
		t_atom_float thresh = atom_getlong(av);
		x->thresh = pow(10, thresh/20.);
	}
	return MAX_ERR_NONE;
}

t_max_err myObj_thresh_get(t_myObj *x, void *attr, long *ac, t_atom **av){
	if (ac && av) {
		char alloc;
		double threshdb = 20*log10(x->thresh);
		if (atom_alloc(ac, av, &alloc)) {
			return MAX_ERR_GENERIC;
		}
		atom_setlong(*av, threshdb);
	}
	return MAX_ERR_NONE;
}


void myObj_tick(t_myObj*x) {
	jbox_redraw((t_jbox *)x);
}

void myObj_outputEnv(t_myObj *x) {
	outlet_float(x->out, x->envval);
}

//64-bit dsp method
void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate,
				 long maxvectorsize, long flags) {
	object_method(dsp64, gensym("dsp_add64"), x, myObj_perform64, 0, NULL);
}


// 64 bit signal input version
void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins,
					 double **outs, long numouts, long sampleframes, long flags, void *userparam){
	
	t_double *in = ins[0];
	//int vs = sampleframes;
	t_double vecsum;
	t_double sum = x->sum;
	t_double value, lastval;
	t_double level = x->level;
	long			maxcount = x->maxcount;
	lastval = x->lastval;
	
	if (x->u_box.z_disabled)
		return;
	
	if (sampleframes > maxcount)
		maxcount = sampleframes;
	
	//scaler = (double)sampleframes / maxcount;		// TODO: check integer division
	/*
	while(vs--) {
		//insamp = fabs(*in++);
		insamp = (*in++);
		insamp *= insamp;
		
		if(insamp>=1) {			// TODO: check if signal clips!
			x->u_background.red = 1.0;
			x->u_background.green = 0.0;
			clock_delay(x->p_clock,0);
			break;
		}
		//sum += insamp;
	}
	*/
	vDSP_measqvD(in, 1, &vecsum, sampleframes);	// sum the squared input values
	sum += vecsum;		// sum the sums of the consecutiv vectors
	x->count += sampleframes;
	
	if (x->count >= maxcount) {
		x->count = 0;
		value = sum*sampleframes / maxcount;	// / maxcount;
		sum = 0;
		
		value = sqrt(value);
		
		if(lastval<value) lastval=value;				// slideup ist immer 1
		else lastval = (lastval+(value-lastval)*x->slidedown);
		
		//x->envval = lastval;
		//clock_delay(x->p_clock2,0);

		if (lastval>x->thresh) level = 1.0;
		else {
			level -= x->decr;
			if (level<0.25) level = 0.0;
		}
		
		if(level != x->lastlevel) {		// only draw, if level changed
			x->u_background.green = level;
			//post("sum: %f vs %f", level, x->lastlevel);
			x->lastlevel = level;
			clock_delay(x->p_clock,0);
		}
		
	}
	x->level = level;
	x->lastval = lastval;
	x->sum = sum;
	
}


void myObj_paint(t_myObj *x, t_object *patcherview)
{
	t_rect rect;
	t_jgraphics *g = (t_jgraphics*) patcherview_get_jgraphics(patcherview);		// obtain graphics context
	jbox_get_rect_for_view((t_object *)x, patcherview, &rect);

	// background
	jgraphics_set_source_jrgba(g, &x->u_background);
	jgraphics_rectangle(g, 0., 0., rect.width, rect.height);
	jgraphics_fill(g);
	
	// paint outline
	/*
	jgraphics_set_source_jrgba(g, &x->u_outline);
	jgraphics_set_line_width(g, 1.);
	jgraphics_rectangle(g, 0., 0., rect.width, rect.height);
	jgraphics_stroke(g);
	 */
}


void myObj_bang(t_myObj *x)
{
	post("bang!");
}


void myObj_assist(t_myObj *x, void *b, long m, long a, char *s)
{
	if (m == ASSIST_INLET)
		sprintf(s, "(signal) Audio Input");
}


void myObj_free(t_myObj *x)
{
	dsp_freejbox((t_pxjbox *)x);
	freeobject((t_object *)x->p_clock);
	//freeobject((t_object *)x->p_clock2);
	jbox_free((t_jbox *)x);
}

void *myObj_new(t_symbol *s, long argc, t_atom *argv)
{
	t_myObj *x = NULL;
 	t_dictionary *d = NULL;
	long boxflags;
	
	if (!(d = object_dictionaryarg(argc,argv)))
		return NULL;
   
	x = (t_myObj *)object_alloc(s_myObj_class);
	boxflags = 0 
		| JBOX_DRAWFIRSTIN 
		| JBOX_NODRAWBOX
		| JBOX_DRAWINLAST
		| JBOX_TRANSPARENT	
		//		| JBOX_NOGROW
		//		| JBOX_GROWY
		| JBOX_GROWBOTH
		//		| JBOX_HILITE
		//		| JBOX_BACKGROUND
		| JBOX_DRAWBACKGROUND
		//		| JBOX_NOFLOATINSPECTOR
		//		| JBOX_TEXTFIELD
		//		| JBOX_MOUSEDRAGDELTA
		//		| JBOX_TEXTFIELD
					;

	jbox_new((t_jbox *)x, boxflags, argc, argv);
	x->u_box.z_box.b_firstin = (void *)x;
	
	dsp_setupjbox((t_pxjbox*)x, 1);		// signal inlet
	//floatin(x, 1);
	
	//x->out = floatout(x);

	x->count = 0;
	x->maxcount = 4096;		// #samples interval
	x->sum = x->lastlevel = x->level = x->lastval = 0;

	x->slidedown = 0.75;		// was: 0.5
	x->decr = 0.2;
	x->thresh = 0.001;			// -60 dB
	
	// set color TODO: geht das auch anders?
	x->u_background.red = 0;
	x->u_background.green = 0;
	x->u_background.blue = 0;
	x->u_background.alpha = 1;
	
	
	x->p_clock = clock_new(x,(method)myObj_tick);
	//x->p_clock2 = clock_new(x,(method)myObj_outputEnv);
	attr_dictionary_process(x,d);
	jbox_ready((t_jbox *)x);


	return x;
}
