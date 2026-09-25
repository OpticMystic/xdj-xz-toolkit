#include "ui.h"
#include "stem_pads.h"
#include "font_atlas.h"
#include "wave_viewport.h"
#include "pixel_font.h"
#include <stdio.h>
#include <string.h>

static const enum xz_ui_page menu_pages[4]={XZ_UI_CONTROLS,XZ_UI_THEMES,XZ_UI_CONNECTION,XZ_UI_SETTINGS};
static const char *menu_names[4]={"CONTROLS","APPEARANCE","VJ.TOOLS","ADVANCED"};
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
struct canvas {uint16_t *p;size_t stride;int width,height,theme;};
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
 if(c.theme==7||c.theme==10){
  int size=scale>1?2:1;
  for(int count=0;*s&&count<limit;count++,x+=6*size){
   unsigned ch=codepoint(&s);
   for(unsigned row=0;row<7;row++)for(unsigned col=0;col<5;col++)
    if(xz_pixel_font_row(ch,row)&(1u<<(4-col)))rect(c,x+(int)col*size,y+(int)row*size,size,size,color);
  }
  return;
 }
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
static struct xz_theme_surface themed(struct canvas c){
 return (struct xz_theme_surface){c.p,c.stride,c.width,c.height,0,0,c.width,c.height};
}
static void frame(struct canvas c,int theme,struct xz_ui_widget a,uint32_t fill,int selected){
 xz_theme_frame(themed(c),theme,(struct xz_theme_rect){a.x,a.y,a.w,a.h},fill,selected,XZ_THEME_BUTTON);
}
void xz_ui_render_native_buttons(uint16_t *pixels,size_t stride,int theme,int stems,int vj_visible,int vj_active){
 if(!pixels||stride<800)return;
 if(!theme){xz_ui_render_badge(pixels,stride);xz_ui_render_stems_button(pixels,stride,stems);if(vj_visible)xz_ui_render_vj_button(pixels,stride,vj_active);return;}
 struct canvas c={pixels,stride,800,480,theme};const struct xz_theme_palette *p=xz_theme_palette(theme);
 struct xz_ui_widget buttons[3]={{744,0,56,24,XZ_UI_NONE,0,0,"MODS",-1},{674,0,68,24,XZ_UI_NONE,0,0,"STEMS",-1},{0,0,112,24,XZ_UI_NONE,0,0,vj_active?"EXIT VJ":"VJ.TOOLS",-1}};
 for(int i=0;i<(vj_visible?3:2);i++){
  struct xz_ui_widget a=buttons[i];frame(c,theme,a,p->bg,i==1?stems:i==2?vj_active:0);
  text(c,a.x+6,a.y+7,a.label,1,(a.w-12)/6,p->ink);
 }
}
static void deck_card(struct canvas c,const struct xz_ui_widget *a,int selected,const struct xz_theme_palette *p){
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
 struct canvas c={pixels,stride,800,480,0};
 rect(c,744,0,56,24,0x15191e);border(c,744,0,56,24,0xa8ceff);
 text(c,751,5,"MODS",2,4,0xf4f5f6);
}
void xz_ui_render_stems_button(uint16_t *pixels,size_t stride,int enabled){
 if(!pixels||stride<800)return;
 struct canvas c={pixels,stride,800,480,0};uint32_t color=enabled?0x52d794:0xa8ceff;
 rect(c,674,0,68,24,enabled?0x123c2a:0x15191e);border(c,674,0,68,24,color);
 text(c,681,5,"STEMS",2,5,color);
}
void xz_ui_render_vj_button(uint16_t *pixels,size_t stride,int takeover_active){
 if(!pixels||stride<800)return;
 struct canvas c={pixels,stride,800,480,0};
 uint32_t border_col=takeover_active?0x52d794:0xa8ceff;
 rect(c,0,0,112,24,0x15191e);border(c,0,0,112,24,border_col);
 text(c,7,5,takeover_active?"EXIT VJ":"VJ.TOOLS",2,8,takeover_active?0x52d794:0xf4f5f6);
}
static void add(struct xz_ui_widget *w,size_t *n,int x,int y,int width,int height,
 enum xz_ui_action_kind kind,int index,uint32_t cap,const char *label){
 w[*n]=(struct xz_ui_widget){x,y,width,height,kind,index,cap,label,-1};(*n)++;
}
const char *xz_ui_theme_name(int theme){return xz_theme_name(theme);}
uint32_t xz_ui_stem_color(int theme,int index){
 return xz_theme_palette(theme)->stem[index>=0&&index<3?index:0];
}
void xz_ui_init(struct xz_ui *u){memset(u,0,sizeof(*u));u->capture=-1;u->page=XZ_UI_CONTROLS;}
size_t xz_ui_layout(const struct xz_ui *u,const struct xz_ui_model *m,struct xz_ui_widget w[XZ_UI_WIDGETS]){
 size_t n=0;int i;
 for(i=0;i<4;i++)add(w,&n,12+i*168,46,160,44,XZ_UI_PANEL,menu_pages[i],0,menu_names[i]);
 add(w,&n,700,46,88,44,XZ_UI_CLOSE,0,0,"CLOSE");
 if(u->page==XZ_UI_STEMS||u->page==XZ_UI_XPAD||u->page==XZ_UI_SETTINGS)
  for(i=0;i<2;i++)add(w,&n,12+i*170,100,162,38,XZ_UI_DECK,i,0,"USB DECK");
 if(u->page==XZ_UI_STEMS){
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
  add(w,&n,12,154,376,44,XZ_UI_ENABLE,XZ_UI_GATE,XZ_UI_GATE,"GATE CUE");
  add(w,&n,412,154,376,44,XZ_UI_ENABLE,XZ_UI_SMART,XZ_UI_SMART,"SMART CUE");
  add(w,&n,12,210,376,44,XZ_UI_ENABLE,XZ_UI_PREVIEW,XZ_UI_PREVIEW,"HOT-CUE PREVIEW");
  add(w,&n,412,210,376,44,XZ_UI_ENABLE,XZ_UI_SAMPLE,XZ_UI_SAMPLE,"X-PAD ENGINE");
  add(w,&n,12,266,376,44,XZ_UI_PANEL,XZ_UI_STEMS,0,"GROOVE PADS");
  add(w,&n,412,266,376,44,XZ_UI_PANEL,XZ_UI_XPAD,0,"X-PAD CONTROLS");
  add(w,&n,12,328,86,46,XZ_UI_KEY_SHIFT,-1,XZ_UI_KEY,"KEY -");
  add(w,&n,106,328,80,46,XZ_UI_KEY_SHIFT,0,XZ_UI_KEY,"RESET");
  add(w,&n,194,328,86,46,XZ_UI_KEY_SHIFT,1,XZ_UI_KEY,"KEY +");
  add(w,&n,288,328,100,46,XZ_UI_KEY_SYNC,0,XZ_UI_KEYSYNC,"KEY SYNC");
  add(w,&n,412,328,376,46,XZ_UI_SHIFT_KEYSYNC,0,XZ_UI_KEYSYNC,"SHIFT + SYNC");
  add(w,&n,12,388,376,44,XZ_UI_SHIFT_PAGES,0,0,"SHIFT + PAD MODES");
 }else if(u->page==XZ_UI_CONTROLS){
  add(w,&n,12,154,376,48,XZ_UI_ENABLE,XZ_UI_STEM,XZ_UI_STEM,"STEM AUDIO");
  add(w,&n,412,154,376,48,XZ_UI_STEMS_OVERLAY,0,0,"SHOW STEM ROWS");
  add(w,&n,12,254,180,44,XZ_UI_STEM_BANK,0,0,"PADS A-D");
  add(w,&n,202,254,186,44,XZ_UI_STEM_BANK,1,0,"PADS E-H");
  add(w,&n,412,254,376,44,XZ_UI_PAD_FEEDBACK,0,0,"STEM PAD LIGHTS");
  for(i=0;i<4;i++)add(w,&n,12+i*196,334,188,44,XZ_UI_STEM_PAGE,i,0,stem_pages[i]);
 }else if(u->page==XZ_UI_THEMES){
  for(i=0;i<XZ_THEME_COUNT;i++)add(w,&n,12+(i%3)*262,158+(i/3)*70,252,62,XZ_UI_SET_THEME,i,0,xz_theme_name(i));
 }else if(u->page==XZ_UI_CONNECTION){
  add(w,&n,492,142,296,42,XZ_UI_TAKEOVER_TOGGLE,0,0,"VJ.TOOLS VIEW");
  add(w,&n,492,238,94,40,XZ_UI_TAKEOVER_ASSIGN,0,0,"LINK");
  add(w,&n,592,238,96,40,XZ_UI_TAKEOVER_ASSIGN,1,0,"REKORDBOX");
  add(w,&n,694,238,94,40,XZ_UI_TAKEOVER_ASSIGN,2,0,"ONSCREEN");
  add(w,&n,492,290,296,44,XZ_UI_CONNECTION_ENABLE,0,XZ_UI_VJ_CONNECTION,"VJ.TOOLS CONNECTION");
  add(w,&n,492,346,296,44,XZ_UI_DISCOVERY,0,XZ_UI_VJ_DISCOVERY,"DISCOVERABLE");
 }
 return n;
}
static int ready(const struct xz_ui_model *m,int deck,const struct xz_ui_widget *w){
 const struct xz_ui_deck *d=&m->deck[deck];
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
 struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n,i;struct canvas c;const struct xz_theme_palette *p;const struct xz_ui_deck *d;uint32_t panel,dim;char buf[96];
 if(!u||!m||!pixels||u->deck<0||u->deck>3||stride<800||stride>count/480)return 0;
 c=(struct canvas){pixels,stride,800,480,m->theme};p=xz_theme_palette(m->theme);d=&m->deck[u->deck];
 panel=blend(p->bg,p->ink,28);dim=blend(p->bg,p->ink,154);xz_theme_background(themed(c),m->theme,(struct xz_theme_rect){0,0,800,480});
 xz_theme_frame(themed(c),m->theme,(struct xz_theme_rect){0,0,800,30},blend(p->bg,p->ink,15),0,XZ_THEME_HEADER);
 text(c,12,7,"XZ MODS",2,15,m->theme==9?0xffffff:p->ink);text(c,108,10,"XDJ-XZ",1,20,m->theme==9?0xffffff:dim);
 text(c,680,9,"vj.tools/xzmods",1,18,m->theme==9?0xffffff:p->accent);
 n=xz_ui_layout(u,m,w);
 for(i=0;i<n;i++){
  struct xz_ui_widget a=w[i];int available=ready(m,u->deck,&a),selected=0;uint32_t color=p->accent;
  if(a.kind==XZ_UI_DECK)selected=a.index==u->deck;
  if(a.kind==XZ_UI_PANEL)selected=a.index==(int)u->page;
  if(a.kind==XZ_UI_SET_THEME)selected=a.index==m->theme;
  if(a.kind==XZ_UI_STEM_PAGE)selected=a.index==m->stem_page;
  if(a.kind==XZ_UI_SHIFT_PAGES)selected=m->shift_pages;
  if(a.kind==XZ_UI_PAD_FEEDBACK)selected=m->pad_feedback;
  if(a.kind==XZ_UI_STEMS_OVERLAY)selected=m->stems_overlay;
  if(a.kind==XZ_UI_SHIFT_KEYSYNC)selected=m->shift_keysync;
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
   frame(c,m->theme,a,available?(selected?blend(panel,color,38):panel):blend(p->bg,p->ink,14),available&&selected);
  }
  if(a.kind==XZ_UI_PANEL)rect(c,a.x,a.y+a.h-3,a.w,3,selected?p->accent:blend(p->bg,p->ink,58));
  if(a.kind==XZ_UI_MUTE||a.kind==XZ_UI_GROOVE_PAD)rect(c,a.x,a.y,a.w,3,available?color:dim);
  if(a.kind==XZ_UI_PANEL&&a.index==XZ_UI_STEMS&&(m->enabled&XZ_UI_STEM)&&!d->bypass&&
      (d->levels[0]<1||d->levels[1]<1||d->levels[2]<1||d->muted||d->groove_active>=0))
      rect(c,a.x+a.w-8,a.y+7,4,4,p->alarm);
  if(a.kind==XZ_UI_DECK){
   deck_card(c,&a,selected,p);snprintf(buf,sizeof(buf),"DECK %d",a.index+1);
   text(c,a.x+14,a.y+4,buf,2,12,p->ink);text(c,a.x+14,a.y+24,a.label,1,22,dim);
  }
  else if(a.kind==XZ_UI_SET_THEME){
   const struct xz_theme_palette *sample=xz_theme_palette(a.index);
   struct canvas sample_canvas=c;sample_canvas.theme=a.index;
   frame(c,a.index,a,sample->bg,selected);
   text(sample_canvas,a.x+14,a.y+12,a.label,2,(a.w-28)/12,sample->ink);
   text(sample_canvas,a.x+14,a.y+40,selected?"SELECTED":"TAP TO APPLY",1,32,sample->ink);
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
   if(a.kind==XZ_UI_SHIFT_PAGES||a.kind==XZ_UI_PAD_FEEDBACK||a.kind==XZ_UI_SHIFT_KEYSYNC||a.kind==XZ_UI_TAKEOVER_TOGGLE||a.kind==XZ_UI_STEMS_OVERLAY)
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
  if(u->page==XZ_UI_STEMS){
   text(c,12,234,"GROOVE PADS",2,55,p->ink);
   text(c,12,269,"REPLACE A STEM WITH AN ASSIGNED GROOVE",1,115,dim);
   text(c,12,294,"USE THE PLAY-SCREEN ROWS TO MIX STEMS",1,115,dim);
   text(c,12,324,"UNASSIGNED OR UNAVAILABLE PADS ARE MARKED BELOW",1,115,dim);
  }
  else{snprintf(buf,sizeof(buf),"%s BEATS / %+.1f KEY",lengths[d->loop_index>=0&&d->loop_index<6?d->loop_index:0],(double)d->pitch);text(c,600,279,buf,1,31,p->alarm);text(c,600,357,"VOL / HOLD LATCHES",1,30,dim);text(c,12,372,"SAMPLE BANK A-H / CLOSING X-PAD STOPS SOUND",1,90,dim);}
 }else if(u->page==XZ_UI_SETTINGS){
  text(c,412,112,"DECK SETTINGS + EXPERIMENTAL TOOLS",1,58,dim);
  text(c,412,395,"STEM SERVER: NOT AVAILABLE",1,54,dim);
  text(c,412,416,"UNAVAILABLE TOOLS ARE MARKED NOT READY",1,58,dim);
 }
 else if(u->page==XZ_UI_CONTROLS){
  text(c,12,112,"STEMS ON THE PLAY SCREEN",2,62,p->ink);
  text(c,12,222,"PHYSICAL PAD SHORTCUTS",2,62,p->ink);
  text(c,12,311,"STEM PAD MODE / OTHER MODES KEEP THEIR NORMAL CONTROLS",1,118,dim);
  text(c,12,393,"TAP A STEM TO MUTE. SLIDE LEFT OR RIGHT TO CHANGE ITS VOLUME.",1,126,p->ink);
  text(c,12,412,"BYPASS RETURNS THAT DECK TO THE ORIGINAL MIX.",1,126,dim);
  if(u->notice[0])text(c,12,430,u->notice,1,115,p->alarm);
 }
 else if(u->page==XZ_UI_THEMES)text(c,12,112,"CHOOSE A LOOK FOR THE WHOLE XZ INTERFACE",2,62,p->ink);
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
  text(c,492,192,m->fb_takeover?"COMPUTER SCREEN ACTIVE":"NATIVE PLAY SCREEN ACTIVE",1,45,m->fb_takeover?p->accent:dim);
  text(c,492,220,"SCREEN SWITCH BUTTON",1,30,dim);
  text(c,492,402,"OPTIONAL VJ.TOOLS LIBRARY CONNECTION",1,48,p->ink);
  text(c,492,422,"vj.tools",2,24,p->accent);
  text(c,12,422,"DJ MODS: CDJ3K-MODS / NSAINTOT + CONTRIBUTORS",1,95,dim);
 }

 rect(c,0,451,800,29,blend(p->bg,p->ink,20));rect(c,0,451,800,1,blend(p->bg,p->ink,90));
 text(c,12,461,u->page==XZ_UI_THEMES&&m->theme_status?m->theme_status:u->page==XZ_UI_CONTROLS?(m->settings_status?m->settings_status:"INSERT USB TO SAVE SETTINGS"):u->notice[0]?u->notice:((u->page==XZ_UI_SETTINGS||u->page==XZ_UI_CONTROLS)?(m->settings_status?m->settings_status:"INSERT USB TO SAVE SETTINGS"):(u->page==XZ_UI_CONNECTION?(m->connection.status?m->connection.status:(m->connection.ready&&m->connection.connected?"VJ.Tools CONNECTED":"VJ.Tools CONNECTION AVAILABLE")):(d->status?d->status:"LOAD A TRACK TO USE DECK CONTROLS"))),1,126,u->notice[0]?p->alarm:dim);
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
 struct xz_ui_widget a;size_t n,i;const struct xz_ui_deck *d;int press,deck;
 if(!u||!m||!out||u->deck<0||u->deck>3)return 0;
 if(!down)return xz_ui_cancel(u,out);
 press=!u->down;u->down=1;n=0;
 if(press){u->capture=-1;for(i=0;i<count;i++)if(x>=w[i].x&&x<w[i].x+w[i].w&&y>=w[i].y&&y<w[i].y+w[i].h){u->capture=(int)i;break;}}
 if(u->capture<0||(size_t)u->capture>=count)return 0;a=w[u->capture];
 deck=a.deck>=0?a.deck:u->deck;d=&m->deck[deck];
 if(!ready(m,deck,&a)){
  if(!press)return 0;
  if(a.kind==XZ_UI_CONNECTION_ENABLE||a.kind==XZ_UI_DISCOVERY)snprintf(u->notice,sizeof(u->notice),"%s CONTROL IS NOT AVAILABLE",a.label);
  else snprintf(u->notice,sizeof(u->notice),"%s / NOT READY - CHECK FEATURE, MEDIA AND RUNTIME",a.label[0]?a.label:"CONTROL");
  out[n++]=action(XZ_UI_UNAVAILABLE,XZ_UI_PRESS,deck,(int)a.requires,0,0);return n;
 }
 if(press)u->notice[0]=0;
 if(a.kind==XZ_UI_LEVEL||a.kind==XZ_UI_VOLUME){out[n++]=action(a.kind,press?XZ_UI_PRESS:XZ_UI_MOVE,deck,a.index,clampf((float)(x-a.x-8)/(float)(a.w-16),0,1),0);return n;}
 if(a.kind==XZ_UI_STRIP){static const float beats[6]={.0625f,.125f,.25f,.5f,1,2};int column=(int)clampf((float)(x-a.x)/96,0,5);float pitch=clampf(12-24*(float)(y-a.y)/(float)(a.h-1),-12,12);
  u->held_kind=a.kind;u->held_deck=deck;u->held_index=column;out[n++]=action(a.kind,press?XZ_UI_PRESS:XZ_UI_MOVE,deck,column,beats[column],pitch);return n;}
 if(!press)return 0;
 if(a.kind==XZ_UI_PANEL){enum xz_ui_page old=u->page;u->page=(enum xz_ui_page)a.index;u->capture=-1;out[n++]=action(a.kind,XZ_UI_PRESS,u->deck,(int)old,(float)u->page,0);}
 else if(a.kind==XZ_UI_DECK){int old=u->deck;u->deck=a.index;u->capture=-1;out[n++]=action(a.kind,XZ_UI_PRESS,old,a.index,0,0);}
 else if(a.kind==XZ_UI_MUTE||a.kind==XZ_UI_SAMPLE_PAD||a.kind==XZ_UI_HOTCUE_PAD){u->held_kind=a.kind;u->held_deck=deck;u->held_index=a.index;out[n++]=action(a.kind,XZ_UI_PRESS,deck,a.index,1,0);}
 else{float value=1;
  switch(a.kind){case XZ_UI_ENABLE:value=(m->enabled&(uint32_t)a.index)?0:1;break;case XZ_UI_BYPASS:value=d->bypass?0:1;break;
  case XZ_UI_HOLD:value=d->hold?0:1;break;case XZ_UI_OVERDUB:value=d->overdub?0:1;break;case XZ_UI_SERVER_AUTO:value=m->server_auto?0:1;break;
  case XZ_UI_CONNECTION_ENABLE:value=m->connection.enabled?0:1;break;case XZ_UI_DISCOVERY:value=m->connection.discoverable?0:1;break;
  case XZ_UI_SHIFT_PAGES:value=m->shift_pages?0:1;break;
  case XZ_UI_PAD_FEEDBACK:value=m->pad_feedback?0:1;break;
  case XZ_UI_SHIFT_KEYSYNC:value=m->shift_keysync?0:1;break;
  case XZ_UI_TAKEOVER_TOGGLE:value=m->fb_takeover?0:1;break;
  case XZ_UI_TAKEOVER_ASSIGN:value=(float)a.index;break;
  case XZ_UI_STEM_PAGE:value=(float)a.index;break;
  case XZ_UI_KEY_SHIFT:value=(float)a.index;break;case XZ_UI_SET_THEME:value=(float)a.index;break;default:break;}
  out[n++]=action(a.kind,XZ_UI_PRESS,deck,a.index,value,0);
 }
 return n;
}
size_t xz_ui_touch(struct xz_ui *u,const struct xz_ui_model *m,int x,int y,int down,struct xz_ui_action out[XZ_UI_ACTIONS]){
 struct xz_ui_widget w[XZ_UI_WIDGETS];
 if(!u||!m||u->deck<0||u->deck>3)return 0;
 size_t count=xz_ui_layout(u,m,w);
 return touch_widgets(u,m,x,y,down,out,w,count);
}
size_t xz_ui_inline_layout(const struct xz_ui *u,int width,int height,struct xz_ui_widget w[XZ_UI_WIDGETS]){
 size_t n=0;
 if(width<400||width>800||height!=XZ_WAVE_INLINE_HEIGHT||u->deck<0||u->deck>1)return 0;
 for(int deck=0;deck<2;deck++)for(int slot=0;slot<4;slot++){
  int x=width*slot/4,end=width*(slot+1)/4;
  int stem=slot<3?xz_stem_for_pad(slot):0;
  add(w,&n,x+1,deck*XZ_WAVE_CONTROL_HEIGHT+1,end-x-2,XZ_WAVE_CONTROL_HEIGHT-2,slot<3?XZ_UI_MUTE:XZ_UI_BYPASS,
      stem,XZ_UI_STEM,slot<3?stems[stem]:"BYPASS");
  w[n-1].deck=deck;
 }
 return n;
}
size_t xz_ui_inline_touch(struct xz_ui *u,const struct xz_ui_model *m,int width,int height,int x,int y,int down,struct xz_ui_action out[XZ_UI_ACTIONS]){
 struct xz_ui_widget w[XZ_UI_WIDGETS];
 if(!u||!m)return 0;
 size_t count=xz_ui_inline_layout(u,width,height,w);
 if(down&&!u->down)for(size_t i=0;i<count;i++){
  struct xz_ui_widget a=w[i];
  if(a.kind==XZ_UI_MUTE&&x>=a.x&&x<a.x+a.w&&y>=a.y&&y<a.y+a.h&&ready(m,a.deck,&a)){
   u->down=1;u->capture=(int)i;u->held_kind=XZ_UI_MUTE;u->held_deck=a.deck;u->held_index=a.index;
   u->touch_start_x=x;u->touch_start_level=(m->deck[a.deck].muted&(1u<<a.index))?0:m->deck[a.deck].levels[a.index];u->dragged=0;return 0;
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
 struct canvas c={pixels,stride,width,height,m->theme};
 const struct xz_theme_palette *p=xz_theme_palette(m->theme);
 rect(c,0,0,width,height,p->bg);
 for(size_t i=0;i<n;i++){
  struct xz_ui_widget a=w[i];const struct xz_ui_deck *d=&m->deck[a.deck];
  int available=ready(m,a.deck,&a);
  int muted=a.kind==XZ_UI_MUTE&&(d->muted&(1u<<a.index));
  uint32_t color=available?p->ink:blend(p->bg,p->ink,100);
  rect(c,a.x,a.y,a.w,a.h,blend(p->bg,p->ink,22));
  border(c,a.x,a.y,a.w,a.h,blend(p->bg,p->ink,100));
  if(a.kind==XZ_UI_MUTE){
   int on=available&&!muted&&d->levels[a.index]>0;
   rect(c,a.x+1,a.y+1,a.w-2,a.h-2,blend(p->bg,p->stem[a.index],on?150:32));
   border(c,a.x,a.y,a.w,a.h,available?p->stem[a.index]:color);
   if(m->theme>=7)frame(c,m->theme,a,blend(p->bg,p->stem[a.index],on?70:20),on);
   text(c,a.x+8,a.y+3,a.label,2,(a.w-16)/12,color);
   int bar=(int)((a.w-48)*clampf(muted?0:d->levels[a.index],0,1));
   rect(c,a.x+6,a.y+a.h-10,a.w-48,4,blend(p->bg,p->ink,60));
   if(bar)rect(c,a.x+6,a.y+a.h-10,bar,4,on?p->stem[a.index]:color);
   char value[8];snprintf(value,sizeof(value),"%d%%",(int)(clampf(muted?0:d->levels[a.index],0,1)*100+.5f));
   text(c,a.x+a.w-34,a.y+a.h-14,muted?"MUTE":value,1,5,color);
   if(d->stem_loading)text(c,a.x+8,a.y+24,"LOADING",1,(a.w-16)/6,p->alarm);
   continue;
  }
  if(d->bypass){rect(c,a.x+1,a.y+1,a.w-2,a.h-2,blend(p->bg,p->alarm,88));border(c,a.x,a.y,a.w,a.h,p->alarm);}
  if(m->theme>=7)frame(c,m->theme,a,p->bg,d->bypass);
  text(c,a.x+8,a.y+3,a.label,2,(a.w-16)/12,color);
  text(c,a.x+8,a.y+32,d->bypass?"ON":"OFF",1,6,d->bypass?p->alarm:color);
  text(c,a.x+a.w-29,a.y+32,a.deck?"D2":"D1",1,3,color);
 }
 return 1;
}
