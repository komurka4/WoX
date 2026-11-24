/* Phase132: wox.cfg parsing, lookup, defaults, diagnostics and ROM checks. */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include "xeen/wox_config.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#include <process.h>
#define mkdir_one(p) _mkdir(p)
#else
#include <unistd.h>
#define mkdir_one(p) mkdir(p,0700)
#endif
static int failures;
/* Phase 331: sets (VALUE) or removes (NULL) environment variable NAME; 1 on success. */
static int set_env(const char *name,const char *value){
#if defined(_WIN32)
    return _putenv_s(name,value?value:"")==0;
#else
    return value?setenv(name,value,1)==0:unsetenv(name)==0;
#endif
}
#define CHECK(c) do{if(!(c)){fprintf(stderr,"%s:%d: CHECK failed: %s\n",__FILE__,__LINE__,#c);++failures;}}while(0)
static char root[512];
static const char *put(const char *name,const char *text)
{
    static char path[1024];FILE *f;
    snprintf(path,sizeof path,"%s/%s",root,name);
    f=fopen(path,"wb");if(!f){perror(path);exit(2);}
    fputs(text,f);fclose(f);return path;
}
static int load(const char *text,xeen_wox_config *c,char *error)
{
    return xeen_wox_config_load(put("wox.cfg",text),c,error,512);
}
int main(void)
{
    xeen_wox_config c;char error[512],expect[1100];
    snprintf(root,sizeof root,"phase132_cfg_%ld",(long)
#if defined(_WIN32)
        _getpid()
#else
        getpid()
#endif
        );
    mkdir_one(root);
    /* Defaults (the XEEN.EXE player's). */
    xeen_wox_config_defaults(&c,"base");
    CHECK(c.system==XEEN_MUSIC_EXTERNAL && c.voice_volume==100u && c.music_volume==100u && !c.fullscreen);
    CHECK(!strcmp(c.mt32_roms,"base/roms/mt32"));
    CHECK(!strcmp(c.sc55_roms,"base/roms/sc55"));
    CHECK(c.cd_tracks_auto&&!strcmp(c.cd_tracks,"base/music"));
    /* Phase 331: the former environment overrides are ignored (owner decision): auto is roms/mt32, roms/sc55 and
     * music beside wox.cfg whatever they hold. */
    CHECK(set_env("XEEN_MT32_ROMS","elsewhere/mt")&&set_env("XEEN_SC55_ROMS","elsewhere/sc")&&
          set_env("XEEN_CD_TRACKS","elsewhere/cd"));
    xeen_wox_config_defaults(&c,"base");
    CHECK(!strcmp(c.mt32_roms,"base/roms/mt32")&&!strcmp(c.sc55_roms,"base/roms/sc55"));
    CHECK(c.cd_tracks_auto&&!strcmp(c.cd_tracks,"base/music"));
    CHECK(load("[files]\nmt32_roms_dir = auto\nsc55_roms_dir = auto\ncd_tracks_dir = auto\n",&c,error)&&
          strstr(c.mt32_roms,"roms/mt32")&&!strstr(c.mt32_roms,"elsewhere")&&strstr(c.sc55_roms,"roms/sc55")&&
          c.cd_tracks_auto&&strstr(c.cd_tracks,"music")&&!strstr(c.cd_tracks,"elsewhere"));
    CHECK(set_env("XEEN_MT32_ROMS",NULL)&&set_env("XEEN_SC55_ROMS",NULL)&&set_env("XEEN_CD_TRACKS",NULL));
    /* Phase 331: the default text create writes is the accessor's, and every setting the parser knows is listed. */
    {const char *sec,*name;size_t i=0;int addons=0;
     while(xeen_wox_config_setting(i,&sec,&name)){CHECK(sec&&name&&*name);addons+=!strcmp(sec,"addons");++i;}
     CHECK(i>=40u&&addons==18);
     CHECK(strstr(xeen_wox_config_default_text(),"[bugfix]\n")&&!strstr(xeen_wox_config_default_text(),"$XEEN"));}
    /* Each backend, with the required sections (Phase 336: music_override; the player's alias none is refused). */
    {static const struct {const char *v;xeen_music_system s;} t[]={
        {"system",XEEN_MUSIC_EXTERNAL},{"mt32",XEEN_MUSIC_MT32},{"sc55",XEEN_MUSIC_SC55},
        {"sbpro",XEEN_MUSIC_SBPRO},{"off",XEEN_MUSIC_OFF}};
     for(unsigned i=0;i<sizeof t/sizeof *t;++i){char text[256];
        snprintf(text,sizeof text,"[audio]\nmusic_override = %s\n[files]\nmt32_roms_dir = roms/mt32\nsc55_roms_dir = roms/sc55\n[display]\nfullscreen = off\n",t[i].v);
        CHECK(load(text,&c,error));CHECK(c.system==t[i].s);}}
    /* One key, the player's values only. */
    CHECK(!load("[audio]\nsystem = mt32\n",&c,error)&&strstr(error,"unknown setting: system"));
    CHECK(!load("[audio]\nmusic_override = external\n",&c,error)&&strstr(error,"invalid music_override"));
    CHECK(!load("[audio]\nmusic_override = none\n",&c,error)&&strstr(error,"invalid music_override (")&&strstr(error,"off): none"));
    /* Phase 336 (owner decision): the former names and spellings are unknown settings, refused. */
    {static const char *const refused[][2]={
        {"[audio]\nmusic = auto\n","music"},{"[audio]\nmusic-override = auto\n","music-override"},
        {"[files]\ndata = auto\n","data"},{"[files]\nsave-dir = auto\n","save-dir"},
        {"[files]\nXEEN_CFG_location = auto\n","XEEN_CFG_location"},{"[files]\nmt32-roms = auto\n","mt32-roms"},
        {"[files]\nsc55_roms = auto\n","sc55_roms"},{"[files]\ncd-tracks = auto\n","cd-tracks"},
        {"[files]\ndata-dir = auto\n","data-dir"},{"[audio]\nvoice-volume = 1\n","voice-volume"},
        {"[audio]\nmidi-device = auto\n","midi-device"},{"[display]\nauthentic-cursor = on\n","authentic-cursor"},
        {"[display]\nlocked-scale = on\n","locked-scale"},{"[addons]\nmap-movement = on\n","map-movement"},
        {"[bugfix]\nquest-up-down = on\n","quest-up-down"},{"[audio]\nno-music = true\n","no-music"},
        {"[audio]\nno_music = on\n","no_music"},{"[display]\nwindowed = true\n","windowed"},
        {"[addons]\nwizard-eye-plus.sky = black\n","wizard-eye-plus.sky"}};
     for(unsigned i=0;i<sizeof refused/sizeof *refused;++i){char want[96];
      snprintf(want,sizeof want,"wox.cfg:2: unknown setting: %s",refused[i][1]);
      CHECK(!load(refused[i][0],&c,error)&&strstr(error,want));}}
    /* Relative paths are relative to the file; spaces, quotes, absolute paths. */
    CHECK(load("[files]\nmt32_roms_dir = my roms/mt 32\nsc55_roms_dir = \"sc 55 dir\"\n",&c,error));
    snprintf(expect,sizeof expect,"%s/my roms/mt 32",root);CHECK(!strcmp(c.mt32_roms,expect));
    snprintf(expect,sizeof expect,"%s/sc 55 dir",root);CHECK(!strcmp(c.sc55_roms,expect));
#if !defined(_WIN32)
    CHECK(load("[files]\nmt32_roms_dir = '/opt/x y/mt32'\n",&c,error));CHECK(!strcmp(c.mt32_roms,"/opt/x y/mt32"));
#endif
    /* Fullscreen both ways (Phase 336: on/off only). */
    CHECK(load("[display]\nfullscreen = on\n",&c,error)&&c.fullscreen==1);
    CHECK(load("[display]\nfullscreen = off\n",&c,error)&&c.fullscreen==0);
    CHECK(load("[display]\nfullscreen = auto\n",&c,error)&&c.fullscreen==0);
    CHECK(!load("[display]\nfullscreen = true\n",&c,error)&&strstr(error,"wox.cfg:2: must be on or off: fullscreen"));
    CHECK(!load("[display]\nfullscreen = false\n",&c,error)&&strstr(error,"must be on or off"));
    /* Owner-approved add-ons ([addons], off by default; not original behavior). */
    CHECK(load("[display]\nfullscreen = off\n",&c,error)&&c.addon_identify==0);
    CHECK(load("[addons]\nidentify = on\n",&c,error)&&c.addon_identify==1);
    CHECK(load("[addons]\nidentify = off\n",&c,error)&&c.addon_identify==0);
    CHECK(!load("[audio]\nidentify = on\n",&c,error)&&strstr(error,"belongs in [addons]"));
    CHECK(!load("[addons]\nidentify = yes\n",&c,error)&&strstr(error,"must be on or off: identify"));
    CHECK(!load("[addons]\nidentify = true\n",&c,error)&&strstr(error,"must be on or off: identify"));
    CHECK(!load("[addons]\nidentify = On\n",&c,error)&&strstr(error,"must be on or off"));
    /* Owner-approved bug fixes ([bugfix], off by default; not original behavior; Phase 237's two). */
    CHECK(load("[display]\nfullscreen = off\n",&c,error)&&!c.bugfix_left_clip_colors&&!c.bugfix_sprite_width_trim);
    CHECK(load("[bugfix]\nleft_clip_colors = on\n",&c,error)&&c.bugfix_left_clip_colors==1&&!c.bugfix_sprite_width_trim);
    CHECK(load("[bugfix]\nsprite_width_trim = on\n",&c,error)&&c.bugfix_sprite_width_trim==1&&!c.bugfix_left_clip_colors);
    CHECK(load("[bugfix]\nleft_clip_colors = off\nsprite_width_trim = off\n",&c,error)
          &&!c.bugfix_left_clip_colors&&!c.bugfix_sprite_width_trim);
    CHECK(!load("[addons]\nleft_clip_colors = on\n",&c,error)&&strstr(error,"belongs in [bugfix]"));
    CHECK(!load("[bugfix]\nsprite_width_trim = 1\n",&c,error)&&strstr(error,"must be on or off"));
    CHECK(!load("[bugfix]\nsprite_width_trim = false\n",&c,error)&&strstr(error,"must be on or off"));
    /* Phase269: quest_up_down, off by default */
    CHECK(load("[display]\nfullscreen = off\n",&c,error)&&!c.bugfix_quest_up_down);
    CHECK(load("[bugfix]\nquest_up_down = on\n",&c,error)&&c.bugfix_quest_up_down==1&&!c.bugfix_sprite_width_trim);
    /* Volumes, MIDI selection, comments, BOM, the movie keys. */
    CHECK(load("\xef\xbb\xbf# c\n; c\n[audio]\nvoice_volume = 0\nmusic_volume = 200\nmidi_device = 2\nmidi_port = 128:0\n"
               "[files]\ndata_dir = auto\ncue = auto\n[timing]\ntiming = approximate\n",&c,error));
    CHECK(c.voice_volume==0u&&c.music_volume==200u&&!strcmp(c.midi_device,"2")&&!strcmp(c.midi_port,"128:0"));
    CHECK(load("[audio]\nmidi_device = auto\nmidi_port = auto\n",&c,error)&&!*c.midi_device&&!*c.midi_port);
    /* Diagnostics: file:line: reason. */
    /* Phase 286: every original driver but the PC speaker's is a value (adlib was rejected before Phase 286); the PC
     * speaker's driver (ibmmus) has no value and nothing stands in for it. */
    {static const char *const v[]={"adlib","sb","pas","wave","tap10"};
     static const xeen_music_system k[]={XEEN_MUSIC_ADLIB,XEEN_MUSIC_SB,XEEN_MUSIC_PAS,XEEN_MUSIC_WAVE,XEEN_MUSIC_TAP10};
     static const char *const d[]={"admus","blastmus","specmus","wavemus","tapmus"};
     for(unsigned i=0;i<5u;i++){char t[64];snprintf(t,sizeof t,"[audio]\nmusic_override = %s\n",v[i]);
      CHECK(load(t,&c,error)&&c.system==k[i]&&!strcmp(xeen_music_system_name(k[i]),v[i])&&!strcmp(xeen_music_system_driver(k[i]),d[i]));}}
    CHECK(!strcmp(xeen_music_system_driver(XEEN_MUSIC_MT32),"rolmus")&&!strcmp(xeen_music_system_driver(XEEN_MUSIC_SC55),"canmus")
          &&!strcmp(xeen_music_system_driver(XEEN_MUSIC_SBPRO),"promus")&&!strcmp(xeen_music_system_driver(XEEN_MUSIC_OFF),"nullmus"));
    /* Phase 286: music_override = auto resolves XEEN.CFG's music word (offset 4) as 15EC9 does; explicit values override; the
     * absent key keeps the default; no XEEN.CFG or the PC speaker: no music. */
    {static const xeen_music_system want[12]={XEEN_MUSIC_OFF,XEEN_MUSIC_ADLIB,XEEN_MUSIC_SB,XEEN_MUSIC_SBPRO,XEEN_MUSIC_MT32,
      XEEN_MUSIC_SC55,XEEN_MUSIC_PAS,XEEN_MUSIC_OFF,XEEN_MUSIC_WAVE,XEEN_MUSIC_TAP10,XEEN_MUSIC_OFF,XEEN_MUSIC_OFF};
     uint8_t cfg[29]={0};xeen_music_resolution r;char why[300];
     CHECK(load("[audio]\nmusic_override = auto\n",&c,error)&&c.music_auto&&c.system==XEEN_MUSIC_EXTERNAL);
     for(unsigned w=0;w<12u;w++){cfg[4]=(uint8_t)(w==11u?0xffu:w);cfg[5]=(uint8_t)(w==11u?0xffu:0u);
      xeen_music_resolve(&c,cfg,sizeof cfg,&r,why,sizeof why);
      CHECK(r.system==want[w]&&r.from_xeen_cfg&&r.xeen_cfg_present&&r.unsupported==(w==7u));}
     xeen_music_resolve(&c,NULL,0,&r,why,sizeof why);
     CHECK(r.system==XEEN_MUSIC_OFF&&!r.xeen_cfg_present&&strstr(why,"no XEEN.CFG"));
     cfg[4]=4;cfg[5]=0;xeen_music_resolve(&c,cfg,5,&r,why,sizeof why);
     CHECK(r.system==XEEN_MUSIC_OFF&&r.word==0u); /* a file too short for the word reads 0 */
     cfg[4]=7;xeen_music_resolve(&c,cfg,sizeof cfg,&r,why,sizeof why);
     CHECK(r.unsupported&&!strcmp(r.driver,"ibmmus")&&strstr(why,"PC speaker"));
     CHECK(load("[audio]\nmusic_override = sc55\n",&c,error)&&!c.music_auto);cfg[4]=3;
     xeen_music_resolve(&c,cfg,sizeof cfg,&r,why,sizeof why);
     CHECK(r.system==XEEN_MUSIC_SC55&&!r.from_xeen_cfg&&!strcmp(r.driver,"promus"));
     CHECK(load("[audio]\nvoice_volume = 50\n",&c,error)&&!c.music_auto);
     xeen_music_resolve(&c,cfg,sizeof cfg,&r,why,sizeof why);CHECK(r.system==XEEN_MUSIC_EXTERNAL&&!r.from_xeen_cfg);
     CHECK(!load("[audio]\nmusic_override = AUTO\n",&c,error)&&strstr(error,"invalid music_override")&&strstr(error,"auto"));}
    /* Phase 286: [audio] speech = auto (default) | on | off over XEEN.CFG's sound word (15E52). */
    {uint8_t cfg[29]={0};xeen_speech_resolution r;char why[300];
     static const char *const drv[11]={"nullsnd","ibmsnd","adsnd","nullsnd","nullsnd","source","blastsnd","prosnd","specsnd","covxsnd","prosnd"};
     CHECK(load("[audio]\nvoice_volume = 100\n",&c,error)&&c.speech==XEEN_SPEECH_AUTO);
     for(unsigned w=0;w<11u;w++){cfg[0]=(uint8_t)w;xeen_speech_resolve(&c,cfg,sizeof cfg,&r,why,sizeof why);
      int none=w==0u||w==3u||w==4u;
      CHECK(!strcmp(r.driver,drv[w])&&r.driver_voice==!none&&r.pc_speaker==(w==1u)&&r.modelled==!none
            &&r.audible==(!none&&w!=1u)&&!r.forced_off&&!r.fallback);}
     CHECK(load("[audio]\nspeech = on\n",&c,error)&&c.speech==XEEN_SPEECH_ON);
     for(unsigned w=0;w<11u;w++){cfg[0]=(uint8_t)w;xeen_speech_resolve(&c,cfg,sizeof cfg,&r,why,sizeof why);
      CHECK(r.audible&&r.modelled&&!r.forced_off&&r.fallback==(w==0u||w==1u||w==3u||w==4u));}
     CHECK(load("[audio]\nspeech = off\n",&c,error)&&c.speech==XEEN_SPEECH_OFF);
     for(unsigned w=0;w<11u;w++){cfg[0]=(uint8_t)w;xeen_speech_resolve(&c,cfg,sizeof cfg,&r,why,sizeof why);
      CHECK(!r.audible&&!r.modelled&&r.forced_off&&strstr(why,"subtitles"));}
     cfg[0]=0xff;cfg[1]=0xff;CHECK(load("[audio]\nspeech = auto\n",&c,error));
     xeen_speech_resolve(&c,cfg,sizeof cfg,&r,why,sizeof why);CHECK(!strcmp(r.driver,"nullsnd")&&!r.audible);
     xeen_speech_resolve(&c,NULL,0,&r,why,sizeof why);CHECK(!r.xeen_cfg_present&&r.word==0u);
     CHECK(!load("[audio]\nspeech = yes\n",&c,error)&&strstr(error,"invalid speech (auto, on, off)"));
     CHECK(!load("[audio]\nspeech = Off\n",&c,error)&&strstr(error,"invalid speech"));
     CHECK(!load("[audio]\nspeech = off\nspeech = on\n",&c,error)&&strstr(error,"duplicate"));
     CHECK(!load("[playback]\nspeech-mode = config\n[audio]\nspeech = off\n",&c,error)&&strstr(error,"unknown setting: speech-mode"));}
    CHECK(!load("[audio]\nmusic_override = pcspeaker\n",&c,error)&&strstr(error,"wox.cfg:2: invalid music"));
    CHECK(!load("[audio]\nmusic_override = ibmmus\n",&c,error)&&strstr(error,"wox.cfg:2: invalid music"));
    CHECK(!load("[audio]\nmusic_override = adlib2\n",&c,error)&&strstr(error,"wox.cfg:2: invalid music")&&strstr(error,"tap10"));
    CHECK(!load("[audio]\nmusic_override = config\n",&c,error)&&strstr(error,"wox.cfg:2: invalid music_override (")&&strstr(error,"): config"));
    CHECK(!load("[display]\nfullscreen = yes\n",&c,error)&&strstr(error,":2: must be on or off"));
    CHECK(!load("[audio]\nvoice_volume = 201\n",&c,error)&&strstr(error,"0..200"));
    CHECK(!load("[audio]\nmusic_volume = -1\n",&c,error)&&strstr(error,"0..200"));
    CHECK(!load("[audio]\nlouder = on\n",&c,error)&&strstr(error,"unknown setting: louder"));
    CHECK(!load("[audio]\nmusic_override = mt32\nmusic_override = sc55\n",&c,error)&&strstr(error,":3: duplicate"));
    CHECK(!load("[display]\nmusic_override = mt32\n",&c,error)&&strstr(error,"belongs in [audio]"));
    CHECK(!load("[sound]\n",&c,error)&&strstr(error,"unknown section"));
    CHECK(!load("[audio\n",&c,error)&&strstr(error,"malformed section"));
    CHECK(!load("[audio]\nsystem\n",&c,error)&&strstr(error,"expected key = value"));
    CHECK(!load("[files]\nmt32_roms_dir = \"x\n",&c,error)&&strstr(error,"unterminated"));
    /* A missing file is an error for the loader (the frontend creates it). */
    {char missing[600];snprintf(missing,sizeof missing,"%s/absent.cfg",root);
     CHECK(!xeen_wox_config_load(missing,&c,error,sizeof error)&&strstr(error,"cannot open"));}
    /* ROM checks: only the selected backend's; sbpro and external need none. */
    {char dir[600],file[700];FILE *f;
     snprintf(dir,sizeof dir,"%s/mt",root);mkdir_one(dir);
     CHECK(load("[audio]\nmusic_override = mt32\n[files]\nmt32_roms_dir = absent dir\n",&c,error));
     CHECK(!xeen_wox_config_check_roms(&c,error,sizeof error)&&strstr(error,"MT-32 ROM directory not found"));
     CHECK(load("[audio]\nmusic_override = mt32\n[files]\nmt32_roms_dir = mt\n",&c,error));
     CHECK(!xeen_wox_config_check_roms(&c,error,sizeof error)&&strstr(error,"MT32_CONTROL.ROM"));
     snprintf(file,sizeof file,"%s/MT32_CONTROL.ROM",dir);f=fopen(file,"wb");fclose(f);
     CHECK(!xeen_wox_config_check_roms(&c,error,sizeof error)&&strstr(error,"MT32_PCM.ROM"));
     snprintf(file,sizeof file,"%s/MT32_PCM.ROM",dir);f=fopen(file,"wb");fclose(f);
     CHECK(xeen_wox_config_check_roms(&c,error,sizeof error));
     CHECK(load("[audio]\nmusic_override = sc55\n[files]\nsc55_roms_dir = absent\n",&c,error));
     CHECK(!xeen_wox_config_check_roms(&c,error,sizeof error)&&strstr(error,"SC-55 ROM directory not found"));
     CHECK(load("[audio]\nmusic_override = sbpro\n[files]\nmt32_roms_dir = absent\nsc55_roms_dir = absent\n",&c,error));
     CHECK(xeen_wox_config_check_roms(&c,error,sizeof error));
     CHECK(load("[audio]\nmusic_override = system\n[files]\nmt32_roms_dir = absent\n",&c,error));
     CHECK(xeen_wox_config_check_roms(&c,error,sizeof error));}
    /* Create once: the defaults load as the defaults; an existing file is kept. */
    {char path[600];FILE *f;char text[64]={0};
     snprintf(path,sizeof path,"%s/created.cfg",root);remove(path);
     CHECK(xeen_wox_config_create(path));
     CHECK(xeen_wox_config_load(path,&c,error,sizeof error));
     CHECK(c.system==XEEN_MUSIC_EXTERNAL&&c.voice_volume==100u&&!c.fullscreen&&!c.addon_identify);
     CHECK(c.scale==6u&&!c.aspect&&!c.authentic_cursor&&c.data_auto); /* Phase 270; Phase 335 (owner): the created
                                                                       file says 6 (was 3) */
     CHECK(load("[display]\n",&c,error)&&c.scale==3u); /* without the line: still 3 */
     f=fopen(path,"wb");fputs("[audio]\nmusic_override = sbpro\n",f);fclose(f);
     CHECK(xeen_wox_config_create(path));
     f=fopen(path,"rb");CHECK(fread(text,1,sizeof text-1u,f)>0);fclose(f);
     CHECK(!strcmp(text,"[audio]\nmusic_override = sbpro\n"));remove(path);}
    /* Phase 270: [display] scale/aspect/authentic_cursor and [files] data_dir. */
    xeen_wox_config_defaults(&c,"base");
    CHECK(c.scale==3u&&!c.aspect&&!c.authentic_cursor&&c.data_auto&&!strcmp(c.data,"."));
    CHECK(load("[display]\nscale = 1\naspect = on\nauthentic_cursor = on\n[files]\ndata_dir = game\n",&c,error));
    snprintf(expect,sizeof expect,"%s/game",root);
    CHECK(c.scale==1u&&c.aspect==1&&c.authentic_cursor==1&&!c.data_auto&&!strcmp(c.data,expect));
    CHECK(load("[display]\nscale = 6\naspect = off\nauthentic_cursor = off\n[files]\ndata_dir = /abs/game\n",&c,error));
    CHECK(c.scale==6u&&!c.aspect&&!c.authentic_cursor&&!strcmp(c.data,"/abs/game"));
    CHECK(load("[display]\nscale = auto\naspect = auto\n[files]\ndata_dir = auto\n",&c,error));
    CHECK(c.scale==3u&&!c.aspect&&c.data_auto&&!strcmp(c.data,"."));
    CHECK(load("[files]\ndata_dir = ./auto\n",&c,error)&&!c.data_auto);
    CHECK(!load("[display]\nscale = 0\n",&c,error)&&strstr(error,"wox.cfg:2: scale must be a whole number 1..10"));
    /* Phase 333 (owner request): the scale goes up to 10 (XEEN_WOX_SCALE_MAX); 11, a leading zero or a sign do not. */
    CHECK(load("[display]\nscale = 7\n",&c,error)&&c.scale==7u);
    CHECK(load("[display]\nscale = 10\n",&c,error)&&c.scale==10u);
    CHECK(!load("[display]\nscale = 11\n",&c,error)&&strstr(error,"scale must be"));
    CHECK(!load("[display]\nscale = 12\n",&c,error)&&strstr(error,"scale must be"));
    CHECK(!load("[display]\nscale = 100\n",&c,error)&&strstr(error,"scale must be"));
    CHECK(!load("[display]\nscale = 05\n",&c,error)&&strstr(error,"scale must be"));
    CHECK(!load("[display]\nscale = +5\n",&c,error)&&strstr(error,"scale must be"));
    CHECK(!load("[display]\nscale = 1x\n",&c,error)&&strstr(error,"scale must be"));
    CHECK(!load("[display]\naspect = yes\n",&c,error)&&strstr(error,"wox.cfg:2: must be on or off: aspect"));
    CHECK(!load("[display]\nauthentic_cursor = 1\n",&c,error)&&strstr(error,"must be on or off"));
    /* Phase 332: [display] locked_scale (an owner-approved host option): off by default; on/off (Phase 336: only these,
     * lower case, as every switch). */
    CHECK(load("[display]\nscale = 3\n",&c,error)&&!c.locked_scale);
    CHECK(load("[display]\nlocked_scale = on\n",&c,error)&&c.locked_scale==1);
    CHECK(!load("[display]\nlocked_scale = true\n",&c,error)&&strstr(error,"must be on or off: locked_scale"));
    CHECK(!load("[display]\nlocked_scale = True\n",&c,error)&&strstr(error,"must be on or off")); /* as every switch */
    CHECK(load("[display]\nlocked_scale = off\n",&c,error)&&!c.locked_scale);
    CHECK(!load("[display]\nlocked_scale = 1\n",&c,error)&&strstr(error,"must be on or off: locked_scale"));
    CHECK(!load("[files]\nlocked_scale = on\n",&c,error)&&strstr(error,"locked_scale belongs in [display]"));
    CHECK(!load("[files]\nscale = 2\n",&c,error)&&strstr(error,"scale belongs in [display]"));
    CHECK(!load("[display]\ndata_dir = x\n",&c,error)&&strstr(error,"data_dir belongs in [files]"));
    CHECK(!load("[display]\nfullscreen = 1\n",&c,error)&&strstr(error,"must be on or off"));
    /* Phase 287: [files] save_dir: auto (the default, "."), explicit paths relative to the file (spaces, quotes, a
     * trailing separator kept), absolute, ./auto; the section, duplicates; the directory check (never created). */
    CHECK(load("[display]\nscale = 3\n",&c,error)&&c.save_dir_auto&&!strcmp(c.save_dir,"."));
    CHECK(load("[files]\nsave_dir = auto\n",&c,error)&&c.save_dir_auto&&!strcmp(c.save_dir,"."));
    CHECK(load("[files]\nsave_dir =\n",&c,error)&&c.save_dir_auto);
    CHECK(load("[files]\nsave_dir = my saves\n",&c,error)&&!c.save_dir_auto);
    snprintf(expect,sizeof expect,"%s/my saves",root);CHECK(!strcmp(c.save_dir,expect));
    CHECK(load("[files]\nsave_dir = \"x y/\"\n",&c,error)&&!c.save_dir_auto);
    snprintf(expect,sizeof expect,"%s/x y/",root);CHECK(!strcmp(c.save_dir,expect));
    CHECK(load("[files]\nsave_dir = ./auto\n",&c,error)&&!c.save_dir_auto);
#if !defined(_WIN32)
    CHECK(load("[files]\nsave_dir = /abs/saves\n",&c,error)&&!strcmp(c.save_dir,"/abs/saves"));
#else
    CHECK(load("[files]\nsave_dir = D:\\Might and Magic 4-5\\saves\n",&c,error)&&
          !strcmp(c.save_dir,"D:\\Might and Magic 4-5\\saves"));
#endif
    CHECK(load("[files]\ndata_dir = auto\nsave_dir = auto\n",&c,error)&&c.data_auto&&c.save_dir_auto); /* independent */
    CHECK(!load("[display]\nsave_dir = x\n",&c,error)&&strstr(error,"save_dir belongs in [files]"));
    CHECK(!load("[files]\nsave_dir = a\nsave_dir = b\n",&c,error)&&strstr(error,"duplicate setting"));
    {char why[300],dir[1100],file[1100];FILE *f;struct stat st;
     snprintf(dir,sizeof dir,"%s/w dir",root);mkdir_one(dir);
     CHECK(xeen_wox_save_dir_check(dir,why,sizeof why));
     snprintf(file,sizeof file,"%s/w dir/",root);CHECK(xeen_wox_save_dir_check(file,why,sizeof why)); /* trailing / */
     snprintf(file,sizeof file,"%s/absent",root);
     CHECK(!xeen_wox_save_dir_check(file,why,sizeof why)&&strstr(why,"not an existing directory"));
     CHECK(stat(file,&st)!=0); /* not created */
     snprintf(file,sizeof file,"%s/plain",root);f=fopen(file,"wb");if(f)fclose(f);
     CHECK(!xeen_wox_save_dir_check(file,why,sizeof why)&&strstr(why,"not an existing directory"));
     CHECK(!xeen_wox_save_dir_check("",why,sizeof why));
#if !defined(_WIN32)
     if(geteuid()!=0){snprintf(file,sizeof file,"%s/ro",root);mkdir_one(file);chmod(file,0500);
      CHECK(!xeen_wox_save_dir_check(file,why,sizeof why)&&strstr(why,"not writable"));chmod(file,0700);}
#endif
    }
    /* Phase 287: [files] xeen_cfg_dir (Phase 336; XEEN_CFG_location before): auto (the default), a directory
     * relative to the file, absolute; other spellings and capitalizations are unknown keys, as for every key. */
    CHECK(load("[display]\nscale = 3\n",&c,error)&&c.xeen_cfg_auto);
    CHECK(load("[files]\nxeen_cfg_dir = auto\n",&c,error)&&c.xeen_cfg_auto);
    CHECK(load("[files]\nxeen_cfg_dir = \"cfg dir\"\n",&c,error)&&!c.xeen_cfg_auto);
    snprintf(expect,sizeof expect,"%s/cfg dir",root);CHECK(!strcmp(c.xeen_cfg_dir,expect));
#if !defined(_WIN32)
    CHECK(load("[files]\nxeen_cfg_dir = /games/xeen\n",&c,error)&&!strcmp(c.xeen_cfg_dir,"/games/xeen"));
#else
    CHECK(load("[files]\nxeen_cfg_dir = D:\\Might and Magic 4-5\n",&c,error)&&
          !strcmp(c.xeen_cfg_dir,"D:\\Might and Magic 4-5"));
#endif
    CHECK(!load("[files]\nxeen_cfg_location = x\n",&c,error)&&strstr(error,"unknown setting"));
    CHECK(!load("[files]\nXEEN_CFG_DIR = x\n",&c,error)&&strstr(error,"unknown setting"));
    CHECK(!load("[files]\nxeen-cfg-dir = x\n",&c,error)&&strstr(error,"unknown setting: xeen-cfg-dir"));
    CHECK(!load("[audio]\nxeen_cfg_dir = x\n",&c,error)&&strstr(error,"belongs in [files]"));
    CHECK(!load("[files]\nxeen_cfg_dir = a\nxeen_cfg_dir = b\n",&c,error)&&strstr(error,"duplicate"));
    CHECK(load("[files]\ndata_dir = game\nsave_dir = s\nxeen_cfg_dir = auto\n",&c,error)&&!c.data_auto&&
          !c.save_dir_auto&&c.xeen_cfg_auto); /* independent: auto is resolved by the host from data */
    /* Phase 287: the generated file: [files] in the order data_dir, save_dir, xeen_cfg_dir, then the ROM and CD-rip
     * directories; it loads to the defaults (every new key auto). */
    {char path[1100],text[16384];size_t got;FILE *f;
     snprintf(path,sizeof path,"%s/created.cfg",root);remove(path);
     CHECK(xeen_wox_config_create(path)&&(f=fopen(path,"rb"))!=NULL);
     got=f?fread(text,1,sizeof text-1u,f):0u;if(f)fclose(f);text[got]=0;
     {const char *files=strstr(text,"\n[files]\n"),*end=files?strstr(files+1,"\n[display]\n"):NULL;
      static const char *const order[]={"\ndata_dir = auto\n","\nsave_dir = auto\n","\nxeen_cfg_dir = auto\n",
                                        "\nmt32_roms_dir = auto\n","\nsc55_roms_dir = auto\n","\ncd_tracks_dir = auto\n"};
      const char *at=files;
      CHECK(files&&end);
      for(unsigned i=0;at&&end&&i<sizeof order/sizeof *order;++i){const char *k=strstr(at,order[i]);CHECK(k&&k<end);at=k;}}
     CHECK(xeen_wox_config_load(path,&c,error,512)&&c.data_auto&&c.save_dir_auto&&c.xeen_cfg_auto&&
           c.system==XEEN_MUSIC_EXTERNAL&&c.speech==XEEN_SPEECH_AUTO);
     /* Phase 336 (owner decision): every setting line of the generated file spells its key with '_' only (no '-')
      * and no value is true or false; music_override and the [files] *_dir keys are there. */
     {unsigned settings=0;
      for(char *line=strtok(text,"\n");line;line=strtok(NULL,"\n")){char *eq=strchr(line,'=');
       if(*line=='#'||*line=='['||!eq)continue;++settings;
       for(char *q=line;q<eq;++q)CHECK(*q!='-');
       CHECK(!strstr(eq,"true")&&!strstr(eq,"false"));}
      CHECK(settings>=40u);}
     CHECK(strstr(xeen_wox_config_default_text(),"\nmusic_override = system\n")&&
           strstr(xeen_wox_config_default_text(),"\nmt32_roms_dir = auto\nsc55_roms_dir = auto\n")&&
           strstr(xeen_wox_config_default_text(),"\ncd_tracks_dir = auto\n")&&
           strstr(xeen_wox_config_default_text(),"Any other\n# value overrides XEEN.CFG's choice"));}
    /* Lookup: explicit path, else beside the executable. */
    {char out[1100];
     CHECK(xeen_wox_config_locate("x/y.cfg",NULL,out,sizeof out)&&!strcmp(out,"x/y.cfg"));
     CHECK(xeen_wox_config_locate(NULL,"ignored",out,sizeof out));
     CHECK(strlen(out)>8u&&!strcmp(out+strlen(out)-8u,"/wox.cfg"));}
    if(failures)fprintf(stderr,"%d failure(s)\n",failures);
    else puts("phase132 wox.cfg: PASS");
    return failures?1:0;
}
