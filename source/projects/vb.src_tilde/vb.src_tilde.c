#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"
#include "ext_common.h"
#include "ext_buffer.h"

#include "samplerate.h"
#include <Accelerate/accelerate.h>


/*
	a varispeed object, based on SRC by Erik de Castro lopo
 
    callback version
*/


#define ARRAY_LEN(x)	((int) (sizeof (x) / sizeof ((x) [0])))

#define	BUFFER_LEN			4096
#define INPUT_BLOCK_LEN     32




typedef struct
{
    t_buffer_ref *bufref;
    t_symbol    *bufname;
    long        position;
    long        frames, nchnls;
    double      b_srms;
    float		*buffer;
    short       loop, dir;      // loop & playback direction
    double      b_sr;           // original sample rate of audio data
    
    long        loop_start, loop_end;
    
} SNDFILE_CB_DATA ;


typedef struct
{
    SNDFILE_CB_DATA snd;
    SRC_STATE   *src_state;     // pointer
    SRC_STATE   *src_state1;    // mono buffer
    SRC_STATE	*src_state2;    // stereo buffer
    
} SRC_CB_DATA ;



typedef struct {
	t_pxobject      x_obj;
	double	        sr;
    
    SRC_CB_DATA     data;
    float           *output;
    short           stop;
    short           outchns;
    int             sigvs;
    double          lastoutL, lastoutR;
} t_myObj;


static t_class *myObj_class;

static long src_input_callback (void *cb_data, float **data) ;

void myObj_int(t_myObj*, long);
void myObj_float(t_myObj*, double);
void myObj_ft1(t_myObj *x, double);
void myObj_ft2(t_myObj *x, double);
void myObj_bang(t_myObj*);
void myObj_stop(t_myObj*);
void myObj_loop(t_myObj*, long);

// DSP methods
void myObj_dsp(t_myObj *x, t_signal **sp, short *count);
t_int *myObj_perform(t_int *w);

void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate, 
				 long maxvectorsize, long flags);
void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					 double **outs, long numouts, long sampleframes, long flags, void *userparam);

void myObj_set(t_myObj *x, t_symbol *s);
void getBufferInfo(t_myObj *x);

void *myObj_new(t_symbol *s, long ac, t_atom *av);
void myObj_dblclick(t_myObj *x);
t_max_err myObj_notify(t_myObj *x, t_symbol *s, t_symbol *msg, void *sender, void *data);
void myObj_assist(t_myObj *x, void *b, long m, long a, char *s);
void myObj_free(t_myObj *x);

t_symbol *ps_buffer_modified;




void ext_main(void *r) {
	t_class *c;

	c = class_new("vb.src~", (method)myObj_new, (method)myObj_free, (short)sizeof(t_myObj), 0L, A_GIMME, 0L);

	class_addmethod(c, (method)myObj_dsp64,     "dsp64", A_CANT, 0);
    class_addmethod(c, (method)myObj_bang,      "bang", 0);
    class_addmethod(c, (method)myObj_stop,      "stop", 0);
	class_addmethod(c, (method)myObj_float,     "float", A_FLOAT, 0);
    class_addmethod(c, (method)myObj_int,       "int", A_LONG, 0);
    class_addmethod(c, (method)myObj_ft1,       "ft1", A_FLOAT, 0);
    class_addmethod(c, (method)myObj_ft2,       "ft2", A_FLOAT, 0);
    class_addmethod(c, (method)myObj_set,       "set", A_SYM, 0);
	class_addmethod(c, (method)myObj_assist,    "assist", A_CANT,0);
    class_addmethod(c, (method)myObj_dblclick,  "dblclick", A_CANT, 0);
    class_addmethod(c, (method)myObj_notify,    "notify", A_CANT, 0);

	class_dspinit(c);
	class_register(CLASS_BOX, c);
	myObj_class = c;
	
	ps_buffer_modified = gensym("buffer_modified");
    
	// attributes
    CLASS_ATTR_CHAR(c,"loop", 0, t_myObj, data.snd.loop);
    CLASS_ATTR_SAVE(c, "loop", 0);
    CLASS_ATTR_STYLE_LABEL(c,"loop", 0, "onoff", "loop on/off");
	
	post("vb.src~ 1.0.1, by Volker Böhm, using libsamplerate by Erik de Castro Lopo");
	
}



void myObj_int(t_myObj *x, long input) {
    myObj_float(x, (double)input);
}


void myObj_float(t_myObj *x, double input)
{
    src_reset(x->data.src_state);
    input = input * x->data.snd.b_srms;
    CLIP_ASSIGN(input, 0., x->data.snd.frames-1);
    x->data.snd.position = (long)input;
    x->stop = 0;
}


void myObj_stop(t_myObj *x)
{
    x->stop = 1;
}


void myObj_bang(t_myObj *x)
{
    if (x->data.snd.dir) myObj_float(x, 0.0);       // jump to beginning
    else {                                          // jump to end
        // we need to provide it in ms...
        double end = (double)x->data.snd.frames / (double)x->data.snd.b_srms;
        myObj_float(x, end);
    }
}

// set start loop point
void myObj_ft1(t_myObj *x, double input)
{
    long start_pos = input * x->data.snd.b_srms + 0.5;
    CLIP_ASSIGN(start_pos, 0, x->data.snd.frames);
    x->data.snd.loop_start = start_pos;
}

// set end loop point
void myObj_ft2(t_myObj *x, double input)
{
    long end_pos = input * x->data.snd.b_srms + 0.5;
    CLIP_ASSIGN(end_pos, 0, x->data.snd.frames);
    x->data.snd.loop_end = end_pos;
}


void myObj_set(t_myObj *x, t_symbol *s)
{
    if (!x->data.snd.bufref)
        x->data.snd.bufref = buffer_ref_new((t_object*)x, s);
    else
        buffer_ref_set(x->data.snd.bufref, s);

    x->data.snd.bufname = s;
    
    
    getBufferInfo(x);

}

void getBufferInfo(t_myObj *x)
{

    t_buffer_obj *b = buffer_ref_getobject(x->data.snd.bufref);
    if(!b) {
        if(x->data.snd.bufname == NULL)
            object_error((t_object *)x,"no buffer reference provided!");
        else
            object_error((t_object *)x,
                     "buffer~ %s - not valid!",
                     x->data.snd.bufname->s_name);
        x->stop = 1;
        return;
    }
    long frames = buffer_getframecount(b);
    long chns = buffer_getchannelcount(b);
    
    switch(chns) {      // choose converter by #channels
        case 1: x->data.src_state = x->data.src_state1; break;
        case 2: x->data.src_state = x->data.src_state2; break;
        default:
            x->stop = 1;
            object_error((t_object *)x,
                              "buffer~ %s - more than 2 channels not supported!",
                              x->data.snd.bufname->s_name);
            return;
    }
    
    if((frames != x->data.snd.frames) || (chns != x->data.snd.nchnls)) {
        src_reset(x->data.src_state);
        x->data.snd.position = 0;   // reset playback position
    }
    x->data.snd.nchnls = chns;
    x->data.snd.frames = frames;
    x->data.snd.b_srms = buffer_getmillisamplerate(b);
    x->data.snd.b_sr = buffer_getsamplerate(b);
    
    x->data.snd.loop_start = 0;
    x->data.snd.loop_end = frames;
    
}



//64-bit dsp method
void myObj_dsp64(t_myObj *x, t_object *dsp64, short *count, double samplerate,
				 long maxvectorsize, long flags) {
    
    if(maxvectorsize<16) {
        object_error((t_object *)x, "sigvs must be 16 or higher!");
    }
    else {
        if( maxvectorsize != x->sigvs){
            x->sigvs = (int)maxvectorsize;
            // make it 8x longer, just to be sure...
            x->output = (float *)sysmem_resizeptr(x->output, 8 * x->sigvs * sizeof(float));
        }
        object_method(dsp64, gensym("dsp_add64"), x, myObj_perform64, 0, NULL);
    }
    
	x->sr = samplerate;
	if(x->sr<=0) x->sr = 44100.0;
    
    myObj_set(x, x->data.snd.bufname);

}

// 64 bit signal input version
void myObj_perform64(t_myObj *x, t_object *dsp64, double **ins, long numins, 
					   double **outs, long numouts, long sampleframes, long flags, void *userparam) {
	
	t_double    *in = ins[0];
	t_double    *out = outs[0];
    t_double    *out2, *outpos;
    
    if(x->outchns > 1) {
        out2 = outs[1];
        outpos = outs[2];
    } else {
        outpos = outs[1];
    }
    
	long        vs = sampleframes;
    long        nchnls, rc = 0;         // hold the number of returned frames
    int         offset;         // sample offset when copying to output buffers
    float       *output  = x->output;
    short       outchns = x->outchns;
    double      src_ratio, speed;
    double      input = in[0];
    SRC_CB_DATA data = x->data;

	
	if (x->x_obj.z_disabled)
		return;
    
    // determine play direction
    x->data.snd.dir = (input >= 0);
    
    if(x->stop)
        goto zero;
    
    ////// need this here, to determine if buffer data is accessible!
    ////// it might not be! (e.g. if loading new soundfile, or buffer not valid)
    ////// this makes the converter freak out...
    t_buffer_obj    *b = buffer_ref_getobject(x->data.snd.bufref);
    if(!b) goto zero;
    float *tab = buffer_locksamples(b);
    if (!tab) goto zero;
    buffer_unlocksamples(b);
    //////
    
    
    nchnls = x->data.snd.nchnls;
    offset = (int)nchnls - 1;
    
    // clip playback speed to reasonable values
    speed = fabs(input);
    if(speed < 0.01) {          // clip and output DC
        vDSP_vfillD(&x->lastoutL, out, 1, sampleframes);
        if(outchns > 1)
            vDSP_vfillD(&x->lastoutR, out2, 1, sampleframes);
        return;
    } else if(speed > 10.0) speed = 10.0;
    
    
    src_ratio = x->sr / (data.snd.b_sr * speed);
    
    
    rc = src_callback_read(x->data.src_state, src_ratio, sampleframes, output);
    
    if(rc < 0 ) {
        object_error((t_object *)x, "callback return -1");
        goto zero;
    }
    else if (rc < sampleframes) {           // end of buffer reached
        //post("%ld: not enough frames returned...", rc);

        vDSP_vspdp(output, nchnls, out, 1, rc);                 // copy samples
        vDSP_vclrD(out+rc, 1, (vs-rc));               // zero out rest of vector
        
        if(outchns > 1) {
            vDSP_vspdp(output+offset, nchnls, out2, 1, rc);
            vDSP_vclrD(out2+rc, 1, (vs-rc));
        }
        x->stop = 1;
    }
    else {
        vDSP_vspdp(output, nchnls, out, 1, vs);
        
        if(outchns > 1)             // if 2outlets
            vDSP_vspdp(output+offset, nchnls, out2, 1, vs);
        else if(nchnls > 1) {       // if 1outlet and more than mono buffer
            int i;
            //for(c=1; c<nchnls; c++) {       // TODO: DOESN'T WORK YET!!!!!!!
                for(i=0; i<vs; i++)
                    out[i] += output[(i*nchnls)+offset];        // TODO: vectorize
            //}
            double monoScale = 0.707;
            vDSP_vsmulD(out, 1, &monoScale, out, 1, vs);
            //for(i=0; i<vs; i++)
                //out[i] += output[(i*nchnls)+offset];     //
        }
            
    }
    double position = (double)x->data.snd.position / (double)x->data.snd.frames;
//    int i;
//    for(i=0; i<vs; i++)
//        outpos[i] = position;
    vDSP_vfillD(&position, outpos, 1, vs);
    
    x->lastoutL = out[vs-1];
    if(outchns>1)
        x->lastoutR = out2[vs-1];
	return;
	
zero:
    vDSP_vclrD(out, 1, vs);
    if(outchns>1) vDSP_vclrD(out2, 1, vs);
    vDSP_vclrD(outpos, 1, vs);
    x->lastoutL = 0.;
    x->lastoutR = 0.;
}




static long
src_input_callback (void *cb_data, float **audio)
{
    SNDFILE_CB_DATA *snddata = (SNDFILE_CB_DATA *)cb_data ;

    long    frames, nc, input_frames;
    long    loop_start, loop_end;
    float   *tab;
    t_buffer_obj	*b = buffer_ref_getobject(snddata->bufref);
    
    tab = buffer_locksamples(b);
    if (!tab) {
        object_error(NULL, "can't acces buffer now...");
        return 0;       // TODO: return 0, good idea?
    }
    
    snddata->nchnls = nc = buffer_getchannelcount(b);
    snddata->frames = frames = buffer_getframecount(b);
    snddata->b_srms = buffer_getmillisamplerate(b);
    snddata->b_sr = buffer_getsamplerate(b);
    
    
    // check loop points
    if ( snddata->loop_start > 0)
        loop_start = snddata->loop_start;
    else
        loop_start = 0;
    
    if ( snddata->loop_end < frames )
        loop_end = snddata->loop_end;
    else
        loop_end = frames;
    
    
    if(snddata->dir)        // ..............play forward
    {
        input_frames = MIN(INPUT_BLOCK_LEN, (loop_end - snddata->position));
        
        if(input_frames <= 0) {
            if(snddata->loop) {
                snddata->position = loop_start; //0;
                input_frames = INPUT_BLOCK_LEN;
            } else
                input_frames = 0;
        }
        
        
        int i;
        for(i=0; i<input_frames*nc; i++)
            snddata->buffer[i] = tab[(snddata->position*nc)+i];
        
        // update reading position in buffer
        snddata->position += input_frames;
    }
    
    else                   // ..............play backward
    {
        input_frames = MIN(INPUT_BLOCK_LEN, snddata->position - loop_start);  // ??
        if(input_frames <= 0) {
            if (snddata->loop) {
                snddata->position = loop_end -1; //snddata->frames-1;
                input_frames = INPUT_BLOCK_LEN;
            } else
                input_frames = 0;
        }

        int i;
        for(i=0; i<input_frames*nc; i++)
            snddata->buffer[i] = tab[(snddata->position*nc)-i];     // reversing stuff...
        
        
        // update reading position in buffer
        snddata->position -= input_frames;
    }
    
    buffer_unlocksamples(b);
    
    *audio = &(snddata->buffer[0]);
    
    return input_frames;       // number of frames read from buffer
}



void myObj_dblclick(t_myObj *x) {
    t_buffer_obj *b = buffer_ref_getobject(x->data.snd.bufref);
    buffer_view(b);
}



void myObj_free(t_myObj *x)
{
    dsp_free((t_pxobject *)x);
    
    if(x->data.snd.bufref)
        object_free(x->data.snd.bufref);
    if(x->data.snd.buffer)
        sysmem_freeptr(x->data.snd.buffer);
    if(x->data.src_state1)
        src_delete(x->data.src_state1);
    if(x->data.src_state2)
        src_delete(x->data.src_state2);
    if(x->output)
        sysmem_freeptr(x->output);
}



void *myObj_new(t_symbol *s, long ac, t_atom *av)
{
	t_myObj     *x = object_alloc(myObj_class);
    
    long        attrstart = attr_args_offset(ac, av);
    t_symbol    *buf_name = NULL;
    long        outchns = 1;

    
	if(x) {
        floatin(x, 2);
        floatin(x, 1);
        dsp_setup((t_pxobject*)x, 1);			// one signal inlet
        
        // check arguments
        if(attrstart < 1)
            object_error((t_object *)x, "no buffer name provided!");
        else {
            if (atom_gettype(av) == A_SYM)
                buf_name = atom_getsym(av);
            else
                object_error((t_object *)x, "no valid buffer name provided!");
            
            if (attrstart > 1) {
                if (atom_gettype(av+1) == A_LONG) {
                    outchns = atom_getlong(av+1);
                    if (outchns > 2 || outchns < 1)
                        object_warn((t_object *)x, "number of channels out of range! should be 1 or 2");
                }
                else
                    object_error((t_object *)x, "can't process number of channels - should be an int!");
            }
        }
        
		outlet_new(x, "signal");
        if(outchns > 1) {
            outlet_new(x, "signal");
            x->outchns = 2;
        } else x->outchns = 1;
        outlet_new(x, "signal");        // position outlet
		
		x->sr = sys_getsr();
		if(x->sr <= 0)
			x->sr = 44100.;
        
        x->sigvs = sys_getblksize();
        
        int		error ;
        int     converter = SRC_SINC_FASTEST;
        int     channels = 1;       // mono
        
        memset(&x->data, 0, sizeof (x->data)) ;
        
        x->data.snd.buffer = (float *)sysmem_newptrclear(BUFFER_LEN * sizeof(float));
        
        // Initialize the sample rate converter.
        if ((x->data.src_state1 =
             src_callback_new (src_input_callback, converter, channels, &error, &x->data.snd)) == NULL)
        {
            object_error((t_object *)x, "src_new() failed : %s.", src_strerror (error)) ;
            //exit (1) ;
        } ;
        
        // have a second one for stereo buffers
        channels = 2;
        if ((x->data.src_state2 =
             src_callback_new (src_input_callback, converter, channels, &error, &x->data.snd)) == NULL)
        {
            object_error((t_object *)x, "src_new() failed : %s.", src_strerror (error)) ;
        } ;
        
        // output buffer -- make it 8x longer, just to be sure...
        x->output = (float *)sysmem_newptrclear(8 * x->sigvs * sizeof(float));
        

        if (!x->data.snd.bufref)
            x->data.snd.bufref = buffer_ref_new((t_object*)x, buf_name);
        else
            buffer_ref_set(x->data.snd.bufref, buf_name);
        x->data.snd.bufname = buf_name;

        
        x->data.snd.loop = 0;
        x->stop = 0;
        x->lastoutL = x->lastoutR = 0.;
		
		attr_args_process(x, ac, av);			// process attributes
	}
	
	else {
		object_free(x);
		x = NULL;
	}
    
	return x;
}


t_max_err myObj_notify(t_myObj *x, t_symbol *s, t_symbol *msg, void *sender, void *data) {

    if(msg == ps_buffer_modified)
        getBufferInfo(x);

    return buffer_ref_notify(x->data.snd.bufref, s, msg, sender, data);
}



void myObj_assist(t_myObj *x, void *b, long m, long a, char *s) {
	if (m == ASSIST_INLET) {
		switch(a) {
			case 0: sprintf (s,"(signal) playback speed, (float) playback position"); break;
            case 1: sprintf (s, "(float) loop start point in ms"); break;
            case 2: sprintf (s, "(float) loop end point in ms"); break;
		}
	}
	else {
        if(x->outchns > 1) {
            switch(a) {
                case 0: sprintf (s, "(signal) chn1 audio out"); break;
                case 1: sprintf (s, "(signal) chn2 audio out"); break;
                case 2: sprintf (s, "(signal) position out"); break;
            }
        } else {
            switch(a) {
                case 0: sprintf (s, "(signal) chn1 audio out"); break;
                case 1: sprintf (s, "(signal) position out"); break;
            }
        }
		
	}
}
