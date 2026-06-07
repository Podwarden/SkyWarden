#include "decor.h"
#include <math.h>
void decor_init_stars(Star* st){
    unsigned r=2246u;
    for(int i=0;i<STAR_COUNT;i++){
        r=r*1103515245u+12345u; st[i].x=(short)((r>>16)%400);
        r=r*1103515245u+12345u; st[i].y=(short)((r>>16)%150);
        r=r*1103515245u+12345u; st[i].sp=0.03f+((r>>16)%50)/1000.0f;
        r=r*1103515245u+12345u; st[i].ph0=((r>>16)%100)/100.0f*4.0f;
    }
}
int decor_twinkle_phase(const Star* s, float t){
    int p=(int)(s->ph0 + t*s->sp); p%=4; if(p<0)p+=4; return p;
}
