"""Compare portable DSP with compiled, pinned upstream functions. POSIX host.

python3 verify_performance.py /path/to/cdj3k-mods
"""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
from verify_upstream import PIN, function, run

HERE = Path(__file__).resolve().parent
upstream = Path(sys.argv[1]).resolve()
git = ["git", "-c", f"safe.directory={upstream}", "-C", upstream]
audio = run(git + ["show", f"{PIN}:package/deck/mods/xpad/audio.c"])
loop = run(git + ["show", f"{PIN}:package/deck/mods/stem/loop.c"])
oracle = """#include <stdint.h>
#include <stdio.h>
#include <math.h>
#define XP_XFADE_UP_MS 26.67f
#define XP_XFADE_DOWN_MS 31.35f
#define BEAT_WALK_MAX 4
struct xp_pitch {float w,wq,half;uint32_t step;};
struct stem_loop {int64_t from,span;};
"""
for name in ("static inline float xp_xfade_window(", "static inline void xp_tap(",
             "static inline void xp_heads(", "static void xp_pitch_block("):
    oracle += function(audio, name) + "\n"
for name in ("double stem_loop_wrap(", "double stem_loop_phase(", "double stem_beat_at("):
    oracle += function(loop, name) + "\n"
oracle += """
int main(int argc,char **argv) {
 int16_t pcm[4000];FILE *out;const float semis[]={-12,0,12};
 if(argc!=2 || !(out=fopen(argv[1],"wb")))return 1;
 for(unsigned i=0;i<4000;++i)pcm[i]=(int16_t)((i*37)%20001-10000);
 for(unsigned test=0;test<3;++test) {
  float win=0,ratio=exp2f(semis[test]/12),a=expf(-(256.0f/44100)/(49.5f*0.001f));
  uint32_t phi=0;int64_t position=0;
  for(unsigned block=0;block<2;++block) {
   struct xp_pitch p;xp_pitch_block(&p,&win,ratio,a,44100);
   for(unsigned k=0;k<256;++k) {
    float pair[2];xp_heads(pcm,2000,position,phi,&p,pair,pair+1);
    fwrite(pair,sizeof(float),2,out);++position;phi+=p.step;
   }
  }
 }
 for(int ratio=1;ratio<=4;++ratio)for(int at=-100;at<=100;at+=25) {
  struct stem_loop l={-10,97};double x=stem_loop_phase(&l,at,ratio*0.25);fwrite(&x,sizeof(x),1,out);
 }
 {int64_t beats[]={100,200,325,500},at[]={0,99,100,199,200,300,499,500,700};
  for(unsigned i=0;i<9;++i){double x=stem_beat_at(beats,4,at[i],NULL);fwrite(&x,sizeof(x),1,out);}}
 return fclose(out)!=0;
}
"""
cc = shlex.split(os.environ.get("CC", "cc"))
with tempfile.TemporaryDirectory(prefix="xz-performance-") as temporary:
    root = Path(temporary)
    (root / "oracle.c").write_text(oracle)
    common = ["-std=c11", "-O2", "-UNDEBUG", "-Wall", "-Wextra", "-Werror"]
    subprocess.run(cc + common + [str(root / "oracle.c"), "-lm", "-o", str(root / "oracle")], check=True)
    subprocess.run([str(root / "oracle"), str(root / "expected.bin")], check=True)
    subprocess.run(cc + common + ["-I", str(HERE.parent), str(HERE / "test_performance.c"),
        str(HERE.parent / "performance.c"), str(HERE.parent / "stem_mix.c"),
        "-lm", "-o", str(root / "test")], check=True)
    subprocess.run([str(root / "test"), str(root / "expected.bin")], cwd=root, check=True)
