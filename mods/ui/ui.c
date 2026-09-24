#include "ui.h"
#include "stem_pads.h"
#include "native_mixer_eq.h"
#include "font_atlas.h"
#include <stdio.h>
#include <string.h>

struct palette { uint32_t bg,ink,accent,alarm,stem[3]; };
/* Authored seed colors from upstream presets.c; see PROVENANCE.md. */
static const struct palette palettes[7] = {
 {0x000000,0xf4f5f6,0xa8ceff,0xffa000,{0xff3b30,0x2997ff,0x30d158}},
 {0xf0f0f0,0x141414,0x176398,0xa35400,{0xc52727,0x125eae,0x16753d}},
 {0x00060e,0xdff3f7,0x54c1e6,0xfee801,{0xff2e88,0x54c1e6,0x2bf58a}},
 {0x0b0d17,0xc9d1d9,0x00e5ff,0xff9100,{0xff2daa,0x00e5ff,0x7c4dff}},
 {0x1e1e2e,0xcdd6f4,0xcba6f7,0xfab387,{0xf38ba8,0x89b4fa,0xa6e3a1}},
 {0x141414,0xf0fef9,0x00e575,0xd451ff,{0xff4fc3,0x006afb,0x00e575}},
 {0xfbf0d9,0x262a44,0x393f61,0xfdb03f,{0xe8705d,0x393f61,0x869a5f}}
};
static const char *themes[7]={"ORIGINAL","WHITE","CYBERPUNK","NEON","MOCHA","AURORA","SANDSTONE"};
static const char *pages[XZ_UI_PAGE_COUNT]={"STEMS / GC","X-PAD","SETTINGS","THEMES","VJ.TOOLS","CONTROLS"};
static const char *stem_pages[4]={"HOT CUE","BEAT LOOP","SLIP LOOP","BEAT JUMP"};
static const char *stems[3]={"DRUMS","HARMONICS","VOCALS"};
static const char *pads[8]={"A","B","C","D","E","F","G","H"};
static const char *lengths[6]={"1/16","1/8","1/4","1/2","1","2"};
static float clampf(float v,float lo,float hi){return v<lo?lo:v>hi?hi:v;}
static uint16_t rgb(uint32_t c){return (uint16_t)(((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31));}
static uint32_t blend(uint32_t a,uint32_t b,unsigned n){
 unsigned r=(((a>>16)&255)*(256-n)+((b>>16)&255)*n)>>8;
 unsigned g=(((a>>8)&255)*(256-n)+((b>>8)&255)*n)>>8;
 unsigned v=((a&255)*(256-n)+(b&255)*n)>>8;return (r<<16)|(g<<8)|v;
}
struct canvas {uint16_t *p;size_t stride;int width,height;};
static void rect(struct canvas c,int x,int y,int w,int h,uint32_t col){
 int xx,yy;if(x<0){w+=x;x=0;}if(y<0){h+=y;y=0;}
 if(x+w>c.width)w=c.width-x;if(y+h>c.height)h=c.height-y;
 for(yy=y;yy<y+h;yy++)for(xx=x;xx<x+w;xx++)c.p[(size_t)yy*c.stride+(size_t)xx]=rgb(col);
}
static unsigned codepoint(const char **cursor){
 const unsigned char *s=(const unsigned char *)*cursor;unsigned ch=*s++;
 if(ch>=0xc2&&ch<=0xf4){
  unsigned need=ch<0xe0?1:ch<0xf0?2:3,value=ch&((1u<<(6-need))-1);
  for(unsigned i=0;i<need;i++){
   if((*s&0xc0)!=0x80){*cursor=(const char *)s;return '?';}
   value=(value<<6)|(*s++&63);
  }
  ch=value;
 }
 *cursor=(const char *)s;
 return ch>=32&&ch<=255?ch:'?';
}
static void letter(struct canvas c,int x,int y,const struct xz_font_glyph *g,uint32_t color){
 for(unsigned row=0;row<g->height;row++)for(unsigned col=0;col<g->width;col++){
  int px=x+g->x+(int)col,py=y+g->y+(int)row;
  if(px<0||py<0||px>=c.width||py>=c.height)continue;
  unsigned a=xz_font_coverage[g->offset+row*g->width+col];if(!a)continue;
  uint16_t *pixel=&c.p[(size_t)py*c.stride+(size_t)px];
  if(a==255){*pixel=rgb(color);continue;}
  unsigned old=*pixel,r=((old>>11)&31)*255/31,green=((old>>5)&63)*255/63,b=(old&31)*255/31;
  r=(r*(255-a)+((color>>16)&255)*a+127)/255;
  green=(green*(255-a)+((color>>8)&255)*a+127)/255;
  b=(b*(255-a)+(color&255)*a+127)/255;
  *pixel=rgb((r<<16)|(green<<8)|b);
 }
}
static void text(struct canvas c,int x,int y,const char *s,int scale,int limit,uint32_t color){
 if(!s||limit<=0)return;
 int face=scale>1?1:0,end=x+limit*6*scale;
 while(*s){
  unsigned ch=codepoint(&s);const struct xz_font_glyph *g=&xz_font_glyphs[face][ch-32];
  if(x+g->advance>end)break;
  letter(c,x,y,g,color);x+=g->advance;
 }
}
static void border(struct canvas c,int x,int y,int w,int h,uint32_t color){
 rect(c,x,y,w,1,color);rect(c,x,y+h-1,w,1,color);
 rect(c,x,y,1,h,color);rect(c,x+w-1,y,1,h,color);
}
static void deck_card(struct canvas c,const struct xz_ui_widget *a,int selected,const struct palette *p){
 uint32_t edge=selected?p->accent:blend(p->bg,p->ink,112);
 for(int row=0;row<a->h;row++){
  int width=a->w-9+row*9/(a->h-1);
  uint32_t fill=blend(p->bg,selected?p->accent:p->ink,selected?48-row/2:34-row/3);
  rect(c,a->x,a->y+row,width,1,fill);rect(c,a->x+width-1,a->y+row,1,1,edge);
 }
 rect(c,a->x,a->y,a->w-9,1,edge);rect(c,a->x,a->y+a->h-1,a->w,1,edge);
 rect(c,a->x,a->y,1,a->h,edge);rect(c,a->x+5,a->y+6,selected?3:1,20,selected?p->ink:edge);
}
void xz_ui_render_badge(uint16_t *pixels,size_t stride){
 if(!pixels||stride<800)return;
 struct canvas c={pixels,stride,800,480};
 rect(c,744,0,56,24,0x15191e);border(c,744,0,56,24,0xa8ceff);
 text(c,751,5,"MODS",2,4,0xf4f5f6);
}
void xz_ui_render_stems_button(uint16_t *pixels,size_t stride,int enabled){
 if(!pixels||stride<800)return;
 struct canvas c={pixels,stride,800,480};uint32_t color=enabled?0x52d794:0xa8ceff;
 rect(c,674,0,68,24,enabled?0x123c2a:0x15191e);border(c,674,0,68,24,color);
 text(c,681,5,"STEMS",2,5,color);
}
void xz_ui_render_vj_button(uint16_t *pixels,size_t stride,int takeover_active){
 if(!pixels||stride<800)return;
 struct canvas c={pixels,stride,800,480};
 uint32_t border_col=takeover_active?0x52d794:0xa8ceff;
 rect(c,0,0,112,24,0x15191e);border(c,0,0,112,24,border_col);
 text(c,7,5,takeover_active?"EXIT VJ":"VJ.TOOLS",2,8,takeover_active?0x52d794:0xf4f5f6);
}
static void add(struct xz_ui_widget *w,size_t *n,int x,int y,int width,int height,
 enum xz_ui_action_kind kind,int index,uint32_t cap,const char *label){
 w[*n]=(struct xz_ui_widget){x,y,width,height,kind,index,cap,label};(*n)++;
}
const char *xz_ui_theme_name(int theme){return themes[theme>=0&&theme<7?theme:0];}
uint32_t xz_ui_stem_color(int theme,int index){
 return palettes[theme>=0&&theme<7?theme:0].stem[index>=0&&index<3?index:0];
}
void xz_ui_init(struct xz_ui *u){memset(u,0,sizeof(*u));u->capture=-1;}
size_t xz_ui_layout(const struct xz_ui *u,const struct xz_ui_model *m,struct xz_ui_widget w[XZ_UI_WIDGETS]){
 size_t n=0;int i;
 for(i=0;i<4;i++)add(w,&n,12+i*170,40,162,42,XZ_UI_DECK,i,0,i<2?"INTERNAL USB":"PC / HYBRID");
 add(w,&n,700,40,88,42,XZ_UI_CLOSE,0,0,"CLOSE");
 for(i=0;i<XZ_UI_PAGE_COUNT;i++)add(w,&n,12+i*130,92,126,42,XZ_UI_PANEL,i,0,pages[i]);
 if(u->page==XZ_UI_STEMS){
  add(w,&n,12,224,98,52,XZ_UI_BYPASS,0,XZ_UI_STEM,"BYPASS");
  for(i=0;i<3;i++){
   add(w,&n,122+i*224,224,212,38,XZ_UI_MUTE,xz_stem_for_pad(i),XZ_UI_STEM,stems[xz_stem_for_pad(i)]);
   add(w,&n,122+i*224,266,212,42,XZ_UI_LEVEL,xz_stem_for_pad(i),XZ_UI_STEM,"");
  }
  for(i=0;i<8;i++){
   int assigned=(m->deck[u->deck].groove_assigned&(1u<<i))!=0;
   add(w,&n,12+i*98,352,90,64,assigned?XZ_UI_GROOVE_PAD:XZ_UI_HOTCUE_PAD,i,
       assigned?(XZ_UI_GROOVE|XZ_UI_STEM):XZ_UI_HOTCUE,pads[i]);
  }
 }else if(u->page==XZ_UI_XPAD){
  add(w,&n,12,224,576,144,XZ_UI_STRIP,0,XZ_UI_SAMPLE,"");
  add(w,&n,600,224,90,42,XZ_UI_HOLD,0,XZ_UI_SAMPLE,"HOLD");
  add(w,&n,698,224,90,42,XZ_UI_OVERDUB,0,XZ_UI_SAMPLE,"OVERDUB");
  add(w,&n,600,306,188,42,XZ_UI_VOLUME,0,XZ_UI_SAMPLE,"");
  for(i=0;i<8;i++)add(w,&n,12+i*98,386,90,42,XZ_UI_SAMPLE_PAD,i,XZ_UI_SAMPLE,pads[i]);
 }else if(u->page==XZ_UI_SETTINGS){
  static const char *labels[5]={"GATE CUE","SMART CUE","PREVIEW HOTCUE","ENABLE STEMS","ENABLE X-PAD"};
  static const uint32_t flags[5]={XZ_UI_GATE,XZ_UI_SMART,XZ_UI_PREVIEW,XZ_UI_STEM,XZ_UI_SAMPLE};
  for(i=0;i<5;i++)add(w,&n,12,152+i*50,366,42,XZ_UI_ENABLE,(int)flags[i],flags[i],labels[i]);
  add(w,&n,12,402,366,42,XZ_UI_PANEL,XZ_UI_THEMES,0,"THEME");
  add(w,&n,398,152,390,42,XZ_UI_SERVER_AUTO,0,XZ_UI_SERVER,"STEM SERVER LOCATION");
  add(w,&n,398,204,390,42,XZ_UI_SERVER_ADDRESS,0,XZ_UI_SERVER,"STEM SERVER ADDRESS");
  add(w,&n,398,280,86,46,XZ_UI_KEY_SHIFT,-1,XZ_UI_KEY,"KEY -");
  add(w,&n,492,280,80,46,XZ_UI_KEY_SHIFT,0,XZ_UI_KEY,"RESET");
  add(w,&n,580,280,86,46,XZ_UI_KEY_SHIFT,1,XZ_UI_KEY,"KEY +");
  add(w,&n,674,280,114,46,XZ_UI_KEY_SYNC,0,XZ_UI_KEYSYNC,"KEY SYNC");
 }else if(u->page==XZ_UI_CONTROLS){
  add(w,&n,12,228,180,36,XZ_UI_STEM_BANK,0,0,"STEMS ON A-D");
  add(w,&n,202,228,186,36,XZ_UI_STEM_BANK,1,0,"STEMS ON E-H");
  for(i=0;i<4;i++)add(w,&n,12+i*196,174,188,44,XZ_UI_STEM_PAGE,i,0,stem_pages[i]);
  add(w,&n,12,274,376,44,XZ_UI_SHIFT_PAGES,0,0,"SHIFT + PAGE BUTTONS");
  add(w,&n,12,328,376,44,XZ_UI_PAD_FEEDBACK,0,0,"PAD COLOR FEEDBACK");
  add(w,&n,12,382,376,44,XZ_UI_SHIFT_KEYSYNC,0,0,"SHIFT + SYNC");
  add(w,&n,406,250,382,48,XZ_UI_SPARE_EQ,0,0,"SPARE CHANNEL STEM EQ");
 }else if(u->page==XZ_UI_THEMES){
  for(i=0;i<7;i++)add(w,&n,12+(i%3)*262,158+(i/3)*88,252,72,XZ_UI_SET_THEME,i,XZ_UI_THEME,themes[i]);
 }else if(u->page==XZ_UI_CONNECTION){
  add(w,&n,492,142,296,42,XZ_UI_TAKEOVER_TOGGLE,0,0,"VJ.TOOLS VIEW");
  add(w,&n,492,238,94,40,XZ_UI_TAKEOVER_ASSIGN,0,0,"LINK");
  add(w,&n,592,238,96,40,XZ_UI_TAKEOVER_ASSIGN,1,0,"REKORDBOX");
  add(w,&n,694,238,94,40,XZ_UI_TAKEOVER_ASSIGN,2,0,"ONSCREEN");
  add(w,&n,492,290,296,44,XZ_UI_CONNECTION_ENABLE,0,XZ_UI_VJ_CONNECTION,"VJ CONNECTION");
  add(w,&n,492,346,296,44,XZ_UI_DISCOVERY,0,XZ_UI_VJ_DISCOVERY,"DISCOVERABLE");
 }
 return n;
}
static int ready(const struct xz_ui_model *m,int deck,const struct xz_ui_widget *w){
 const struct xz_ui_deck *d=&m->deck[deck];
 if(w->kind==XZ_UI_SPARE_EQ)return m->eq_available||m->spare_eq;
 if(w->kind==XZ_UI_CONNECTION_ENABLE)return m->connection.ready&&m->connection.can_enable;
 if(w->kind==XZ_UI_DISCOVERY)return m->connection.ready&&m->connection.can_discover;
 uint32_t available=d->ready;
 if(w->kind==XZ_UI_ENABLE&&(w->index==XZ_UI_STEM||w->index==XZ_UI_GATE||w->index==XZ_UI_SMART))
  available=m->deck[0].ready|m->deck[1].ready;
 if(w->requires && (available&w->requires)!=w->requires)return 0;
 switch(w->kind){
 case XZ_UI_LEVEL:case XZ_UI_MUTE:case XZ_UI_BYPASS:return (m->enabled&XZ_UI_STEM)!=0;
 case XZ_UI_GROOVE_PAD:return (m->enabled&XZ_UI_STEM)&&(d->groove_loaded&(1u<<w->index));
 case XZ_UI_SAMPLE_PAD:return (m->enabled&XZ_UI_SAMPLE)&&(d->sample_loaded&(1u<<w->index));
 case XZ_UI_STRIP:case XZ_UI_HOLD:case XZ_UI_OVERDUB:case XZ_UI_VOLUME:return (m->enabled&XZ_UI_SAMPLE)!=0;
 default:return 1;}
}
int xz_ui_render(const struct xz_ui *u,const struct xz_ui_model *m,uint16_t *pixels,size_t count,size_t stride){
 struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n,i;struct canvas c;const struct palette *p;const struct xz_ui_deck *d;uint32_t panel,dim;char buf[96];
 if(!u||!m||!pixels||u->deck<0||u->deck>3||stride<800||stride>count/480)return 0;
 c=(struct canvas){pixels,stride,800,480};p=&palettes[m->theme>=0&&m->theme<7?m->theme:0];d=&m->deck[u->deck];
 panel=blend(p->bg,p->ink,28);dim=blend(p->bg,p->ink,154);rect(c,0,0,800,480,p->bg);
 rect(c,0,0,800,30,blend(p->bg,p->ink,15));rect(c,0,29,800,1,blend(p->bg,p->ink,90));
 text(c,12,7,"XZ MODS",2,15,p->ink);text(c,108,10,"XDJ-XZ",1,20,dim);
 text(c,691,9,"VJ.Tools",1,16,p->accent);
 n=xz_ui_layout(u,m,w);
 for(i=0;i<n;i++){
  struct xz_ui_widget a=w[i];int available=ready(m,u->deck,&a),selected=0;uint32_t color=p->accent;
  if(a.kind==XZ_UI_DECK)selected=a.index==u->deck;
  if(a.kind==XZ_UI_PANEL)selected=a.index==(int)u->page;
  if(a.kind==XZ_UI_SET_THEME)selected=a.index==m->theme;
  if(a.kind==XZ_UI_STEM_PAGE)selected=a.index==m->stem_page;
  if(a.kind==XZ_UI_SHIFT_PAGES)selected=m->shift_pages;
  if(a.kind==XZ_UI_PAD_FEEDBACK)selected=m->pad_feedback;
  if(a.kind==XZ_UI_SHIFT_KEYSYNC)selected=m->shift_keysync;
  if(a.kind==XZ_UI_SPARE_EQ)selected=m->spare_eq;
  if(a.kind==XZ_UI_STEM_BANK)selected=m->stem_bank==a.index;
  if(a.kind==XZ_UI_TAKEOVER_TOGGLE){color=0x52d794;selected=m->fb_takeover!=0;}
  if(a.kind==XZ_UI_TAKEOVER_ASSIGN)selected=a.index==m->takeover_assign;
  if(a.kind==XZ_UI_BYPASS)selected=d->bypass;
  if(a.kind==XZ_UI_HOLD)selected=d->hold;
  if(a.kind==XZ_UI_OVERDUB)selected=d->overdub;
  if(a.kind==XZ_UI_ENABLE)selected=(m->enabled&(uint32_t)a.index)!=0;
  if(a.kind==XZ_UI_CONNECTION_ENABLE)selected=m->connection.ready&&m->connection.enabled;
  if(a.kind==XZ_UI_DISCOVERY)selected=m->connection.ready&&m->connection.discoverable;
  if(a.kind==XZ_UI_MUTE){color=p->stem[a.index];selected=(d->muted&(1u<<a.index))!=0;}
  if(a.kind==XZ_UI_GROOVE_PAD){color=p->stem[d->groove_stem[a.index]<3?d->groove_stem[a.index]:0];selected=d->groove_active==a.index&&m->blink;}
  if(a.kind==XZ_UI_SAMPLE_PAD)selected=d->sample_active==a.index;
  if(a.kind!=XZ_UI_DECK){
   rect(c,a.x,a.y,a.w,a.h,available?(selected?blend(panel,color,38):panel):blend(p->bg,p->ink,14));
   border(c,a.x,a.y,a.w,a.h,available&&selected?color:blend(p->bg,p->ink,102));
  }
  if(a.kind==XZ_UI_PANEL)rect(c,a.x,a.y+a.h-3,a.w,3,selected?p->accent:blend(p->bg,p->ink,58));
  if(a.kind==XZ_UI_MUTE||a.kind==XZ_UI_GROOVE_PAD)rect(c,a.x,a.y,a.w,3,available?color:dim);
  if(a.kind==XZ_UI_PANEL&&a.index==XZ_UI_STEMS&&(m->enabled&XZ_UI_STEM)&&!d->bypass&&
      (d->levels[0]<1||d->levels[1]<1||d->levels[2]<1||d->muted||d->groove_active>=0))
      rect(c,a.x+a.w-8,a.y+7,4,4,p->alarm);
  if(a.kind==XZ_UI_DECK){
   deck_card(c,&a,selected,p);snprintf(buf,sizeof(buf),"DECK %d",a.index+1);
   text(c,a.x+14,a.y+5,buf,2,12,p->ink);text(c,a.x+14,a.y+27,a.label,1,22,dim);
  }
  else if(a.kind==XZ_UI_LEVEL||a.kind==XZ_UI_VOLUME){
   float value=a.kind==XZ_UI_LEVEL?d->levels[a.index]:d->sample_volume;
   int position=(int)(clampf(value,0,1)*(float)(a.w-16));
   uint32_t bar=a.kind==XZ_UI_LEVEL?p->stem[a.index]:p->accent;
   if(a.kind==XZ_UI_LEVEL&&(d->muted&(1u<<a.index)))bar=dim;
   rect(c,a.x+8,a.y+27,a.w-16,2,dim);rect(c,a.x+8,a.y+27,position,2,available?bar:dim);
   for(int tick=0;tick<5;tick++)rect(c,a.x+8+(a.w-16)*tick/4,a.y+24,1,8,dim);
   rect(c,a.x+5+position,a.y+20,6,16,available?p->ink:dim);
   rect(c,a.x+5+position,a.y+20,6,3,available?bar:dim);
   snprintf(buf,sizeof(buf),"%d%%",(int)(clampf(value,0,1)*100+.5f));text(c,a.x+8,a.y+5,buf,1,10,p->ink);
  }else if(a.kind==XZ_UI_STRIP){
   int k;for(k=0;k<6;k++){rect(c,a.x+k*96,a.y,2,a.h,dim);text(c,a.x+k*96+20,a.y+8,lengths[k],2,6,p->ink);}
   rect(c,a.x,a.y+72,a.w,1,dim);text(c,a.x+8,a.y+38,"+12",1,5,dim);text(c,a.x+8,a.y+118,"-12",1,5,dim);
   if(d->loop_index>=0&&d->loop_index<6){int py=(int)((12-clampf(d->pitch,-12,12))*5.95833f);rect(c,a.x+d->loop_index*96+4,a.y+py,88,3,available?p->accent:dim);}
  }else if(a.kind==XZ_UI_KEY_SHIFT&&a.index==0){
   if(available)snprintf(buf,sizeof(buf),"%+d",d->key_semitones);else snprintf(buf,sizeof(buf),"--");
   text(c,a.x+8,a.y+6,buf,2,5,available?p->ink:dim);
   if(available)text(c,a.x+8,a.y+30,"RESET",1,8,dim);
  }else{
   int scale=(a.kind==XZ_UI_OVERDUB)?1:2;
   const char *label=a.label;
   text(c,a.x+8,a.y+8,label,scale,(a.w-16)/(6*scale),available?p->ink:dim);
   if(a.kind==XZ_UI_MUTE&&selected){
    rect(c,a.x+a.w-63,a.y+10,55,17,p->alarm);
    text(c,a.x+a.w-58,a.y+13,"MUTED",1,9,p->bg);
   }
   if(a.kind==XZ_UI_SHIFT_PAGES||a.kind==XZ_UI_PAD_FEEDBACK||a.kind==XZ_UI_SHIFT_KEYSYNC||a.kind==XZ_UI_TAKEOVER_TOGGLE||a.kind==XZ_UI_SPARE_EQ)
    text(c,a.x+a.w-40,a.y+18,selected?"ON":"OFF",1,6,selected?p->accent:dim);
   if(a.kind==XZ_UI_ENABLE)text(c,a.x+a.w-78,a.y+16,!available?"NOT READY":selected?"ON":"OFF",1,12,available&&selected?p->accent:dim);
   if(a.kind==XZ_UI_CONNECTION_ENABLE||a.kind==XZ_UI_DISCOVERY)
    text(c,a.x+8,a.y+34,m->connection.ready?(selected?"ON":"OFF"):"UNAVAILABLE",1,20,selected?p->accent:dim);
   if(a.kind==XZ_UI_GROOVE_PAD)text(c,a.x+8,a.y+31,available?(d->groove_active==a.index?"PLAYING":"READY"):"NOT READY",1,12,available?color:dim);
   if(a.kind==XZ_UI_HOTCUE_PAD)text(c,a.x+8,a.y+31,"HOT CUE",1,12,dim);
   if(a.kind==XZ_UI_SERVER_AUTO)text(c,a.x+8,a.y+27,m->server_auto?"AUTO":"MANUAL",1,30,p->accent);
   if(a.kind==XZ_UI_SERVER_ADDRESS)text(c,a.x+8,a.y+27,m->server_address&&*m->server_address?m->server_address:"NOT SET",1,58,dim);
  }
  if(!available&&a.kind!=XZ_UI_GROOVE_PAD&&a.kind!=XZ_UI_ENABLE)
   text(c,a.x+a.w-64,a.kind==XZ_UI_LEVEL||a.kind==XZ_UI_VOLUME?a.y+5:a.y+a.h-12,
        a.kind==XZ_UI_CONNECTION_ENABLE||a.kind==XZ_UI_DISCOVERY?"READ ONLY":"NOT READY",1,10,dim);
 }
 if(u->page==XZ_UI_STEMS||u->page==XZ_UI_XPAD){
  rect(c,12,140,776,27,blend(p->bg,p->alarm,122));border(c,12,140,776,27,p->alarm);
  snprintf(buf,sizeof(buf),"%s",d->track?d->track:(u->deck<2?"NO TRACK LOADED":"COMPUTER DECK / EXTERNAL AUDIO"));text(c,20,145,buf,2,51,p->ink);
  if(d->bpm>0)snprintf(buf,sizeof(buf),"%.1f BPM",(double)d->bpm);else snprintf(buf,sizeof(buf),"---.- BPM");
  text(c,692,150,buf,1,15,p->ink);
  rect(c,12,197,776,1,blend(p->bg,p->ink,40));
  if(d->wave_peaks&&d->wave_count){size_t j;for(j=0;j<d->wave_count&&j<388;j++){int x=12+(int)(j*776/d->wave_count);int h=d->wave_peaks[j]*40/255;rect(c,x,198-h/2,2,h,p->accent);}}
  else {rect(c,295,186,212,22,p->bg);text(c,306,191,"WAVEFORM NOT AVAILABLE",1,35,dim);}
  if(u->page==XZ_UI_STEMS){text(c,12,317,"GROOVE CIRCUIT",2,28,p->ink);text(c,190,322,"BANK A-H",1,20,dim);text(c,12,338,"TAP TO REPLACE A STEM / TAP AGAIN TO RELEASE",1,100,dim);snprintf(buf,sizeof(buf),"%s%s    %s",m->stem_page>0?"HOT CUE + ":"",stem_pages[m->stem_page>=0&&m->stem_page<4?m->stem_page:0],m->stem_bank?"E VOCALS / F HARMONICS / G DRUMS / H BYPASS":"A VOCALS / B HARMONICS / C DRUMS / D BYPASS");text(c,12,428,buf,1,115,dim);}
  else{snprintf(buf,sizeof(buf),"%s BEATS / %+.1f KEY",lengths[d->loop_index>=0&&d->loop_index<6?d->loop_index:0],(double)d->pitch);text(c,600,279,buf,1,31,p->alarm);text(c,600,357,"VOL / HOLD LATCHES",1,30,dim);text(c,12,372,"SAMPLE BANK A-H / CLOSING X-PAD STOPS SOUND",1,90,dim);}
 }else if(u->page==XZ_UI_SETTINGS){text(c,398,258,"KEY CONTROL",1,62,dim);text(c,398,348,"STEM AND CUE SETTINGS APPLY TO BOTH DECKS",1,62,p->ink);text(c,398,369,"UNAVAILABLE CONTROLS ARE MARKED NOT READY",1,62,dim);}
 else if(u->page==XZ_UI_CONTROLS){
  text(c,12,149,"ADDITIONAL STEMS PAD PAGE",2,40,p->ink);
  text(c,406,230,m->stem_bank?"E VOCALS / F MUSIC / G DRUMS / H BYPASS":"A VOCALS / B MUSIC / C DRUMS / D BYPASS",1,63,dim);
  text(c,406,311,"CH3 -> DECK 1 / CH4 -> DECK 2",1,63,p->ink);
  text(c,406,331,"HIGH VOCALS / MID HARMONICS / LOW DRUMS",1,63,dim);
  const char *eq_status=m->eq_status==XZ_EQ_ACTIVE?"ACTIVE - TURN KNOB THROUGH CURRENT LEVEL":m->eq_status==XZ_EQ_SOURCE?"LOAD USB TRACKS / LINK OR PC SUSPENDS EQ":m->eq_status==XZ_EQ_EXTERNAL?"SUSPENDED: SELECT PC ON SPARE CHANNELS":m->eq_status==XZ_EQ_WAITING?"WAITING: MOVE A SPARE EQ KNOB":m->eq_status==XZ_EQ_UNAVAILABLE?"EQ DISABLED: SAFE MIDI INPUT REQUIRED":"OFF";
  text(c,406,353,eq_status,1,63,p->accent);
  text(c,406,374,"CENTRE = FULL / LEFT = SILENT / NO BOOST",1,63,dim);
  text(c,406,395,"EXTERNAL INPUTS SUSPEND THEIR STEM CONTROL",1,63,dim);
  text(c,12,430,m->settings_status?m->settings_status:"INSERT USB TO SAVE SETTINGS",1,115,p->ink);
 }
 else if(u->page==XZ_UI_THEMES)text(c,282,361,"ORIGINAL + SIX UPSTREAM THEMES",1,70,dim);
 else if(u->page==XZ_UI_CONNECTION){
  const struct xz_ui_connection *v=&m->connection;
  text(c,12,152,"VJ.TOOLS CONNECTION",2,50,p->ink);
  text(c,12,177,"OPTIONAL COMPUTER LINK / DJ FEATURES WORK STANDALONE",1,100,dim);
  rect(c,12,194,464,204,panel);
  text(c,24,210,"STATUS",1,20,dim);
  text(c,144,207,!v->ready?"NOT AVAILABLE":v->connected?"CONNECTED":v->enabled?"WAITING FOR VJ.TOOLS":"DISABLED",2,27,v->ready&&v->connected?p->accent:dim);
  text(c,24,246,"DEVICE IP",1,20,dim);
  text(c,144,242,v->device_ip&&*v->device_ip?v->device_ip:"NOT REPORTED",2,26,p->ink);
  text(c,24,278,"PORT",1,20,dim);
  if(v->port)snprintf(buf,sizeof(buf),"%u",v->port);else snprintf(buf,sizeof(buf),"NOT REPORTED");
  text(c,144,274,buf,2,26,p->ink);
  text(c,24,310,"PROTOCOL",1,20,dim);
  text(c,144,306,v->protocol&&*v->protocol?v->protocol:"NOT REPORTED",2,26,p->ink);
  text(c,24,342,"RECEIVED FPS",1,20,dim);
  if(v->ready&&v->connected&&v->stats_valid)snprintf(buf,sizeof(buf),"%.1f HZ",(double)v->frame_hz);else snprintf(buf,sizeof(buf),"NOT REPORTED");
  text(c,144,338,buf,2,26,p->ink);
  text(c,24,378,"STATUS COMES FROM THE RECEIVER",1,65,dim);
  text(c,492,192,m->fb_takeover?"VIDEO / FB TAKEOVER ACTIVE":"STOCK DISPLAY ACTIVE (TAKEOVER OFF)",1,45,m->fb_takeover?p->accent:dim);
  text(c,492,220,"TAKEOVER BUTTON TRIGGER",1,30,dim);
  text(c,492,402,"FOR VJ.TOOLS LIBRARY / STANDALONE READY",1,48,p->ink);
  text(c,492,422,"vj.tools",2,24,p->accent);
  text(c,12,422,"DJ MODS: CDJ3K-MODS / NSAINTOT + CONTRIBUTORS",1,95,dim);
 }
 if(u->page==XZ_UI_SETTINGS)text(c,12,430,m->settings_status?m->settings_status:"INSERT USB TO SAVE SETTINGS",1,115,p->ink);
 rect(c,0,451,800,29,blend(p->bg,p->ink,20));rect(c,0,451,800,1,blend(p->bg,p->ink,90));
 text(c,12,461,u->notice[0]?u->notice:((u->page==XZ_UI_SETTINGS||u->page==XZ_UI_CONTROLS)?(m->settings_status?m->settings_status:"INSERT USB TO SAVE SETTINGS"):(u->page==XZ_UI_CONNECTION?(m->connection.status?m->connection.status:(m->connection.ready&&m->connection.connected?"VJ.Tools CONNECTED":"VJ.Tools CONNECTION AVAILABLE")):(d->status?d->status:"LOAD A TRACK TO USE DECK CONTROLS"))),1,126,u->notice[0]?p->alarm:dim);
 return 1;
}
static struct xz_ui_action action(enum xz_ui_action_kind kind,enum xz_ui_phase phase,int deck,int index,float value,float second){
 return (struct xz_ui_action){kind,phase,deck,index,value,second};
}
size_t xz_ui_cancel(struct xz_ui *u,struct xz_ui_action out[XZ_UI_ACTIONS]){
 size_t n=0;if(u->held_kind!=XZ_UI_NONE)out[n++]=action(u->held_kind,XZ_UI_RELEASE,u->held_deck,u->held_index,0,0);
 u->held_kind=XZ_UI_NONE;u->capture=-1;u->down=0;u->dragged=0;return n;
}
static size_t touch_widgets(struct xz_ui *u,const struct xz_ui_model *m,int x,int y,int down,struct xz_ui_action out[XZ_UI_ACTIONS],const struct xz_ui_widget *w,size_t count){
 struct xz_ui_widget a;size_t n,i;const struct xz_ui_deck *d;int press;
 if(!u||!m||!out||u->deck<0||u->deck>3)return 0;
 if(!down)return xz_ui_cancel(u,out);
 press=!u->down;u->down=1;n=0;d=&m->deck[u->deck];
 if(press){u->capture=-1;for(i=0;i<count;i++)if(x>=w[i].x&&x<w[i].x+w[i].w&&y>=w[i].y&&y<w[i].y+w[i].h){u->capture=(int)i;break;}}
 if(u->capture<0||(size_t)u->capture>=count)return 0;a=w[u->capture];
 if(!ready(m,u->deck,&a)){
  if(!press)return 0;
  if(a.kind==XZ_UI_CONNECTION_ENABLE||a.kind==XZ_UI_DISCOVERY)snprintf(u->notice,sizeof(u->notice),"%s CONTROL IS NOT AVAILABLE",a.label);
  else snprintf(u->notice,sizeof(u->notice),"%s / NOT READY - CHECK FEATURE, MEDIA AND RUNTIME",a.label[0]?a.label:"CONTROL");
  out[n++]=action(XZ_UI_UNAVAILABLE,XZ_UI_PRESS,u->deck,(int)a.requires,0,0);return n;
 }
 if(press)u->notice[0]=0;
 if(a.kind==XZ_UI_LEVEL||a.kind==XZ_UI_VOLUME){out[n++]=action(a.kind,press?XZ_UI_PRESS:XZ_UI_MOVE,u->deck,a.index,clampf((float)(x-a.x-8)/(float)(a.w-16),0,1),0);return n;}
 if(a.kind==XZ_UI_STRIP){static const float beats[6]={.0625f,.125f,.25f,.5f,1,2};int column=(int)clampf((float)(x-a.x)/96,0,5);float pitch=clampf(12-24*(float)(y-a.y)/(float)(a.h-1),-12,12);
  u->held_kind=a.kind;u->held_deck=u->deck;u->held_index=column;out[n++]=action(a.kind,press?XZ_UI_PRESS:XZ_UI_MOVE,u->deck,column,beats[column],pitch);return n;}
 if(!press)return 0;
 if(a.kind==XZ_UI_PANEL){enum xz_ui_page old=u->page;u->page=(enum xz_ui_page)a.index;u->capture=-1;out[n++]=action(a.kind,XZ_UI_PRESS,u->deck,(int)old,(float)u->page,0);}
 else if(a.kind==XZ_UI_DECK){int old=u->deck;u->deck=a.index;u->capture=-1;out[n++]=action(a.kind,XZ_UI_PRESS,old,a.index,0,0);}
 else if(a.kind==XZ_UI_MUTE||a.kind==XZ_UI_SAMPLE_PAD||a.kind==XZ_UI_HOTCUE_PAD){u->held_kind=a.kind;u->held_deck=u->deck;u->held_index=a.index;out[n++]=action(a.kind,XZ_UI_PRESS,u->deck,a.index,1,0);}
 else{float value=1;
  switch(a.kind){case XZ_UI_ENABLE:value=(m->enabled&(uint32_t)a.index)?0:1;break;case XZ_UI_BYPASS:value=d->bypass?0:1;break;
  case XZ_UI_HOLD:value=d->hold?0:1;break;case XZ_UI_OVERDUB:value=d->overdub?0:1;break;case XZ_UI_SERVER_AUTO:value=m->server_auto?0:1;break;
  case XZ_UI_CONNECTION_ENABLE:value=m->connection.enabled?0:1;break;case XZ_UI_DISCOVERY:value=m->connection.discoverable?0:1;break;
  case XZ_UI_SHIFT_PAGES:value=m->shift_pages?0:1;break;
  case XZ_UI_PAD_FEEDBACK:value=m->pad_feedback?0:1;break;
  case XZ_UI_SHIFT_KEYSYNC:value=m->shift_keysync?0:1;break;
  case XZ_UI_SPARE_EQ:value=m->spare_eq?0:1;break;
  case XZ_UI_TAKEOVER_TOGGLE:value=m->fb_takeover?0:1;break;
  case XZ_UI_TAKEOVER_ASSIGN:value=(float)a.index;break;
  case XZ_UI_STEM_PAGE:value=(float)a.index;break;
  case XZ_UI_KEY_SHIFT:value=(float)a.index;break;case XZ_UI_SET_THEME:value=(float)a.index;break;default:break;}
  out[n++]=action(a.kind,XZ_UI_PRESS,u->deck,a.index,value,0);
 }
 return n;
}
size_t xz_ui_touch(struct xz_ui *u,const struct xz_ui_model *m,int x,int y,int down,struct xz_ui_action out[XZ_UI_ACTIONS]){
 struct xz_ui_widget w[XZ_UI_WIDGETS];
 if(!u||!m||u->deck<0||u->deck>3)return 0;
 size_t count=xz_ui_layout(u,m,w);
 return touch_widgets(u,m,x,y,down,out,w,count);
}
size_t xz_ui_inline_layout(const struct xz_ui *u,int width,int height,struct xz_ui_widget *w){
 size_t n=0;
 if(width<400||width>800||height<48||height>96||u->deck<0||u->deck>1)return 0;
 add(w,&n,0,2,40,height-4,XZ_UI_DECK,1-u->deck,0,u->deck?"D2":"D1");
 add(w,&n,width-56,2,56,height-4,XZ_UI_BYPASS,0,XZ_UI_STEM,"BYPASS");
 for(int i=0;i<3;i++){
  int stem=xz_stem_for_pad(i);
  int x=42+(width-100)*i/3,end=42+(width-100)*(i+1)/3;
  add(w,&n,x,2,end-x-2,height-4,XZ_UI_MUTE,stem,XZ_UI_STEM,stems[stem]);
 }
 return n;
}
size_t xz_ui_inline_touch(struct xz_ui *u,const struct xz_ui_model *m,int width,int height,int x,int y,int down,struct xz_ui_action out[XZ_UI_ACTIONS]){
 struct xz_ui_widget w[XZ_UI_WIDGETS];
 if(!u||!m)return 0;
 size_t count=xz_ui_inline_layout(u,width,height,w);
 if(down&&!u->down)for(size_t i=0;i<count;i++){
  struct xz_ui_widget a=w[i];
  if(a.kind==XZ_UI_MUTE&&x>=a.x&&x<a.x+a.w&&y>=a.y&&y<a.y+a.h&&ready(m,u->deck,&a)){
   u->down=1;u->capture=(int)i;u->held_kind=XZ_UI_MUTE;u->held_deck=u->deck;u->held_index=a.index;
   u->touch_start_x=x;u->touch_start_level=m->deck[u->deck].levels[a.index];u->dragged=0;return 0;
  }
 }
 if(u->held_kind==XZ_UI_MUTE&&u->capture>=0&&(size_t)u->capture<count){
  struct xz_ui_widget a=w[u->capture];int dx=x-u->touch_start_x;
  if(down){
   if(dx>8||dx< -8)u->dragged=1;
   if(!u->dragged)return 0;
   out[0]=action(XZ_UI_LEVEL,XZ_UI_MOVE,u->held_deck,u->held_index,clampf(u->touch_start_level+(float)dx/(a.w-12),0,1),0);return 1;
  }
  size_t n=0;if(!u->dragged)out[n++]=action(XZ_UI_MUTE,XZ_UI_PRESS,u->held_deck,u->held_index,1,0);
  struct xz_ui_action ignored[XZ_UI_ACTIONS];xz_ui_cancel(u,ignored);return n;
 }
 return touch_widgets(u,m,x,y,down,out,w,count);
}
int xz_ui_inline_render(const struct xz_ui *u,const struct xz_ui_model *m,uint16_t *pixels,size_t count,size_t stride,int width,int height){
 struct xz_ui_widget w[XZ_UI_WIDGETS];
 if(!u||!m||!pixels||height<=0||width<=0||stride<(size_t)width||stride>count/(size_t)height)return 0;
 size_t n=xz_ui_inline_layout(u,width,height,w);if(!n)return 0;
 struct canvas c={pixels,stride,width,height};
 const struct palette *p=&palettes[m->theme>=0&&m->theme<7?m->theme:0];
 const struct xz_ui_deck *d=&m->deck[u->deck];
 rect(c,0,0,width,height,p->bg);
 for(size_t i=0;i<n;i++){
  struct xz_ui_widget a=w[i];int available=ready(m,u->deck,&a);
  int muted=a.kind==XZ_UI_MUTE&&(d->muted&(1u<<a.index));
  uint32_t color=available?p->ink:blend(p->bg,p->ink,100);
  rect(c,a.x,a.y,a.w,a.h,blend(p->bg,p->ink,22));
  border(c,a.x,a.y,a.w,a.h,blend(p->bg,p->ink,100));
  if(a.kind==XZ_UI_MUTE){
   int on=available&&!muted&&d->levels[a.index]>0;
   rect(c,a.x+1,a.y+1,a.w-2,a.h-2,blend(p->bg,p->stem[a.index],on?150:32));
   border(c,a.x,a.y,a.w,a.h,available?p->stem[a.index]:color);
   text(c,a.x+8,a.y+7,a.label,2,(a.w-16)/12,color);
   text(c,a.x+a.w-10,a.y+6,pads[(m->stem_bank==1?4:0)+2-a.index],1,1,color);
   int bar=(int)((a.w-12)*clampf(muted?0:d->levels[a.index],0,1));
   rect(c,a.x+6,a.y+a.h-8,a.w-12,4,blend(p->bg,p->ink,60));
   if(bar)rect(c,a.x+6,a.y+a.h-8,bar,4,on?p->stem[a.index]:color);
   if(d->stem_loading)text(c,a.x+8,a.y+30,"LOADING",1,(a.w-16)/6,p->alarm);
   continue;
  }
  if(a.kind==XZ_UI_LEVEL){
   int y=a.y+a.h/2;
   rect(c,a.x+8,y-1,a.w-16,2,color);
   for(int tick=0;tick<5;tick++)rect(c,a.x+8+(a.w-16)*tick/4,y-3,1,7,color);
   int value=(int)(clampf(d->levels[a.index],0,1)*(a.w-16));
   rect(c,a.x+8+value-2,y-6,4,13,available?p->ink:color);
   rect(c,a.x+8+value-2,y-6,4,3,available?p->stem[a.index]:color);
  }else{
   if(a.kind==XZ_UI_MUTE)rect(c,a.x,a.y,a.w,2,available?p->stem[a.index]:color);
   if(muted||(a.kind==XZ_UI_BYPASS&&d->bypass))border(c,a.x,a.y,a.w,a.h,p->alarm);
   if(a.kind==XZ_UI_DECK){text(c,a.x+6,a.y+8,"DECK",1,5,color);text(c,a.x+14,a.y+28,u->deck?"2":"1",2,2,color);}
   else text(c,a.x+4,a.y+8,a.label,1,(a.w-8)/6,color);
   if(a.kind==XZ_UI_BYPASS){text(c,a.x+7,a.y+32,d->bypass?"ON":"OFF",1,6,d->bypass?p->alarm:color);text(c,a.x+a.w-9,a.y+8,m->stem_bank?"H":"D",1,1,color);}
   if(muted)text(c,a.x+a.w-42,a.y+8,"MUTE",1,6,p->alarm);
  }
 }
 return 1;
}
