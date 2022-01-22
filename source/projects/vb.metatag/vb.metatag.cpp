
 
 //	based on TagLib 1.11.1, https://taglib.org/


//		64bit version + additional features by volker böhm https://vboehm.net/
//		Oktober 2016
//      some update, dez.2018 - still work in progress...


#include <stdio.h>
#include <string.h>
#include "ext.h"
#include "ext_obex.h"
#include "ext_drag.h"
#include "ext_common.h"
#include "ext_strings.h"
#include "ext_dictobj.h"

#include <tag/tag.h>
#include <tag/fileref.h>
#include <tag/mpegfile.h>
#include <tag/id3v2tag.h>
#include <tag/attachedpictureframe.h>
#include <tag/mp4file.h>
#include <tag/mp4tag.h>
#include <tag/mp4coverart.h>
#include <tag/rifffile.h>
//#include <taglib/riff/riffutils.h>
#include <tag/aifffile.h>
#include <tag/wavfile.h>
#include <tag/flacfile.h>
#include <tag/tpropertymap.h>


#include <tag/textidentificationframe.h>
#include <tag/uniquefileidentifierframe.h>



typedef struct _metatag 
{
	t_object            ob;
	TagLib::FileRef		*fileref;
	TagLib::PropertyMap tags;
	t_symbol			*sdummy;
	long				idummy;
	void				*audioPropsOut;
	
	// dictionary stuff
	void                *outlet_dict;
	t_symbol            *name;			// symbol mapped to the dictionary
	t_dictionary        *dictionary;	// the actual dictionary
} t_metatag;


void *metatag_class;

void metatag_anything(t_metatag *x, t_symbol *s, long ac, t_atom *av);
void metatag_assist(t_metatag *x, void *b, long m, long a, char *s);
void metatag_free(t_metatag *x);
void *metatag_new(t_symbol *s, long ac, t_atom *av);
void metatag_open(t_metatag *x, t_symbol *s, long ac, t_atom *av);
void metatag_close(t_metatag *x);
void printProperties(t_metatag *x);
const char* convertString(TagLib::String tstr);
std::string copyString(TagLib::String tstr);

t_max_err metatag_ssetter(t_metatag *x, void *attr, long ac, t_atom *av);
t_max_err metatag_sgetter(t_metatag *x, void *attr, long *ac, t_atom **av);
t_max_err metatag_isetter(t_metatag *x, void *attr, long ac, t_atom *av);
t_max_err metatag_igetter(t_metatag *x, void *attr, long *ac, t_atom **av);

void metatag_get(t_metatag *x, t_symbol *m);
void metatag_set(t_metatag *x, t_symbol *s, long ac, t_atom *av);
void metatag_delete(t_metatag *x, t_symbol *m);
void metatag_audioProperties(t_metatag *x);
void new_metatag_picture_get(t_metatag *x, t_symbol *s, long *ac, t_atom **av);
void readPictFromId3v2(t_metatag *x, TagLib::ID3v2::Tag* tag);
void readPictFromMp4(t_metatag *x, TagLib::MP4::Tag *tag);

void metatag_writeToFile(t_metatag *x);
void metatag_get_all(t_metatag *x);
void exportPictData(t_metatag *x, TagLib::ByteVector pict, t_symbol *outputPath);
void metatag_picture_get(t_metatag *x, t_symbol *s, long *ac, t_atom **av);
void checkForRejectedProperties(const TagLib::PropertyMap &tags);
void printId3Tags(t_metatag *x, TagLib::ID3v2::Tag *tag);
void metatag_setname(t_metatag *x, void *attr, long argc, t_atom *argv);
void metatag_outputDict(t_metatag *x);

long metatag_acceptsdrag(t_metatag *x, t_object *drag, t_object *view);
t_max_err metatag_file_get(t_metatag *x, void *attr, long *ac, t_atom **av);
t_max_err metatag_file_set(t_metatag *x, void *attr, long ac, t_atom *av);

//static t_symbol *ps_title, *ps_artist, *ps_album, *ps_comment, *ps_genre;
t_symbol *ps_title;			// what's wrong with this one?
//static t_symbol *ps_artist, *ps_album, *ps_comment, *ps_genre;
//static t_symbol *ps_year, *ps_track, *ps_albumartist;
//static t_symbol *ps_date, *ps_bpm, *ps_lyrics;
static t_symbol *ps_getname, *ps_read, *ps__none;

static t_symbol *ps_bitrate, *ps_sampleRate, *ps_channels,*ps_length;
static t_symbol *ps_pictpath, *ps_jpgFile, *ps_pngFile;



#pragma mark main function ----------------------------------------

int C74_EXPORT main(void)
{	
	t_class *c;
	common_symbols_init();
	c = class_new("vb.metatag", (method)metatag_new, (method)metatag_free, (short)sizeof(t_metatag),
		(method)0L, A_GIMME, 0);
	
	class_addmethod(c, (method)metatag_open,		"read",		A_GIMME, 0);
	class_addmethod(c, (method)metatag_open,		"open",		A_GIMME, 0);
	class_addmethod(c, (method)metatag_close,		"dispose",	0);
	class_addmethod(c, (method)metatag_close,		"close",	0);
	class_addmethod(c, (method)metatag_get,			"get",	A_SYM, 0);
	class_addmethod(c, (method)metatag_delete,		"delete",	A_SYM, 0);
	class_addmethod(c, (method)metatag_set,			"set",	A_GIMME, 0);
	class_addmethod(c, (method)metatag_audioProperties,	"audioprops",	0);
	class_addmethod(c, (method)metatag_picture_get,	"getpict", A_GIMME, 0);
	class_addmethod(c, (method)printProperties,		"printProps", 0);
	class_addmethod(c, (method)metatag_get_all,		"getall", 0);
	class_addmethod(c, (method)metatag_writeToFile,	"write", 0);
    class_addmethod(c, (method)metatag_outputDict, "bang", 0);

	class_addmethod(c, (method)metatag_assist, 		"assist",	A_CANT, 0);
	class_addmethod(c, (method)object_obex_dumpout,	"dumpout",	A_CANT, 0);
	
	CLASS_ATTR_SYM(c,			"name",			0, t_metatag, name);
	CLASS_ATTR_ACCESSORS(c,		"name",			NULL, metatag_setname);
	CLASS_ATTR_CATEGORY(c,		"name",			0, "Dictionary");
	CLASS_ATTR_LABEL(c,			"name",			0, "Name");
	CLASS_ATTR_BASIC(c,			"name",			0);

	class_addmethod(c, (method)metatag_acceptsdrag, "acceptsdrag_unlocked", A_CANT, 0);
	class_addmethod(c, (method)metatag_acceptsdrag, "acceptsdrag_locked", A_CANT, 0);
	
	class_register(_sym_box, c);
	metatag_class = c;
	
	/*
	ps_title = gensym("TITLE");
	ps_artist = gensym("ARTIST");
	ps_albumartist = gensym("ARTIST");
	ps_album = gensym("ALBUM");
	ps_comment = gensym("COMMENT");
	ps_genre = gensym("GENRE");
	ps_date = gensym("DATE");
	ps_track = gensym("TRACK");
	ps_bpm = gensym("BPM");
	ps_lyrics = gensym("LYRICS");
	
	ps_year = gensym("year");
	*/
	
	ps_getname = gensym("getname");
	ps_read = gensym("read");
	ps__none = gensym("<none>");
	
	ps_bitrate = gensym("bitrate");
	ps_sampleRate = gensym("samplerate");
	ps_channels = gensym("channels");
	ps_length = gensym("length");
	ps_pictpath = gensym("coverart");
	ps_jpgFile = gensym("/tmp/coverart.jpg");
	ps_pngFile = gensym("/tmp/coverart.png");
	
	post("vb.metatag, based on TagLib 1.11.1");
	return 0;
}

long metatag_acceptsdrag(t_metatag *x, t_object *drag, t_object *view)
{
	// accept any file; drag role is useful if we know what is being dragged in 
	// (audio, video, etc.) but we might have an mp3, a flac or some other media file. 
	// we'll let the taglib determine if the file is valid.
	jdrag_object_add(drag, (t_object *)x, ps_read);
	return true;
/*	if (jdrag_matchdragrole(drag, ps_file, 0)) {
		jdrag_object_add(drag, (t_object *)x, ps_read);
		return true;

	}
	return false;
 */
}




#pragma mark new getter/setter -------------------------------------

void metatag_get(t_metatag *x, t_symbol *m) {
	
	t_atom a;
	t_max_err err = dictionary_getatom(x->dictionary, m, &a);
	if(!err)
		outlet_anything(x->audioPropsOut, m, 1, &a);
}


void metatag_set(t_metatag *x, t_symbol *s, long ac, t_atom *av)
{
	TagLib::FileRef f;
	TagLib::PropertyMap map;
	TagLib::String key, value;
	t_symbol		*k, *v;
	t_max_err		err;
	
	if(!x->fileref)
		return;
	
	f = *x->fileref;
	
	if(ac<2) {
		object_error((t_object *)x, "set needs two arguments: key, value");
		return;
	}
	
	if (av && atom_gettype(av) == A_SYM) {
		k = atom_getsym(av);
		key = TagLib::String(k->s_name, TagLib::String::UTF8);
	}
	else {
		object_error((t_object *)x, "set error: first arg must be a 'key' symbol");
		return;
	}
	
	av++;
	if(atom_gettype(av) == A_SYM) {
		v = atom_getsym(av);
		value = TagLib::String(v->s_name, TagLib::String::UTF8);
		
		err = dictionary_appendsym(x->dictionary, k, v);
		metatag_outputDict(x);
		
	}
	else if(atom_gettype(av) == A_LONG) {
		int input = (int)atom_getlong(av);
		value = TagLib::String().number(input);
		
		err = dictionary_appendlong(x->dictionary, k, input);
		metatag_outputDict(x);
	}
	else if(atom_gettype(av) == A_FLOAT) {
		double input = atom_getfloat(av);
		char myfloat[50];
		snprintf(myfloat, 50, "%f", input);
		value = TagLib::String(myfloat, TagLib::String::UTF8);
		
		err = dictionary_appendfloat(x->dictionary, k, input);
		metatag_outputDict(x);
	}
	//post("value: %s", value.toCString());

	if(x->fileref && !f.isNull()) {
		map = f.file()->properties();
		if( map.contains(key) ) {
			map.replace(key, value);
			checkForRejectedProperties(f.file()->setProperties(map));
		}
		else {
			map.insert(key, value);
			checkForRejectedProperties(f.file()->setProperties(map));
		}
	}
}


void metatag_delete(t_metatag *x, t_symbol *m)
{
	TagLib::FileRef *f;
	TagLib::PropertyMap map;
	TagLib::String key = m->s_name;
	
	if(x->fileref) {
		f = x->fileref;
		if(!f->isNull()) {
			map = f->file()->properties();
			map.erase(key);
			f->file()->setProperties(map);
			
			dictionary_deleteentry(x->dictionary, m);
			metatag_outputDict(x);
		}
	}
}


#pragma mark audio properties ----------------------------------------

void metatag_audioProperties(t_metatag *x)
{
	TagLib::FileRef *f;
	TagLib::String tstr;
	std::string cstr;
	
	if (x->fileref) {
		f = x->fileref;
		if(!f->isNull() && f->audioProperties()) {

			t_atom a;
			TagLib::AudioProperties *props = f->audioProperties();

			atom_setlong(&a, props->bitrate());
			outlet_anything(x->audioPropsOut, ps_bitrate, 1, &a);
			atom_setlong(&a, props->sampleRate());
			outlet_anything(x->audioPropsOut, ps_sampleRate, 1, &a);
			atom_setlong(&a, props->channels());
			outlet_anything(x->audioPropsOut, ps_channels, 1, &a);
			atom_setlong(&a, props->length());
			outlet_anything(x->audioPropsOut, ps_length, 1, &a);
		}
	}
}


void metatag_get_all(t_metatag *x) {
	
	TagLib::FileRef f;
	TagLib::PropertyMap tags;
	TagLib::PropertyMap::ConstIterator i;
	TagLib::StringList::ConstIterator j;
	t_symbol *key;
	
	if(!x->fileref)
		return;
	
	f = *x->fileref;
	
	if(!f.isNull()) {
		
		tags = f.file()->properties();
		dictionary_clear(x->dictionary);
		
		for(i = tags.begin(); i != tags.end(); ++i) {
            std::string k = copyString(i->first);
			key = gensym(k.c_str());
            //post("key: %s", key);

			for(j = i->second.begin(); j != i->second.end(); ++j) {
				TagLib::String tstr = *j;
				std::string cstr;
				bool ok = false;
				int myint = tstr.toInt(&ok);		// try to convert string to int // TODO: should we really do that for every tag?
				
				if(ok)
					dictionary_appendlong(x->dictionary, key, myint);
				else {
					cstr = copyString(tstr);
					dictionary_appendstring(x->dictionary, key, cstr.c_str());
				}
			}
		}
	}
}


#pragma mark get album art ----------------------------------------

void metatag_picture_get(t_metatag *x, t_symbol *s, long *ac, t_atom **av)
{
	TagLib::FileRef *f = x->fileref;
	TagLib::Tag *p_tag;
	
	if (!x->fileref || f->isNull())
		return;
	if(!f->tag() || f->tag()->isEmpty())
		return;
	
	p_tag = f->tag();
	
	// what is it?
	if( TagLib::MPEG::File* mpeg = dynamic_cast<TagLib::MPEG::File*>(f->file()) ) {
		//printId3Tags(x, mpeg->ID3v2Tag());
		readPictFromId3v2(x, mpeg->ID3v2Tag());
	}
	else if( TagLib::MP4::File *mp4 = dynamic_cast<TagLib::MP4::File*>(f->file()) ) {
		readPictFromMp4(x, mp4->tag());
	}
	else if( TagLib::FLAC::File * flac = dynamic_cast<TagLib::FLAC::File*>(f->file()) ) {
		readPictFromId3v2(x, flac->ID3v2Tag());
	}
	else if( dynamic_cast<TagLib::RIFF::File*>(f->file()) ) {
		if( TagLib::RIFF::AIFF::File* aiff = dynamic_cast<TagLib::RIFF::AIFF::File*>(f->file()) ) {
			readPictFromId3v2(x, aiff->tag());
		}
		else if( TagLib::RIFF::WAV::File* wav = dynamic_cast<TagLib::RIFF::WAV::File*>(f->file()) ) {
			readPictFromId3v2(x, wav->tag());
		}
	}
}



void readPictFromId3v2(t_metatag *x, TagLib::ID3v2::Tag *tag)
{
	TagLib::ID3v2::FrameList list = tag->frameList("APIC");
	int pos = -1;
	
	if (list.isEmpty()) {
		object_post((t_object *)x, "no pictures found!");
		return;
	}
		
	// old code from here
	TagLib::ID3v2::AttachedPictureFrame *frame = static_cast<TagLib::ID3v2::AttachedPictureFrame *>(list.front());
	
	const TagLib::ByteVector pict = frame->picture();
	//const char *mime = frame->mimeType().toCString(true);
	TagLib::String mime = frame->mimeType();
	TagLib::String jpg("jpeg");
	
	if((pos = mime.rfind(jpg)))
		exportPictData(x, pict, ps_jpgFile);
	else
		exportPictData(x, pict, ps_pngFile);
	
}


void readPictFromMp4(t_metatag *x, TagLib::MP4::Tag *tag)
{
	TagLib::MP4::ItemListMap itemsListMap = tag->itemListMap();
	TagLib::MP4::Item coverItem = itemsListMap["covr"];
	TagLib::MP4::CoverArtList coverArtList = coverItem.toCoverArtList();
	
	if (coverArtList.isEmpty()) {
		object_post((t_object *)x, "no pictures found!");
		return;
	}
	TagLib::MP4::CoverArt coverArt = coverArtList.front();
	TagLib::ByteVector pict = coverArt.data();
	
	TagLib::MP4::CoverArt::Format format = coverArt.format();
	if(format == TagLib::MP4::CoverArt::JPEG)
		exportPictData(x, pict, ps_jpgFile);
	else
		exportPictData(x, pict, ps_pngFile);
	
}



void exportPictData(t_metatag *x, TagLib::ByteVector pict, t_symbol *outputPath)
{
	FILE *outputFile;
	t_atom a;
	unsigned long size ;

	atom_setsym(&a, outputPath);
	size = pict.size();
	
	outputFile = fopen(outputPath->s_name, "wb");
	
	fwrite(pict.data(), size, 1, outputFile);
	fclose(outputFile);
	outlet_anything(x->audioPropsOut, ps_pictpath, 1, &a);	// output file path
}



void printId3Tags(t_metatag *x, TagLib::ID3v2::Tag *tag)
{
	TagLib::ID3v2::FrameList list;
	TagLib::ID3v2::FrameList::Iterator iter;
	
	
	// Get the use text
	post("get TXXX");
    list = tag->frameListMap()["TXXX"];
    for( iter = list.begin(); iter != list.end(); iter++ )
    {
		TagLib::ID3v2::UserTextIdentificationFrame* p_txxx = dynamic_cast<TagLib::ID3v2::UserTextIdentificationFrame*>(*iter);
        if( !p_txxx )
            continue;
        if( !strcmp( p_txxx->description().toCString( true ), "TRACKTOTAL" ) )
        {
            post("%s", p_txxx->fieldList().back().toCString( true ) );
            continue;
        }
        if( !strcmp( p_txxx->description().toCString( true ), "MusicBrainz Album Id" ) )
        {
			post("%s", p_txxx->fieldList().back().toCString( true ) );
            continue;
        }
			post("%s %s", p_txxx->description().toCString( true ),
						  p_txxx->fieldList().back().toCString( true ) );
    }
	
	
	// try other stuff
	list = tag->frameListMap()["TBPM"];
	if(!list.isEmpty())
		post( "bpm: %s", list.front()->toString().toCString(true) );
	
	list = tag->frameListMap()["USLT"];
	if(!list.isEmpty())
		post( "uslt: %s", list.front()->toString().toCString(true) );
	
}


#pragma mark utilities--------------------------------------------

const char* convertString(TagLib::String tstr) {
	std::string cstr;
	if (!tstr.isEmpty() && !tstr.isNull()) {
		cstr = tstr.to8Bit(true);
	} else {
		cstr = "";
	}
    std::cout << "c_str: " << cstr.c_str() << "\n";
    
	//return (char *)cstr.c_str();
    return cstr.c_str();
}


std::string copyString(TagLib::String tstr) {
	std::string cstr;
	if (!tstr.isEmpty() && !tstr.isNull()) {
		cstr = tstr.to8Bit(true);
	} else {
		cstr = "";
	}
	return cstr;
}


void metatag_setname(t_metatag *x, void *attr, long argc, t_atom *argv)
{
	t_symbol		*name = atom_getsym(argv);
	
	if (!x->name || !name || x->name!=name) {
		object_free(x->dictionary); // will call object_unregister
		x->dictionary = dictionary_new();
		x->dictionary = dictobj_register(x->dictionary, &name);
		x->name = name;
	}
	if (!x->dictionary)
		object_error((t_object*)x, "could not create dictionary named %s", name->s_name);
}


void checkForRejectedProperties(const TagLib::PropertyMap &tags)
{ // copied from tagreader.cpp
	if(tags.size() > 0) {
        std::cout << "-- rejected TAGs (properties) --" << "\n";
		for(TagLib::PropertyMap::ConstIterator i = tags.begin(); i != tags.end(); ++i) {
			for(TagLib::StringList::ConstIterator j = i->second.begin(); j != i->second.end(); ++j) {
				post("%s - %s", convertString(i->first), convertString(*j) );
			}
		}
	}
}

void metatag_outputDict(t_metatag *x) {
	if (x->name) {
		t_atom	a[1];
		
		atom_setsym(a, x->name);
		outlet_anything(x->outlet_dict, _sym_dictionary, 1, a);
	}
}


void metatag_writeToFile(t_metatag *x) {
	TagLib::FileRef f;
	
	if(x->fileref) {
		f = *x->fileref;
		if(!f.isNull()) {
			bool success = f.file()->save();
            if(success)
                object_post((t_object *)x, "file saved successfully!");
            else
                object_error((t_object *)x, "saving failed!");
		}
	}
	else {
		object_error((t_object *)x, "can't save file!");
	}
}


#pragma mark file handling ----------------------------------------

void metatag_close(t_metatag *x)
{
	if (x->fileref) {
		delete x->fileref;
		x->fileref = NULL;
	}
	dictionary_clear(x->dictionary);
	metatag_outputDict(x);
}

// TODO: need to defer this?
void metatag_open(t_metatag *x, t_symbol *s, long ac, t_atom *av)
{
	char fname[MAX_PATH_CHARS] = "";
	char aname[MAX_PATH_CHARS] = "";
	short fpath = 0;
	t_fourcc type;
	TagLib::FileRef *fileref = NULL;
	
	if (ac && av && atom_gettype(av) == A_SYM) {
		strncpy(fname, atom_getsym(av)->s_name, MAX_PATH_CHARS);
		if (locatefile_extended(fname, &fpath, &type, &type, 0)) {
			object_error((t_object *)x, "file not found: %s", atom_getsym(av)->s_name);
			return;
		}
	} else {
		if (open_dialog(fname, &fpath, &type, &type, 0)) {
			return;
		}
	}
	path_topotentialname(fpath, fname, aname, true);
#ifdef WIN_VERSION
	path_nameconform(aname, fname, PATH_STYLE_NATIVE_WIN, PATH_TYPE_ABSOLUTE);
	{
		WCHAR *zWide;

		zWide = (WCHAR*) charset_utf8tounicode(fname,NULL); 
		if (zWide) {
			fileref = new TagLib::FileRef(zWide);
			sysmem_freeptr(zWide);
		} else {
			object_error((t_object *)x, "error opening file: %s", fname);
			return;
		}
	}
#else
	path_nameconform(aname, fname, PATH_STYLE_NATIVE, PATH_TYPE_BOOT);
	fileref = new TagLib::FileRef(fname);
#endif
//	post("path: %s", fname);
	
	if (fileref->isNull()) {
		object_error((t_object *)x, "error opening file: %s", fname);
		delete fileref;
		return;
	}
	if (x->fileref)
		delete x->fileref;
	x->fileref = fileref;
	//strncpy(x->filepath, fname, MAX_PATH_CHARS); // save the path used; we may need it for extracting cover art
	//x->file = gensym(aname); // use abs path, I guess;
	
	
	metatag_get_all(x);
	metatag_outputDict(x);
	
}


void printProperties(t_metatag *x)
{
	TagLib::FileRef f;
	TagLib::Tag *tag;
	TagLib::String tstr;
	std::string cstr;
	TagLib::PropertyMap tags;
	TagLib::PropertyMap::ConstIterator i;
	TagLib::StringList::ConstIterator j;
    
    if(x->fileref==NULL)
       return ;
	
    f = *x->fileref;
    
	if(!f.isNull() && f.tag()) {
		
		tag = f.tag();
		
        post("-- TAG (basic) --");
        
		tstr = tag->title();
		//post("title %s", convertString(tstr));
        post("title %s", copyString(tstr).c_str());
		
		tstr = tag->artist();
		post("artist %s", copyString(tstr).c_str());
		
		tstr = tag->album();
		post("album %s", copyString(tstr).c_str());
		
		tstr = tag->comment();
		post("comment %s", copyString(tstr).c_str());
		
		tstr = tag->genre();
		post("genre %s", copyString(tstr).c_str());
		
		int year = tag->year();
		post("year %ld", year);
		
		int track = tag->track();
		post("track %ld", track);
		
		tags = f.file()->properties();
		
		post("-- TAG (properties) --");
		for( i = tags.begin(); i != tags.end(); ++i) {
			for(j = i->second.begin(); j != i->second.end(); ++j) {
				//post("%s: %s", i->first.toCString(true), convertString(*j));
                post("%s: %s", i->first.toCString(true), copyString(*j).c_str());
			}
		}
	}

}


	
void metatag_assist(t_metatag *x, void *b, long m, long a, char *s)
{
	if (m == 1) { //input
		sprintf(s,"messages in");
	}
	else {	//output
		switch(a) {
			case 0:
				sprintf(s, "dump out"); break;
			case 1:
				sprintf(s, "dictionary out"); break;
			default: sprintf(s, "out"); break;
		}
	}
}

void metatag_free(t_metatag *x)
{
	if (x->fileref)
		delete x->fileref;
	
	object_free((t_object *)x->dictionary); // will call object_unregister
}

void *metatag_new(t_symbol *s, long ac, t_atom *av)
{
	t_metatag *x;
	long			attrstart = attr_args_offset(ac, av);
	t_symbol		*name = NULL;
	
	if ((x = (t_metatag *)object_alloc((t_class *)metatag_class))) {
		if (attrstart && atom_gettype(av) == A_SYM)
			name = atom_getsym(av);
		
		x->outlet_dict = outlet_new(x, "dictionary");
		x->audioPropsOut = outlet_new(x, NULL);

		x->dictionary = dictionary_new();
		
		attr_args_process(x, ac, av);
		if (!x->name) {
			if (name)
				object_attr_setsym(x, _sym_name, name);
			else
				object_attr_setsym(x, _sym_name, symbol_unique());
		}
		
		x->fileref = NULL;
		//x->filepath[0] = '\0';
	}
	return (x);
}
