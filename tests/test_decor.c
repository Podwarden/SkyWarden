#include "greatest.h"
#include "decor.h"
#include <math.h>
TEST stars_in_bounds(void){
    Star s[STAR_COUNT]; decor_init_stars(s);
    for(int i=0;i<STAR_COUNT;i++){ ASSERT(s[i].x>=0 && s[i].x<400); ASSERT(s[i].y>=0 && s[i].y<150); ASSERT(s[i].sp>0.0f);}
    PASS();
}
TEST twinkle_phase_cycles(void){
    Star s[STAR_COUNT]; decor_init_stars(s);
    int seen[4]={0,0,0,0};
    for(float t=0;t<400;t+=1.0f){ int p=decor_twinkle_phase(&s[0],t); ASSERT(p>=0&&p<4); seen[p]=1; }
    ASSERT(seen[0]&&seen[1]&&seen[2]&&seen[3]);   /* visits all 4 frames over time */
    PASS();
}
GREATEST_MAIN_DEFS();
int main(int argc,char**argv){GREATEST_MAIN_BEGIN();RUN_TEST(stars_in_bounds);RUN_TEST(twinkle_phase_cycles);GREATEST_MAIN_END();}
