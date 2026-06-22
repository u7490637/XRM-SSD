#include <stdio.h>
#include <stdint.h>
/* Exact rge_blend2_env kernel (bridge.c cJSON version), single-divide scale.
   Q16.16, round-half (+0x8000>>16) on every term, locked accumulation order. */
int32_t rge_blend2_env(int32_t entropy,int32_t kv,int32_t g,int32_t p,int32_t r,
                       int32_t availability,int32_t trust,uint8_t degraded_mode){
    int32_t w_entropy=(int32_t)(0.25*65536),w_kv=(int32_t)(0.25*65536);
    int32_t w_g=(int32_t)(0.20*65536),w_p=(int32_t)(0.15*65536),w_r=(int32_t)(0.15*65536);
    if(degraded_mode) w_r=(int32_t)((((int64_t)w_r*(int32_t)(0.9*65536))+0x8000)>>16);
    int32_t te=(int32_t)((((int64_t)w_entropy*entropy)+0x8000)>>16);
    int32_t tk=(int32_t)((((int64_t)w_kv*kv)+0x8000)>>16);
    int32_t tg=(int32_t)((((int64_t)w_g*g)+0x8000)>>16);
    int32_t tp=(int32_t)((((int64_t)w_p*p)+0x8000)>>16);
    int32_t tr=(int32_t)((((int64_t)w_r*r)+0x8000)>>16);
    int32_t base=te; base+=tk; base+=tg; base+=tp; base+=tr;
    int32_t scale=(int32_t)((((int64_t)availability*trust)+0x8000)>>16);
    return (int32_t)((((int64_t)base*scale)+0x8000)>>16);
}
int main(void){
    /* FROZEN VECTOR (Nikolai/Polo 2026-06-22) */
    int32_t E=45000,KV=32768,G=50000,P=28000,R=60000,AV=55000,TR=48000;
    int32_t normal=rge_blend2_env(E,KV,G,P,R,AV,TR,0);
    int32_t degraded=rge_blend2_env(E,KV,G,P,R,AV,TR,1);
    printf("FROZEN normal   Q16=%d  (%.4f)\n",normal,(double)normal/65536.0);
    printf("FROZEN degraded Q16=%d  (%.4f)\n",degraded,(double)degraded/65536.0);
    return 0;
}
