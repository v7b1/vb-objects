/**
 
 */

#include "ext.h"
#include "ext_obex.h"
#include "ext_common.h"
#include "ext_buffer.h"
#include <fftw3.h>

#define VERSION "1.1.2"

// -----> change: don't scale bin 0 differently than all the others, to conform with what all the other algorithms do...

// update, vb, 20.09.2016
// --- drop max5 support and update to new buffer~handling methods
// --- drop i386 support
// --- introduce deferlow for safety


/****************************************************
 calculate an fft/ifft on a selected audio buffer
 put magnitudes in first half and phases in second half of the buffer
 using FFTW  --  one-dimensional real DFT
 by volker böhm, januar 2008
 ****************************************************/

/* 
	revision, vb, feb.2015
	changed to fftw float version, as buffer~ data will always be float32 
 */




static t_class *myObj_class;

typedef struct _myObj {	// defines our object's internal variables for each instance in a patch
	t_object			b_ob;
	t_buffer_ref		*bufref;
	t_symbol			*bufname;		// buffer name
	short				scale;
	void				*outB;
	void				*outA;		// output size of FFT
	void				*outmess;
	int					magphas;	// flag: calc magphas or output only realimag?
	unsigned int		n;			// FFT size, size of allocated memory for FFT
	fftwf_complex	    *spec;
	fftwf_plan			pIN, pOUT;
	float				*in, *out;
	
	char				doCentroid;
	char				doRMS;
} t_myObj;


void myObj_fft(t_myObj *x);
void myObj_ifft(t_myObj *x);
void do_fft(t_myObj *x);
void do_ifft(t_myObj *x);
float calcCentroid(fftwf_complex *spec, long n2, float sr);
void myObj_setBuf(t_myObj *x, t_symbol *s);
void myObj_normalize(t_myObj *x, int n);
void myObj_magphase(t_myObj *x, int n);
int allocMem(t_myObj *x, unsigned int n);
void myObj_dblclick(t_myObj *x);
t_max_err myObj_notify(t_myObj *x, t_symbol *s, t_symbol *msg, void *sender, void *data);
//void *myObj_new(t_symbol *s);
void *myObj_new( t_symbol *s, long argc, t_atom *argv);
void myObj_assist(t_myObj *x, void *b, long m, long a, char *s);
void myObj_free(t_myObj *x);

t_symbol *sym_centroid;
t_symbol *sym_rms;

//--------------------------------------------------------------------------

void ext_main(void *r)
{
	t_class *c;

	c = class_new("vb.FFTWbuf~", (method)myObj_new, (method)myObj_free, sizeof(t_myObj), 0L, A_GIMME, 0);
	
	class_addmethod(c, (method)myObj_fft, "fft", 0L);
	class_addmethod(c, (method)myObj_ifft, "ifft", 0L);
	class_addmethod(c, (method)myObj_setBuf, "set", A_SYM, 0L);
	class_addmethod(c, (method)myObj_normalize, "normalize", A_LONG, 0);
	class_addmethod(c, (method)myObj_magphase, "magphas", A_LONG, 0);
	class_addmethod(c, (method)myObj_dblclick, "dblclick", A_CANT, 0);
	class_addmethod(c, (method)myObj_notify, "notify", A_CANT, 0);
	class_addmethod(c, (method)myObj_assist,"assist", A_CANT,0);
	
	sym_centroid = gensym("centroid");
    sym_rms = gensym("rms");
	
	class_register(CLASS_BOX, c);
	myObj_class = c;
	
	
	CLASS_ATTR_CHAR(c,"centroid", 0, t_myObj, doCentroid);
	CLASS_ATTR_SAVE(c, "centroid", 0);
	CLASS_ATTR_STYLE_LABEL(c, "centroid", 0, "onoff", "calculate centroid");
	
	CLASS_ATTR_CHAR(c,"RMS", 0, t_myObj, doRMS);
	CLASS_ATTR_SAVE(c, "RMS", 0);
	CLASS_ATTR_STYLE_LABEL(c, "RMS", 0, "onoff", "calculate RMS");
	
	
	post("*** vb.FFTWbuf~ by volker böhm, version %s", VERSION);

}


void myObj_setBuf(t_myObj *x, t_symbol *s)
{
	if (!x->bufref)
		x->bufref = buffer_ref_new((t_object*)x, s);
	else
		buffer_ref_set(x->bufref, s);
	
	x->bufname = s;
}



int allocMem(t_myObj *x, unsigned int n)		// n: size of buffer
{
	//post("enter allocMem");
	
	if( n != x->n ) {		// if size of input buffer changed
		
		//----------- allocate memory and create plan
		if(x->in) fftwf_free(x->in);
		x->in = (float *) fftwf_malloc(sizeof(float) * n);
		if(x->in==NULL) {
			object_error((t_object *)x,"error allocating memory!");
			return 0;
		}
		if(x->spec) fftwf_free(x->spec);
		x->spec = (fftwf_complex *) fftwf_malloc(sizeof(fftwf_complex) * (n/2+1));
		if(x->spec==NULL) {
			object_error((t_object *)x,"error allocating memory!");
			return 0;
		}
		if(x->out) fftwf_free(x->out);
		x->out = (float *) fftwf_malloc(sizeof(float) * n);
		if(x->out==NULL) {
			object_error((t_object *)x,"error allocating memory!");
			return 0;
		}
		
		//if(x->pIN) fftwf_destroy_plan(x->pIN);
		x->pIN = fftwf_plan_dft_r2c_1d( n, x->in, x->spec, FFTW_ESTIMATE);
		//if(x->pOUT) fftwf_destroy_plan(x->pOUT);
		x->pOUT = fftwf_plan_dft_c2r_1d( n, x->spec, x->out, FFTW_ESTIMATE);
		
		x->n = n;
		//post("memory allocated");
	}
	
	return 1;
}




void myObj_normalize(t_myObj *x, int n) {
    x->scale = (n != 0);
}


void myObj_magphase(t_myObj *x, int n) {
    x->magphas = (n != 0) ;
}


// TODO: think about using a different thread

void myObj_fft(t_myObj *x) {
	defer_low(x, (method)do_fft, NULL, 0, NULL);
}

void myObj_ifft(t_myObj *x) {
	defer_low(x, (method)do_ifft, NULL, 0, NULL);
}


void do_fft(t_myObj *x)
{
	float			*tab;
	long			frames, nchnls, i, k;
	t_buffer_obj	*b = NULL;
	float			scale, mag, phas, sr;
	unsigned int	n, n2;
	float			*in;
	fftwf_complex	*spec;
	
	b = buffer_ref_getobject(x->bufref);
	if(!b) {
		object_error((t_object *)x,"%s is no valid buffer", x->bufname->s_name);
		return ;
	}
	tab = buffer_locksamples(b);				// access samples in buffer
	if (!tab) {
		object_error((t_object *)x, "can't access buffer %s", x->bufname->s_name);
		return;
	}
	
	frames = buffer_getframecount(b);		// access samples in buffer....
	nchnls = buffer_getchannelcount(b);	// buffer size in samples
	sr = buffer_getsamplerate(b);
	
	if( !allocMem(x, frames) ) {
		buffer_unlocksamples(b);
		return;
	}
	
	/* the input is n real numbers, while the output is n/2+1 complex numbers -- we skip the n/2+1 output (nyquist) */
	
	n = x->n;		
	n2 = n/2;
	scale = 1.0/n2;	//n;			
	spec = x->spec;
	in = x->in;
	
	
	for(k=0; k<nchnls; k++) {
		
		// copy first channel of buffer to input array
		for(i=0; i<n; i++) {
			in[i] = tab[i*nchnls+k];							
		}
		
		if(x->doRMS) {
			float rms = 0;
			float sum = 0;
			for(i=0; i<n; i++)
				sum+= (in[i]*in[i]);
			rms = sqrtf(sum/n);
            t_atom a;
            atom_setfloat(&a, rms);
            outlet_anything(x->outmess, sym_rms, 1, &a);
		}
		
		fftwf_execute(x->pIN);
		
		//------- calculate magnitude and phase -------//
		if(x->magphas) {
			//tab[0+k] = (float)(sqrt( spec[0][0] * spec[0][0]) / n);		// DC component scaled differently
			//tab[(n2)*nchnls+k] = atan2(spec[0][1], spec[0][0]);		// phase could be 0 or π
			
			// calc magnitude and phase and store it in place of real and imag parts
			for(i=0; i<n2; i++) {
				mag = sqrtf( spec[i][0] * spec[i][0] + spec[i][1] * spec[i][1] ) ;
				phas = atan2f(spec[i][1], spec[i][0]);
				spec[i][0] = mag;
				spec[i][1] = phas;
			} 
		}

		
		// output real and imag
		if(x->scale) {
			for(i=0; i<n2; i++) {
				tab[i*nchnls+k] = (spec[i][0] * scale);
                if (!x->magphas)
                    tab[(i+n2)*nchnls+k] = (spec[i][1] * scale);
                else
                    tab[(i+n2)*nchnls+k] = (spec[i][1]);
			} 
		}
		else {
			for(i=0; i<n2; i++) {
				tab[i*nchnls+k] = (spec[i][0]);
				tab[(i+n2)*nchnls+k] = (spec[i][1]);	
			} 
		}
	}
    
    if(x->doCentroid) {
        double centroid = 0;
        t_atom cent;
        
        if (!x->magphas) {
            spec[i][0] = sqrtf( spec[i][0] * spec[i][0] + spec[i][1] * spec[i][1] );
        }
        centroid = calcCentroid(spec, n2, sr);
        atom_setfloat(&cent, centroid);
        outlet_anything(x->outmess, sym_centroid, 1, &cent);
    }
	
	buffer_setdirty( (t_object *)b);
	
	buffer_unlocksamples(b);
	
	
	// bang when finished processing
	outlet_bang(x->outB);
	outlet_int(x->outA, n);
}






/********** INVERSE FFT **********/
void do_ifft(t_myObj *x)
{
	float				*tab;
	long					frames, nchnls, i, k;
	t_buffer_obj	*b = NULL;
	float				scale;
	float				magnitude, phase;
	unsigned int	n, n2;
	
	float				*out;
	fftwf_complex *spec;
	
	
	b = buffer_ref_getobject(x->bufref);
	if(!b) {
		object_error((t_object *)x,"%s is no valid buffer", x->bufname->s_name);
		return ;
	}
	tab = buffer_locksamples(b);				// access samples in buffer
	if (!tab) {
		object_error((t_object *)x, "can't access buffer %s", x->bufname->s_name);
		return;
	}
	
	frames = buffer_getframecount(b);		// access samples in buffer....
	nchnls = buffer_getchannelcount(b);		// buffer size in samples
	
	if( !allocMem(x, frames)){
		buffer_unlocksamples(b);
		return;
	}
	
	n = frames;
	n2 = n/2;
	spec = x->spec;
	out = x->out;
	
	for(k=0; k<nchnls; k++) {
		
		if(x->magphas) {
			//------- polar to cartesian conversion -------//
			for(i=0; i<n2; i++) {
				magnitude = tab[i*nchnls+k];
				phase = tab[(i+n2)*nchnls+k];
				spec[i][0] = magnitude * cosf(phase);		// copy into real part
				spec[i][1] = magnitude * sinf(phase);			// copy into imag part
			}
		}
		else {
			for(i=0; i<n2; i++) {
				spec[i][0] = tab[i*nchnls+k];					// copy into real part
				spec[i][1] = tab[(i+n2)*nchnls+k];			// copy into imag part
			}
		}
		
		// DC component must be treated differently
		//spec[0][0] *= 2;
		//spec[0][1] *= 2;
		// set nyquist to zero;
		spec[n2][0] = 0;
		spec[n2][1] = 0;
		
		fftwf_execute(x->pOUT);
		
		
		if(x->scale)
			scale = 0.5f;
		else
			scale = 1.0f/n;
		
		
		//-- copy to buffer and scale (watch out for number of channels) --//
		for(i=0; i<n; i++) 
			tab[i*nchnls+k] = out[i] * scale;
	}
	
	buffer_setdirty( (t_object *)b);
	
	buffer_unlocksamples(b);
	
	// bang when finished processing
	outlet_bang(x->outB);
	outlet_int(x->outA, n);
}


float calcCentroid(fftwf_complex *spec, long n2, float sr) {
	int i;
	float sum1, sum2, basefreq;
	
	basefreq = sr / (n2*2);
	sum1 = sum2 = 0;
	
	for(i=0; i<n2; i++) {
		sum1 += (i*spec[i][0]);
		sum2 += spec[i][0];
	}
	if(sum2 > 0.0f)
		return basefreq * sum1 / sum2;
	else
		return 0.0f;
}


//--------------------------------------------------------------------------
#pragma mark utility functions -----------------------

void myObj_dblclick(t_myObj *x)
{
	buffer_view(buffer_ref_getobject(x->bufref));
}


t_max_err myObj_notify(t_myObj *x, t_symbol *s, t_symbol *msg, void *sender, void *data)
{
	return buffer_ref_notify(x->bufref, s, msg, sender, data);
}


void myObj_assist(t_myObj *x, void *b, long m, long a, char *s) // 4 final arguments are always the same for the assistance method
{
	if (m==ASSIST_INLET)
		sprintf(s, "message: fft/ifft");
	else {
		switch(a) {
			case 0:
				sprintf(s, "(int) FFT size"); break;
			case 1:
				sprintf(s, "bang when done processing"); break;
            case 2:
                sprintf(s, "info out"); break;
		}
	}
}


//--------------------------------------------------------------------------
#pragma mark new -----------------------

void *myObj_new( t_symbol *s, long argc, t_atom *argv)
{
	t_myObj *x;	
	
	x = (t_myObj *)object_alloc(myObj_class); 
	
    x->outmess = outlet_new(x, NULL);
	x->outB = bangout(x);		// create an outlet for bangs (right)
	x->outA = intout(x);			// outlet for FFT size (left)
	

	x->bufname = gensym("");
	
	if(argc>=1) {
		if(atom_gettype(argv) == A_SYM)
			myObj_setBuf(x, atom_getsym(argv));
		else
			object_warn((t_object *)x, "first argument should be buffer~ name to reference");
	}
	else
		object_warn((t_object *)x, "missing buffer name argument!");
	
	x->scale = 1;
	x->magphas = 1;		// output mag and phase by default
	x->n = 0;						// size of allocate memory for FFT
	
    attr_args_process(x, argc, argv);            // process attributes
    
	return x;
}


void myObj_free(t_myObj *x)
{
	if(x->in) fftwf_free(x->in);
	if(x->out) fftwf_free(x->out);
	if(x->spec) fftwf_free(x->spec);
	if(x->pIN) fftwf_destroy_plan(x->pIN);
	if(x->pOUT) fftwf_destroy_plan(x->pOUT);
	if(x->bufref) object_free(x->bufref);
}
