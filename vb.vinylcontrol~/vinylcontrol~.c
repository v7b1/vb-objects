#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"
#include "ext_common.h"

#include "timecoder.h"

/*
	make a new version of vinylcontrol~
*/

#define SCALE ((double)(1<<15))
#define MAXBLOCK 128

// (from maxxx)
// Sample threshold below which we consider there to be no signal.
#define SAMPLE_MAX SHRT_MAX
const double kMinSignal = 75.0 / SAMPLE_MAX;


typedef struct {
	t_pxobject x_obj;
	double          sr;
	void            *outpitch, *outpos, *outtc;
	struct timecoder tc;
	short           *pcm;
	long            numFrames;
	void			*m_clock;
	int             count;
} t_myObj;


static t_class *myObj_class;

void myObj_bang(t_myObj *x);
void myObj_output_values(t_myObj *x);

// DSP methods
void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, 
				 long maxvectorsize, long flags);
void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					 double **outs, long numouts, long sampleframes, long flags, void *userparam);

void myObj_tick(t_myObj *x);
void myObj_stop(t_myObj *x);
void myObj_free(t_myObj *x);
void myObj_dspstate(t_myObj *x, long n);
void *myObj_new(t_symbol* vt, long phonoline);
void myObj_assist(t_myObj *x, void *b, long m, long a, char *s);


int C74_EXPORT main(void) {
	t_class *c;
	
	c = class_new("vb.vinylcontrol~", (method)myObj_new, (method)myObj_free, (short)sizeof(t_myObj), 0L,
				  A_DEFSYM, A_DEFLONG, 0L);
	class_addmethod(c, (method)myObj_dsp64, "dsp64", A_CANT, 0);
//    class_addmethod(c, (method)myObj_bang, "bang", 0);
//    class_addmethod(c, (method)myObj_dspstate, "dspstate", A_CANT, 0);
	class_addmethod(c, (method)myObj_assist, "assist", A_CANT,0);
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	myObj_class = c;
	
	
	post("vb.vinylcontrol~ by https://vboehm.net - using xwax 1.7");
	
	return 0;
}




void myObj_bang(t_myObj *x) {
	clock_delay(x->m_clock, 0);
//    timecoder_status();
}
void myObj_tick(t_myObj *x) {
	clock_delay(x->m_clock, 23);
	myObj_output_values(x);
}

void myObj_stop(t_myObj *x) {
	clock_unset(x->m_clock);
}


void myObj_output_values(t_myObj *x)
{
	struct timecoder tc = x->tc;
	double when, tcpos, pos, pitch;
	unsigned int timecode = timecoder_get_position(&tc, &when);
	
	// no output if the needle is outside the 'safe' zone of the record
    if (timecode != -1 && timecode > timecoder_get_safe(&tc))
        return;
	
	pitch = timecoder_get_pitch(&tc);
	
	
	if (timecode == -1) {
		pos = -1;
    } else {
        tcpos = (double)timecode / timecoder_get_resolution(&tc);
        pos = tcpos + pitch * when;
    }
	
	outlet_int(x->outtc, timecode);
	outlet_float(x->outpos, pos);
	outlet_float(x->outpitch, pitch);


}


/* detect when dsp is on/off. turn on/off clock to output float result resp. */

void myObj_dspstate(t_myObj *x, long n) {
	if(n)
		myObj_bang(x);		//start clock
	else
		myObj_stop(x);			//stop clock
}


//64-bit dsp method
void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate,
				 long maxvectorsize, long flags) {
	object_method(dsp64, gensym("dsp_add64"), x, myObj_perform64, 0, NULL);
	
	x->sr = samplerate;
	x->numFrames = maxvectorsize;
	if(x->sr<=0) x->sr = 44100.0;

}



// 64 bit signal input version
void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					   double **outs, long numouts, long sampleframes, long flags, void *userparam){
	
	double input, sum = 0;
	long vs = sampleframes;
	int i, n, count = x->count;
	short  *pcm = x->pcm;
	
	if (x->x_obj.z_disabled)
		return;

	
	for(n = 0; n < vs; n++) {
		for(i = 0; i < TIMECODER_CHANNELS; i++) {
			input = ins[i][n];
			sum += fabs(input);
			pcm[(n+count) * TIMECODER_CHANNELS + i ] = (int)(SCALE*input);
		}
	}
    /*
	count += sampleframes;
	if(count>=MAXBLOCK) {
		count = 0;
		if(sum<0.5)
			return;
		timecoder_submit(&x->tc, pcm, MAXBLOCK);
		defer_low(x, (method)myObj_output_values, NULL, 0, NULL);
	}
	x->count = count;
     */

    if(sum<0.5)
        return;
    timecoder_submit(&x->tc, pcm, sampleframes);
    
    
    defer_low(x, (method)myObj_output_values, NULL, 0, NULL);
	
}



void myObj_free(t_myObj *x)
{
	dsp_free((t_pxobject *)x);
    timecoder_free_lookup2(x->tc.def);
    timecoder_clear(&x->tc);
    
	if(x->pcm)
		sysmem_freeptr(x->pcm);
	if(x->m_clock)
		freeobject(x->m_clock);
}



void *myObj_new(t_symbol* vt,  long phonoline)
{
	t_myObj *x = object_alloc(myObj_class);
	struct timecode_def *timecodeDef;
	bool phono;
	
	if(x) {
		dsp_setup((t_pxobject*)x, 2);			// two signal inlets
		
		x->outtc = intout(x);
		x->outpos = floatout(x);
		x->outpitch = floatout(x);
		
		x->sr = sys_getsr();
		if(x->sr <= 0)
			x->sr = 44100.;
		
        phono = phonoline != 0;
		
		x->count = 0;
		x->numFrames = 0;


		if(vt == gensym("")) {
			vt = gensym("serato_2a");
			post("no vinyltype provided. setting to serato_2a");
		}
		
        /* Set the timecode definition to use */
        timecodeDef = timecoder_find_definition(vt->s_name);
        if(timecodeDef == NULL) {
            object_error((t_object*)x, "bad timecode definition...");
            object_free(x);
            return NULL;
        }
        
        
		double speed = 1.0;	// 33.3 RPM
		timecoder_init(&x->tc, timecodeDef, speed, x->sr, phono);
		
		// alloc mem
		x->pcm = (short*)sysmem_newptrclear(8192*sizeof(short));
		// clock
		x->m_clock = clock_new(x, (method)myObj_tick);
		
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
			case 0: sprintf (s,"(signal) left audio in"); break;
			case 1: sprintf (s,"(signal) right audio in"); break;
		}
	}
	else {
		switch(a) {
			case 0: sprintf (s, "(float) pitch"); break;
			case 1: sprintf(s, "(float) position"); break;
			case 2: sprintf(s, "(int) timecode"); break;
		}
		
	}
}
