/* Shared audio extension map, including native WAV fallback. */
#ifndef POCKETOS_AUDIO_FORMATS_H
#define POCKETOS_AUDIO_FORMATS_H
#include <string.h>
#include <strings.h>
static inline const char *pocketos_demuxer(const char *path){
    const char *e=strrchr(path,'.');if(!e)return NULL;
    static const struct {const char *ext,*format;} formats[]={
    {".wav","wav"},
    {".flac","flac"},
    {".mp3","mp3"},
    {".mp2","mp3"},
    {".mpa","mp3"},
    {".aac","aac"},
    {".adts","aac"},
    {".m4a","mov"},
    {".mp4","mov"},
    {".m4b","mov"},
    {".alac","mov"},
    {".ogg","ogg"},
    {".oga","ogg"},
    {".opus","ogg"},
    {".wma","asf"},
    {".asf","asf"},
    {".ape","ape"},
    {".aiff","aiff"},
    {".aif","aiff"},
    {".aifc","aiff"},
    {".wv","wv"},
    {".tta","tta"},
    {".mpc","mpc"},
    {".mpp","mpc"},
    {".mp+","mpc"},
    {".ac3","ac3"},
    {".eac3","eac3"},
    {".dts","dts"},
    {".dsf","dsf"},
    {".dff","iff"},
    {".caf","caf"},
    {".au","au"},
    {".snd","au"},
    {".amr","amr"},
    {".3gp","mov"},
    {".3g2","mov"},
    {".mka","matroska"},
    {".webm","matroska"},
    {".mkv","matroska"},
    {".ra","rm"},
    {".rm","rm"},
    {".spx","ogg"},
    {".tak","tak"},
    {".voc","voc"},
    {".w64","w64"},
    {".mlp","mlp"},
    {".thd","truehd"},
    {".aob","mpeg"},
    {".oma","oma"},
    {".aa3","oma"},
    {".wvpk","wv"},
    };
    for(size_t i=0;i<sizeof(formats)/sizeof(formats[0]);i++)if(!strcasecmp(e,formats[i].ext))return formats[i].format;
    return NULL;
}
#endif
