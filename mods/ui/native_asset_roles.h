#ifndef XZ_NATIVE_ASSET_ROLES_H
#define XZ_NATIVE_ASSET_ROLES_H
/* Verified blank artwork only, XZ 1.26 inventory1582. These are visual roles,
   not proof of native pressed/disabled semantics. Text is rendered separately.
   Preserve original key masks and distinct appearance/focus variants. */
enum xz_asset_role {XZ_ASSET_UNCLASSIFIED, XZ_ASSET_BUTTON, XZ_ASSET_FIELD, XZ_ASSET_TITLE_STRIP, XZ_ASSET_TRACK_PANEL};
enum xz_asset_appearance {XZ_ASSET_DARK, XZ_ASSET_LIGHT, XZ_ASSET_DIM, XZ_ASSET_FLAT, XZ_ASSET_PALE, XZ_ASSET_ORANGE_EDGE, XZ_ASSET_ORANGE_LIGHT_EDGE, XZ_ASSET_WHITE_EDGE, XZ_ASSET_COLOR_STRIP};
struct xz_asset_role_info {enum xz_asset_role role;enum xz_asset_appearance appearance;};
static inline struct xz_asset_role_info xz_native_asset_role(unsigned id,unsigned width,unsigned height){
 struct xz_asset_role_info result={XZ_ASSET_UNCLASSIFIED,XZ_ASSET_DARK};
 if(id>=77&&id<=86&&width==128&&height==36){
  static const enum xz_asset_appearance variants[5]={XZ_ASSET_ORANGE_EDGE,XZ_ASSET_ORANGE_LIGHT_EDGE,XZ_ASSET_DARK,XZ_ASSET_LIGHT,XZ_ASSET_WHITE_EDGE};
  result.role=XZ_ASSET_BUTTON;result.appearance=variants[(id-77)%5];
 }else if(id>=87&&id<=88&&width==424&&height==36){result.role=XZ_ASSET_BUTTON;result.appearance=id==87?XZ_ASSET_DARK:XZ_ASSET_LIGHT;
 }else if(id>=106&&id<=109&&width==248&&height==36){
  static const enum xz_asset_appearance variants[4]={XZ_ASSET_DARK,XZ_ASSET_LIGHT,XZ_ASSET_FLAT,XZ_ASSET_PALE};
  result.role=XZ_ASSET_BUTTON;result.appearance=variants[id-106];
 }else if(id>=1316&&id<=1318&&width==160&&height==55){
  static const enum xz_asset_appearance variants[3]={XZ_ASSET_DARK,XZ_ASSET_LIGHT,XZ_ASSET_DIM};
  result.role=XZ_ASSET_BUTTON;result.appearance=variants[id-1316];
 }else if(id>=1386&&id<=1391&&width==240&&height==38){
  static const enum xz_asset_appearance variants[6]={XZ_ASSET_DARK,XZ_ASSET_LIGHT,XZ_ASSET_DIM,XZ_ASSET_FLAT,XZ_ASSET_PALE,XZ_ASSET_DIM};
  result.role=XZ_ASSET_BUTTON;result.appearance=variants[id-1386];
 }else if((id==1459&&width==784&&height==30)||(id==1460&&width==704&&height==30)||(id==1489&&width==344&&height==28)){
  result.role=XZ_ASSET_FIELD;result.appearance=XZ_ASSET_PALE;
 }else if(width==400&&height==30&&((id>=650&&id<=666&&!(id&1))||(id>=688&&id<=704&&!(id&1)))){
  result.role=XZ_ASSET_TITLE_STRIP;result.appearance=XZ_ASSET_COLOR_STRIP;
 }else if((id==649||id==687)&&width==400&&height==172){result.role=XZ_ASSET_TRACK_PANEL;}
 return result;
}
#endif
