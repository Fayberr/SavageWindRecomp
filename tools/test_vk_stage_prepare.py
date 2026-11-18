#!/usr/bin/env python3
"""Compare executor descriptor preparation with the frozen pre-experiment path.

The fixture is synthetic. The actual C functions are extracted and compiled with
resource providers that record their calls and descriptor contents. No game or GPU
is needed. Pass --reference to a saved exec.c from before the candidate changes.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def function(source, name):
    # The definition, not a prototype or a call: "static ... name(...) {" at the start of a line.
    match = re.search(r'^static [^;{}]*?\b' + re.escape(name) + r'\([^;{}]*\)\s*\{', source, re.M)
    start = match.start()
    brace = match.end() - 1
    depth = 1
    pos = brace + 1
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    return source[start:pos]


def typedef(source, name):
    match = re.search(r'^typedef struct \{[^{}]*\} ' + re.escape(name) + r';', source, re.M)
    return match.group(0)


FIXTURE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>
#define MAX_BIND 96
#define GMEM_BUFFER_INDEX 20
#define GMEM2_BUFFER_INDEX 21
#define MVEC_HISTORY_BINDING 22
#define GSNAP_BUFFER_INDEX 23
#define DEDUPE_RSCALE_F 0
#define DEDUPE_RSCALE_V 1
#define DEDUPE_TINT 2
#define DEDUPE_DRV_C 3
#define DEDUPE_DRV_F 4
#define DEDUPE_DRV_V 5
#define SETC_TAB 4096
@CPU_OPTS@
typedef struct {uint64_t addr, size;} Range;
typedef struct {uint32_t set, binding; VkDescriptorType type;} VkBindingInfo;
typedef struct {
    VkBindingInfo binds[MAX_BIND]; int nbinds;
    uint8_t bind_order[2][MAX_BIND], nstage[2], nlate[2];
    bool reuse_stage[2];
} Prog;
typedef struct {int ntex; uint32_t cbuf_size[18]; bool uses_gmem, writes_gmem;} ShaderInfo;
typedef struct {VkBuffer buf; uint64_t off; void *ptr;} VkSpan;
typedef struct {int unused;} NvnProgram;
typedef struct {uint8_t rs_group;} TexBackend;
typedef struct {TexBackend *backend;} NvnTexture;
#define RS_OUT 2
static struct {Range ubuf[6][18], sbuf[6][16]; NvnProgram *program; struct {int ncolor; NvnTexture *color[8];} rt;} st;
static float view_shape=1.78f;
float output_view_shape(void) {return view_shape;}
static uint64_t gcopy_gen=1, uub_serial, gw_gen;
static struct {float s;} sdw_fix;
static struct {float sx;} up_fix;
static bool fxaa_off;
static float tex_sc[2][64][2];
static uint32_t tex_wh[2][64][2];
#define LOG(...) ((void)0)
static _Atomic int stage_cpu_options=-1;
volatile int g_opt_dispatch_fast,g_opt_cb_unlink;
static uint64_t draws_total=12345;
static _Atomic uint64_t stage_reuse_calls, stage_reuse_eligible, stage_reuse_hits;
typedef struct {bool failed; int64_t mask; int n; char key[160][48]; int64_t val[160];} ControlReply;
static void control_fail(ControlReply *r,const char *text) {(void)text;r->failed=true;}
static void control_field_int(ControlReply *r,const char *key,int64_t v) {
    if(!strcmp(key,"mask"))r->mask=v;
    assert(r->n<160); snprintf(r->key[r->n],sizeof r->key[0],"%s",key); r->val[r->n++]=v;
}
static int64_t field(const ControlReply *r,const char *key) {
    for(int i=0;i<r->n;i++) if(!strcmp(r->key[i],key)) return r->val[i];
    printf("FAIL: the reply has no field %s\n",key); exit(1);
}
static void vk_tex_ww_stats(uint64_t out[5]) {memset(out,0,5*sizeof out[0]);}
static bool maxwell_gmem_bda;
static uint64_t stat_sets, stat_sets_reused, setc_gen, pending_seq=1;
typedef struct {uint64_t seq,hash,hash2; VkDescriptorSetLayout layout; VkDescriptorSet set; uint32_t n;} SetEnt;
static SetEnt *setc;
static uint64_t captured[MAX_BIND][4];
static int ncaptured, allocations, calls, last_copy_size;
static unsigned char last_copy[4096];
static uint64_t vk_pending_seq(void) {return pending_seq;}
// (what a set holds: its layout and the writes made to it, folded to one word, to compare sets by what they hold)
#define MAX_SETS (1<<22)
static uint64_t set_hash[MAX_SETS];
static VkDescriptorSetLayout alloc_layout[MAX_SETS];
static VkDescriptorSet vk_alloc_set(VkDescriptorSetLayout layout) {
    assert(allocations+1<MAX_SETS);
    alloc_layout[allocations+1]=layout; set_hash[allocations+1]=(uintptr_t)layout*0x9E3779B97F4A7C15ull;
    return (VkDescriptorSet)(uintptr_t)++allocations;
}
static void rec_UpdateDescriptorSets(uint32_t n, const VkWriteDescriptorSet *w) {
    ncaptured=n;
    for(unsigned i=0;i<n;i++) {
        assert(w[i].sType==VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET && !w[i].pNext);
        assert(w[i].descriptorCount==1 && w[i].dstArrayElement==0 && !w[i].pTexelBufferView);
        captured[i][0]=w[i].dstBinding | (uint64_t)w[i].descriptorType<<32;
        if(w[i].pImageInfo) {
            assert(!w[i].pBufferInfo);
            captured[i][1]=(uintptr_t)w[i].pImageInfo->sampler;
            captured[i][2]=(uintptr_t)w[i].pImageInfo->imageView;
            captured[i][3]=w[i].pImageInfo->imageLayout;
        } else {
            captured[i][1]=(uintptr_t)w[i].pBufferInfo->buffer;
            captured[i][2]=w[i].pBufferInfo->offset;
            captured[i][3]=w[i].pBufferInfo->range;
        }
    }
    uint64_t h=(uintptr_t)alloc_layout[allocations]*0x9E3779B97F4A7C15ull+n;
    for(unsigned i=0;i<n;i++) for(int j=0;j<4;j++) {h=(h^captured[i][j])*0x100000001B3ull; h^=h>>29;}
    set_hash[allocations]=h;
}
// What a constant buffer holds, as cbuf_info makes it: a function of the stage, the buffer, the group counts and the state
// the key of a prepared stage covers, and the state its eligibility leaves out (each as the real function reads it: the
// texel grid fix in both stages, the shadow and fxaa fixes in the fragment stage, the HUD layer in the vertex stage).
static uint64_t mix(uint64_t h,uint64_t v) {h^=v+0x9E3779B97F4A7C15ull+(h<<6)+(h>>2); h*=0xFF51AFD7ED558CCDull; return h^(h>>32);}
static bool hud_active(void) {
    if(!st.rt.ncolor || !st.rt.color[0]) return false;
    TexBackend *tb=st.rt.color[0]->backend;
    float shape=getenv("SAVAGEWIND_NO_HUD_FIX") ? 1.0f : view_shape;
    return (tb && tb->rs_group==RS_OUT) || shape<1;
}
static uint64_t mock_state(int stage) {
    uint64_t h=stage+1;
    h=mix(h,pending_seq); h=mix(h,gcopy_gen); h=mix(h,uub_serial); h=mix(h,gw_gen); h=mix(h,(uintptr_t)st.program);
    for(int i=0;i<18;i++) {h=mix(h,st.ubuf[stage][i].addr); h=mix(h,st.ubuf[stage][i].size);}
    for(int i=0;i<16;i++) {h=mix(h,st.sbuf[stage][i].addr); h=mix(h,st.sbuf[stage][i].size);}
    h=mix(h,(up_fix.sx>0) | (stage==1 && sdw_fix.s>1)<<1 | (stage==1 && fxaa_off)<<2 | (stage==0 && hud_active())<<3);
    return h;
}
static VkDescriptorBufferInfo cbuf_info(int stage, ShaderInfo *si, int k, uint32_t x,uint32_t y,uint32_t z) {
    (void)si; calls++;
    uint64_t h=mix(mock_state(stage),k);
    return (VkDescriptorBufferInfo){(VkBuffer)(uintptr_t)(100+stage+((h>>20&0xffffff)<<4)), (uint64_t)(k*256+x+y+z)+((h&0xfff)<<12), 256+(h>>44&0xffff)};
}
static VkDescriptorBufferInfo gmem_info(void) {calls++; return (VkDescriptorBufferInfo){(VkBuffer)51,256,512};}
static VkDescriptorBufferInfo gmem_info_hi(void) {calls++; return (VkDescriptorBufferInfo){(VkBuffer)52,512,1024};}
static VkDescriptorBufferInfo mvec_history_info(void) {calls++; return (VkDescriptorBufferInfo){(VkBuffer)53,768,1536};}
static VkDescriptorBufferInfo gsnap_info(int s) {calls++; return (VkDescriptorBufferInfo){(VkBuffer)54,(uint64_t)s*256,2048};}
static VkSampler vk_default_sampler(bool b) {(void)b;return (VkSampler)71;}
static VkImageView vk_dummy_view(int t) {(void)t;return (VkImageView)72;}
#define TEXTYPE_2D 1
static void texture_info(int stage, ShaderInfo *si,int t,VkDescriptorImageInfo *ii,float sc[2],uint32_t wh[2]) {
    (void)si; calls++;
    *ii=(VkDescriptorImageInfo){(VkSampler)(uintptr_t)(100+t),(VkImageView)(uintptr_t)(200+t+stage),VK_IMAGE_LAYOUT_GENERAL};
    sc[0]=1+t;sc[1]=2+t;wh[0]=128+t;wh[1]=64+t;
}
static VkSpan copy_dedupe_in(int id,const void *p,uint64_t n,uint64_t total,uint64_t align) {
    (void)align; (void)total; calls++; assert(n<=sizeof last_copy);
    memcpy(last_copy,p,n); last_copy_size=n;
    uint64_t h=id+1; for(uint64_t i=0;i<n;i++)h=h*33+((const unsigned char*)p)[i];
    return (VkSpan){(VkBuffer)(uintptr_t)(1000+id),h&~255ull,NULL};
}
static VkSpan copy_dedupe(int id,const void *p,uint64_t n,uint64_t align) {return copy_dedupe_in(id,p,n,n,align);}
static VkDescriptorBufferInfo ubo_span(VkSpan s,uint64_t n) {return (VkDescriptorBufferInfo){s.buf,s.off,n};}
'''

MAIN = r'''
static uint32_t rng=123;
static uint32_t next(void) {rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}

static bool cached;

// ---- bit 256: a prepared stage of the submission, the vertex stage too, from a table of recent ones
typedef struct {Prog p; ShaderInfo si; VkDescriptorImageInfo pre[64]; float sc[64][2]; bool frag;} Mat;
#define NMATS 13
static Mat mats[NMATS];  // 0..5 vertex stages, 6..11 fragment stages, 12 a vertex stage with textures
static NvnTexture tex_a, tex_b;
static TexBackend be_out={RS_OUT}, be_other={1};
static float walk_row[4]={1,2,3,4}, walk_tint[8]={1,2,3,4,5,6,7,8};
static VkDescriptorSetLayout LAY0=(VkDescriptorSetLayout)5;

static void mat_images(int i) {  // (what a draw has resolved for the stage's textures: images and their scales)
    for(int t=0;t<64;t++) {
        mats[i].pre[t]=(VkDescriptorImageInfo){(VkSampler)(uintptr_t)(300+t+i*100),(VkImageView)(uintptr_t)(400+t+i*100),VK_IMAGE_LAYOUT_GENERAL};
        mats[i].sc[t][0]=1+t; mats[i].sc[t][1]=2+t;
    }
}
static void make_mats(void) {
    for(int i=0;i<NMATS;i++) {
        Mat *m=&mats[i]; memset(m,0,sizeof *m);
        m->frag=i>=6 && i<12;
        int set=m->frag, n=0;
        m->si.ntex=m->frag ? 1+i%4 : (i==12 ? 2 : 0);
        for(int k=0;k<1+i%3;k++)
            m->p.binds[n++]=(VkBindingInfo){set,3+k,((i+k)&1) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER};
        if(i%4==1) m->p.binds[n++]=(VkBindingInfo){set,0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER};
        for(int t=0;t<m->si.ntex+(i%2);t++) m->p.binds[n++]=(VkBindingInfo){set,32+t,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER};
        m->p.binds[n++]=(VkBindingInfo){set,18,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER};
        m->p.binds[n++]=(VkBindingInfo){set,19,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER};
        m->p.nbinds=n; stage_plan_init(&m->p);
        mat_images(i);
    }
}

// the arguments of one call: what draw_ passes for a vertex stage (no scale rows, no tint, no textures, no group counts)
// or a fragment stage
typedef struct {Mat *m; VkDescriptorSetLayout L; ShaderInfo *si; const float *row,*tint; uint32_t gx; bool pre;} Call;
static Call call_of(Mat *m) {
    return (Call){m,LAY0,&m->si,m->frag ? walk_row : NULL,m->frag ? walk_tint : NULL,0,m->frag};
}
static VkDescriptorSet go(const Call *c,bool oracle,uint32_t *d,int *n) {
    int s=c->m->frag;
    // (as draw_ does before the fragment stage: the scales of the shader's textures, and of those only)
    if(s) memcpy(tex_sc[1],c->m->sc,sizeof c->m->sc[0]*c->m->si.ntex);
    return (oracle ? stage_set_prepare : stage_set)(&c->m->p,c->L,s,s,c->si,c->row,c->tint,c->gx,0,0,c->pre ? c->m->pre : NULL,d,n);
}
static void fresh(void) {  // (everything the key covers or leaves out back to a plain state; earlier entries are then stale)
    memset(&st,0,sizeof st); up_fix.sx=0; sdw_fix.s=0; fxaa_off=false; view_shape=1.78f;
    memset(tex_sc,0,sizeof tex_sc);
    pending_seq++; setc_gen++; gcopy_gen++; uub_serial++; gw_gen++;
    for(int i=0;i<4;i++) walk_row[i]=1+i;
    for(int i=0;i<8;i++) walk_tint[i]=1+i;
    tex_a.backend=&be_other; tex_b.backend=&be_other;
    for(int i=0;i<NMATS;i++) mat_images(i);
}
static int cost(const Call *c) {int before=calls; go(c,false,NULL,NULL); return calls-before;}
static void must_prepare(const Call *c,const char *what) {
    if(!cost(c)) {printf("FAIL: %s: the stage was reused, it had to be prepared\n",what); exit(1);}
}
static void must_reuse(const Call *c,const char *what) {
    int n=cost(c);
    if(cached ? n!=0 : n==0) {printf("FAIL: %s: the stage was %s\n",what,cached ? "prepared again, it had to be reused" : "reused with the set cache off"); exit(1);}
}

static const char *const key_name[]={"first constant buffer address","first constant buffer size","last constant buffer","first storage range",
    "last storage range size","guest copy generation","constant buffer serial","storing shader generation","submission",
    "set cache generation","program","layout","shader info","options","scale row","tint","image view","image sampler",
    "image layout","texture scale","last texture scale"};
// 1 applied, 0 not for this stage, -1 past the end
static int key_change(int w,Call *c) {
    Mat *m=c->m; int s=m->frag, nt=m->si.ntex;
    static ShaderInfo alt;
    switch(w) {
    case 0: st.ubuf[s][0].addr+=0x40; return 1;
    case 1: st.ubuf[s][0].size++; return 1;
    case 2: st.ubuf[s][17].addr++; return 1;
    case 3: st.sbuf[s][0].addr++; return 1;
    case 4: st.sbuf[s][15].size++; return 1;
    case 5: gcopy_gen++; return 1;
    case 6: uub_serial++; return 1;
    case 7: gw_gen++; return 1;
    case 8: pending_seq++; return 1;
    case 9: setc_gen++; return 1;
    case 10: st.program=(NvnProgram*)((uintptr_t)st.program+1); return 1;
    case 11: c->L=(VkDescriptorSetLayout)((uintptr_t)c->L+1); return 1;
    case 12: alt=m->si; c->si=&alt; return 1;
    case 13: atomic_store(&stage_cpu_options,atomic_load(&stage_cpu_options)^16); return 1;
    case 14: if(!s) return 0; walk_row[0]+=1; return 1;
    case 15: if(!s) return 0; walk_tint[7]+=1; return 1;
    case 16: if(!s) return 0; m->pre[0].imageView=(VkImageView)((uintptr_t)m->pre[0].imageView+1); return 1;
    case 17: if(!s) return 0; m->pre[nt-1].sampler=(VkSampler)((uintptr_t)m->pre[nt-1].sampler+1); return 1;
    case 18: if(!s) return 0; m->pre[0].imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; return 1;
    case 19: if(!s) return 0; m->sc[0][0]+=1; return 1;
    case 20: if(!s) return 0; m->sc[nt-1][1]+=1; return 1;
    }
    return -1;
}

static int64_t wide_field(const ControlReply *r,int s,const char *name) {
    char key[64]; snprintf(key,sizeof key,"wide_%c_%s",s?'f':'v',name); return field(r,key);
}

static void hud_cases(void) {
    // hud_patch_inert against the layers hud_patch acts on: a first target that is the final image's, or any with a view
    // narrower than 16:9 (SAVAGEWIND_NO_HUD_FIX leaves the view alone)
    bool nofix=getenv("SAVAGEWIND_NO_HUD_FIX")!=NULL;
    static const float shapes[]={0.5f,0.8f,0.99f,1.0f,1.78f};
    TexBackend *backends[]={NULL,&be_out,&be_other};
    for(int ncolor=0;ncolor<2;ncolor++) for(int c0=0;c0<3;c0++) for(int b=0;b<3;b++) for(unsigned sh=0;sh<5;sh++) {
        fresh();
        st.rt.ncolor=ncolor; st.rt.color[0]=c0==0 ? NULL : (c0==1 ? &tex_a : &tex_b); tex_a.backend=tex_b.backend=backends[b];
        view_shape=shapes[sh];
        bool active=ncolor && c0 && ((backends[b] && backends[b]->rs_group==RS_OUT) || (!nofix && view_shape<1));
        if(hud_patch_inert()==active || hud_active()!=active) {printf("FAIL: hud_patch_inert %d with ncolor %d color %d backend %d shape %g\n",hud_patch_inert(),ncolor,c0,b,view_shape); exit(1);}
    }
    fresh();
}

static void scripted(void) {
    atomic_store(&stage_cpu_options,256);
    for(int which=0;which<2;which++) {
        Call c=call_of(&mats[which ? 7 : 0]);
        const char *kind=which ? "fragment" : "vertex";
        fresh();
        uint32_t da[32],db[32]; int na=0,nb=0;
        VkDescriptorSet first=go(&c,false,da,&na);
        int before=calls;
        VkDescriptorSet second=go(&c,false,db,&nb);
        if(cached ? (calls!=before || second!=first) : calls==before) {printf("FAIL: %s stage: the same call twice\n",kind); exit(1);}
        assert(na==nb && !memcmp(da,db,na*sizeof da[0]));
        for(int w=0,r;(r=key_change(w,&c))!=-1;w++) {
            if(w>=(int)(sizeof key_name/sizeof *key_name)) {printf("FAIL: more key changes than names\n"); exit(1);}
            if(!r) continue;  // (the fragment stage's own state: not for the vertex stage)
            must_prepare(&c,key_name[w]);
            must_reuse(&c,key_name[w]);
        }
        // what the key leaves out does not force it: texture scales and images past the shader's textures
        if(which) {
            Mat *m=c.m; int nt=m->si.ntex;
            m->pre[nt].imageView=(VkImageView)77; m->sc[nt][0]=9; tex_sc[1][nt][0]=9;
            must_reuse(&c,"texture past the shader's");
            sdw_fix.s=1;
            must_reuse(&c,"a shadow fix that does not apply");
            sdw_fix.s=0;
        }
    }
    // what makes a stage not eligible: prepared at each call, however many times in a row
    {
        Call c=call_of(&mats[1]);
        for(int w=0;w<12;w++) {
            fresh();
            Call d=c; ShaderInfo si=mats[1].si; bool reuse0=mats[1].p.reuse_stage[0];
            const char *what="";
            switch(w) {
            case 0: up_fix.sx=2; what="texel grid fix"; break;
            case 1: d.gx=1; what="group counts"; break;
            case 2: d.row=walk_row; d.tint=walk_tint; what="scale row"; break;
            case 3: d.pre=true; what="pre-resolved textures"; break;
            case 4: st.rt.ncolor=1; st.rt.color[0]=&tex_a; tex_a.backend=&be_out; what="HUD layer (final image)"; break;
            case 5: st.rt.ncolor=1; st.rt.color[0]=&tex_a; tex_a.backend=&be_other; view_shape=0.8f;
                    if(getenv("SAVAGEWIND_NO_HUD_FIX")) continue;
                    what="HUD layer (narrow view)"; break;
            case 6: si.uses_gmem=true; d.si=&si; what="global memory reads"; break;
            case 7: si.writes_gmem=true; d.si=&si; what="global memory writes"; break;
            case 8: mats[1].p.reuse_stage[0]=false; what="stage with storage"; break;
            case 9: d=call_of(&mats[12]); what="vertex stage with textures"; break;
            case 10: d.tint=walk_tint; what="tint"; break;
            case 11: st.rt.ncolor=1; st.rt.color[0]=&tex_a; tex_a.backend=NULL; view_shape=0.5f;
                    if(getenv("SAVAGEWIND_NO_HUD_FIX")) continue;
                    what="HUD layer (target without backend, narrow view)"; break;
            }
            for(int rep=0;rep<3;rep++) must_prepare(&d,what);
            mats[1].p.reuse_stage[0]=reuse0;
        }
        // and the HUD layers that leave a vertex stage alone are reused
        fresh();
        must_prepare(&c,"first call"); must_reuse(&c,"no target");
        st.rt.ncolor=1; st.rt.color[0]=&tex_a; tex_a.backend=&be_other; view_shape=1.78f;
        must_reuse(&c,"a target that is not the final image's, a wide view");  // (nothing the buffer is made from changed)
        tex_a.backend=NULL;
        must_reuse(&c,"a target without a backend, a wide view");
        // the fragment stages that leave it for the full path
        Call f=call_of(&mats[8]);
        for(int w=0;w<9;w++) {
            fresh();
            Call d=f; ShaderInfo si=mats[8].si; bool reuse1=mats[8].p.reuse_stage[1]; const char *what="";
            switch(w) {
            case 0: sdw_fix.s=2; what="shadow fix"; break;
            case 1: up_fix.sx=2; what="texel grid fix"; break;
            case 2: fxaa_off=true; what="fxaa fix"; break;
            case 3: si.uses_gmem=true; d.si=&si; what="global memory reads"; break;
            case 4: si.writes_gmem=true; d.si=&si; what="global memory writes"; break;
            case 5: mats[8].p.reuse_stage[1]=false; what="stage with storage"; break;
            case 6: d.row=NULL; what="no scale row"; break;
            case 7: d.tint=NULL; what="no tint"; break;
            case 8: d.pre=false; what="no pre-resolved textures"; break;
            }
            for(int rep=0;rep<3;rep++) must_prepare(&d,what);
            mats[8].p.reuse_stage[1]=reuse1;
        }
    }
    // a table of recent ones: the stages of one submission, called in turn, are mostly reused (stages that share a slot push
    // each other out: those that have one to themselves must be reused). The constant buffers' addresses, which are part
    // of the slot, are tried until the six fall into distinct slots but one pair, as the draws' buffers do.
    for(int which=0;which<2;which++) {
        fresh();
        uint64_t salt=0, addr[6]; int alone=0;
        for(;salt<5000 && alone<4;salt++) {
            alone=0;
            for(int i=0;i<6;i++) addr[i]=0x1000*(1+i)+salt*0x40*(i+1);
            for(int i=0;i<6;i++) {
                int same=0;
                for(int j=0;j<6;j++) {
                    Range a[3]={{addr[i]},{0},{0}}, b[3]={{addr[j]},{0},{0}};
                    same+=wide_slot(&mats[which*6+i].p,a)==wide_slot(&mats[which*6+j].p,b);
                }
                alone+=same==1;
            }
        }
        if(alone<4) {printf("FAIL: the test cannot spread its stages over the table\n"); exit(1);}
        int hits=0;
        for(int pass=0;pass<3;pass++) for(int i=0;i<6;i++) {
            Mat *m=&mats[which*6+i];
            st.ubuf[which][0].addr=addr[i];
            Call c=call_of(m);
            if(pass==0) go(&c,false,NULL,NULL);
            else hits+=cost(&c)==0;
        }
        if(cached && hits<2*alone) {printf("FAIL: %s stages in turn: %d of 12 repeated calls reused, %d stages have a slot alone\n",which ? "fragment" : "vertex",hits,alone); exit(1);}
    }
    fresh();
}

static void mutate_state(void) {
    int s=next()%2, i;
    static const float shapes[]={0.8f,1.0f,1.78f,1.78f};
    switch(next()%20) {
    case 0: case 1: i=next()%3; st.ubuf[s][i].addr=0x1000*(1+next()%3); break;  // (the first three feed the slot)
    case 2: i=next()%18; st.ubuf[s][i].addr=0x1000*(next()%3); break;
    case 3: i=next()%18; st.ubuf[s][i].size=next()%3*16; break;
    case 4: i=next()%16; st.sbuf[s][i].addr=0x2000*(next()%3); break;
    case 5: i=next()%16; st.sbuf[s][i].size=next()%3*64; break;
    case 6: st.program=(NvnProgram*)(uintptr_t)(next()%3); break;
    case 7: walk_row[next()%4]=next()%3; break;
    case 8: walk_tint[next()%8]=next()%3; break;
    case 9: {Mat *m=&mats[next()%NMATS]; int t=next()%6; m->pre[t].imageView=(VkImageView)(uintptr_t)(400+t+next()%3);} break;
    case 10: {Mat *m=&mats[next()%NMATS]; int t=next()%6; m->pre[t].sampler=(VkSampler)(uintptr_t)(300+t+next()%3);} break;
    case 11: mats[next()%NMATS].sc[next()%6][next()%2]=next()%3; break;
    case 12: up_fix.sx=next()%8==0 ? 2 : 0; break;
    case 13: sdw_fix.s=next()%8==0 ? 2 : next()%2; break;
    case 14: fxaa_off=next()%8==0; break;
    case 15: st.rt.ncolor=next()%3!=0; break;
    case 16: st.rt.color[0]=next()%3==0 ? NULL : (next()%2 ? &tex_a : &tex_b); break;
    case 17: tex_a.backend=next()%3==0 ? NULL : (next()%3==0 ? &be_out : &be_other);
             tex_b.backend=next()%3==0 ? NULL : (next()%3==0 ? &be_out : &be_other); break;
    case 18: view_shape=shapes[next()%4]; break;
    case 19: {
        static const int modes[]={256,256,256,256|8,256|16,256|4,256|1,256|2,256|128,256|512,8,0,8|16,16};
        atomic_store(&stage_cpu_options,modes[next()%(sizeof modes/sizeof *modes)]);
    } break;
    }
}

// Random states and calls, at the table's slots as they fall: whatever the stage_set answers, a set of the same contents and the
// same dynamic offsets (appended to what was there) as preparing it would, for any mask that moves the paths about.
static void walk(int steps) {
    fresh();
    atomic_store(&stage_cpu_options,256);
    for(int it=0;it<steps;it++) {
        int r=next()%100;
        if(r<3) {
            switch(next()%5) {case 0:pending_seq++;break; case 1:gcopy_gen++;break; case 2:uub_serial++;break; case 3:gw_gen++;break; case 4:setc_gen++;break;}
        } else if(r<20) mutate_state();
        Mat *m=&mats[next()%NMATS];
        Call c=call_of(m);
        if(next()%6==0) c.L=(VkDescriptorSetLayout)((uintptr_t)LAY0+1);
        switch(next()%24) {
        case 0: if(!m->frag) c.gx=1+next()%3; break;  // (draw_ passes no group counts to a fragment stage; the compute stage does not come here)
        case 1: if(!m->frag) {c.row=walk_row; c.tint=walk_tint;} break;
        case 2: if(m->frag) c.pre=false; break;
        case 3: if(m->frag) c.row=NULL; break;
        case 4: if(m->frag) c.tint=NULL; break;
        }
        uint32_t d1[40],d2[40]; int n1=next()%3,n2;
        for(int i=0;i<n1;i++) d1[i]=d2[i]=0xAA000+i;
        n2=n1;
        bool dyn=next()%8!=0;
        VkDescriptorSet got=go(&c,false,dyn ? d1 : NULL,dyn ? &n1 : NULL);
        VkDescriptorSet want=go(&c,true,d2,&n2);
        if(set_hash[(uintptr_t)got]!=set_hash[(uintptr_t)want] || (dyn && (n1!=n2 || memcmp(d1,d2,n1*sizeof d1[0])))) {
            printf("FAIL: step %d, mat %d, mask %d: the answer differs from preparing the stage (set %llx, %llx; %d, %d offsets)\n",
                   it,(int)(m-mats),stage_cpu_opts(),(unsigned long long)set_hash[(uintptr_t)got],(unsigned long long)set_hash[(uintptr_t)want],n1,n2);
            exit(1);
        }
    }
    fresh();
}
int main(void) {
    cached=getenv("SAVAGEWIND_VK_NO_SET_CACHE")==NULL;
    make_mats();
    for(int trial=0;trial<1500;trial++) {
        Prog p={0}; ShaderInfo si={.ntex=4};
        int n=0;
        for(int set=0;set<2;set++) {
            for(int k=0;k<17;k++) if(next()%3==0)
                p.binds[n++]=(VkBindingInfo){set,k,next()%2?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC};
            for(int k=0;k<6;k++) if(next()%2)
                p.binds[n++]=(VkBindingInfo){set,32+k,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER};
            p.binds[n++]=(VkBindingInfo){set,18,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER};
            p.binds[n++]=(VkBindingInfo){set,19,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER};
            for(int k=20;k<=23;k++) if(next()%2)
                p.binds[n++]=(VkBindingInfo){set,k,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
        }
        p.nbinds=n;
        if(trial%10==0) p.nbinds=0;
        for(int k=n-1;k>0;k--) {int j=next()%(k+1);VkBindingInfo v=p.binds[k];p.binds[k]=p.binds[j];p.binds[j]=v;}
        stage_plan_init(&p);
        VkDescriptorImageInfo pre[64];
        for(int t=0;t<64;t++) pre[t]=(VkDescriptorImageInfo){(VkSampler)(uintptr_t)(300+t),(VkImageView)(uintptr_t)(400+t),VK_IMAGE_LAYOUT_GENERAL};
        for(int o=0;o<64;o++)for(int set=0;set<2;set++) {
            int options=(o&31)|(o&32 ? 256 : 0);
            float row[4]={1,2,3,4}, tint[8]={1,2,3,4,5,6,7,8};
            uint32_t da[32]={0},db[32]={0};int na=0,nb=0;
            memset(tex_sc,0,sizeof tex_sc);memset(tex_wh,0,sizeof tex_wh);
            free(setc);setc=NULL;calls=0;ncaptured=-1;
            reference_stage_set(&p,(VkDescriptorSetLayout)5,set,set,&si,row,tint,3,4,5,set?pre:NULL,da,&na);
            uint64_t expected[MAX_BIND][4];memcpy(expected,captured,sizeof expected);
            int expected_n=ncaptured, expected_calls=calls;
            memset(tex_sc,0,sizeof tex_sc);memset(tex_wh,0,sizeof tex_wh);
            free(setc);setc=NULL;calls=0;ncaptured=-1;atomic_store(&stage_cpu_options,options);
            VkDescriptorSet ds=stage_set(&p,(VkDescriptorSetLayout)5,set,set,&si,row,tint,3,4,5,set?pre:NULL,db,&nb);
            assert(ncaptured==expected_n && calls==expected_calls);
            assert(expected_n<=0 || !memcmp(expected,captured,expected_n*sizeof captured[0]));
            assert(na==nb && !memcmp(da,db,na*sizeof da[0]));
            int old_alloc=allocations; ncaptured=-1;
            VkDescriptorSet again=stage_set(&p,(VkDescriptorSetLayout)5,set,set,&si,row,tint,3,4,5,set?pre:NULL,NULL,NULL);
            assert(cached ? again==ds : again!=ds);
            assert(allocations==old_alloc+!cached && (cached ? ncaptured==-1 : ncaptured==expected_n));
            setc_gen++; stage_set(&p,(VkDescriptorSetLayout)5,set,set,&si,row,tint,3,4,5,set?pre:NULL,NULL,NULL);
            assert(allocations==old_alloc+1+!cached);
        }
    }
    for(int stage=0;stage<6;stage++)for(int size=0;size<=2200;size++) {
        ShaderInfo si={0};si.cbuf_size[0]=size;
        for(int k=0;k<16;k++) {
            st.ubuf[stage][k]=(Range){((uint64_t)next()<<32)|next(),next()};
            st.sbuf[stage][k]=(Range){((uint64_t)next()<<32)|next(),next()};
        }
        unsigned char expected[4096];
        driver_reference(stage,&si,11,12,13);int n=last_copy_size;memcpy(expected,last_copy,n);
        atomic_store(&stage_cpu_options,4);driver_candidate(stage,&si,11,12,13);
        assert(last_copy_size==n && !memcmp(expected,last_copy,n));
    }
    // A qualifying material stage: actual preparation is bypassed on a hit,
    // and every independent input or lifetime change forces preparation again.
    Prog p={.nbinds=4,.binds={{1,3,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC},
        {1,32,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER},{1,18,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},
        {1,19,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER}}};
    ShaderInfo si={.ntex=1}; stage_plan_init(&p);
    VkDescriptorImageInfo images[1]={{(VkSampler)5,(VkImageView)6,VK_IMAGE_LAYOUT_GENERAL}};
    float row[4]={1,1},tint[8]={0};uint32_t da[32],db[32];int na=0,nb=0;
    atomic_store(&stage_cpu_options,10);gcopy_gen++;
    stage_set(&p,(VkDescriptorSetLayout)5,1,1,&si,row,tint,0,0,0,images,da,&na);
    int before=calls;
    stage_set(&p,(VkDescriptorSetLayout)5,1,1,&si,row,tint,0,0,0,images,db,&nb);
    assert(na==nb && !memcmp(da,db,na*sizeof da[0]));
    assert(cached ? calls==before : calls>before);
    for(int change=0;change<16;change++) {
        switch(change) {
        case 0:st.ubuf[1][0].addr++;break; case 1:st.ubuf[1][0].size++;break;
        case 2:st.sbuf[1][0].addr++;break; case 3:st.sbuf[1][0].size++;break;
        case 4:gcopy_gen++;break; case 5:uub_serial++;break;case 6:gw_gen++;break;
        case 7:pending_seq++;break;case 8:setc_gen++;break;
        case 9:images[0].imageView=(VkImageView)7;break;case 10:images[0].sampler=(VkSampler)8;break;
        case 11:images[0].imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;break;
        case 12:row[0]++;break;case 13:tint[0]++;break;case 14:tex_sc[1][0][0]++;break;
        case 15:st.program=(NvnProgram*)1;break;
        }
        before=calls;
        stage_set(&p,(VkDescriptorSetLayout)5,1,1,&si,row,tint,0,0,0,images,NULL,NULL);
        assert(calls>before);
        before=calls;
        stage_set(&p,(VkDescriptorSetLayout)5,1,1,&si,row,tint,0,0,0,images,NULL,NULL);
        assert(cached ? calls==before : calls>before);
    }
    for(int special=0;special<6;special++) {
        if(special==0)sdw_fix.s=2;if(special==1)up_fix.sx=2;if(special==2)fxaa_off=true;
        if(special==3)si.uses_gmem=true;if(special==4)si.writes_gmem=true;if(special==5)p.reuse_stage[1]=false;
        before=calls;
        stage_set(&p,(VkDescriptorSetLayout)5,1,1,&si,row,tint,0,0,0,images,NULL,NULL);
        assert(calls>before);
        sdw_fix.s=up_fix.sx=0;fxaa_off=si.uses_gmem=si.writes_gmem=false;p.reuse_stage[1]=true;
    }
    char over[16];snprintf(over,sizeof over,"%d",CPU_OPTS_MAX+1);
    char *bad[]={"", over, "-1", "one", "1x", "99999999999999999999999999999999999999"};
    for(unsigned i=0;i<sizeof bad/sizeof *bad;i++) {
        char *argv[]={"cpuopts",bad[i]};ControlReply reply={0};
        atomic_store(&stage_cpu_options,3);g_opt_dispatch_fast=1;g_opt_cb_unlink=0;
        stage_cpu_command(2,argv,&reply);
        assert(reply.failed && stage_cpu_opts()==3 && g_opt_dispatch_fast==1 && g_opt_cb_unlink==0);  // (a refused mask changes nothing)
    }
    for(int mask=0;mask<=CPU_OPTS_MAX;mask++) {
        char value[16];snprintf(value,sizeof value,"%d",mask);char *argv[]={"cpuopts",value};ControlReply reply={0};
        stage_cpu_command(2,argv,&reply);assert(!reply.failed && reply.mask==mask && stage_cpu_opts()==mask);
        // the guest's threads read plain variables: they follow the mask, bit for bit
        assert(g_opt_dispatch_fast==((mask&1024)!=0) && g_opt_cb_unlink==((mask&2048)!=0));
        assert(field(&reply,"draws")==12345);
    }
    // R2 (mask 256): the stage's own tests, then random states against preparing the stage
    hud_cases();
    scripted();
    walk(400000);
    {
        ControlReply reply={0};char *argv[]={"cpuopts"};
        stage_cpu_command(1,argv,&reply);
        for(int s=0;s<2;s++) {
            #define F(name) wide_field(&reply,s,name)
            int64_t calls_=F("calls"),eligible=F("eligible"),hits=F("hits");
            int64_t misses=F("miss_empty")+F("miss_program")+F("miss_generation")+F("miss_ubuf")+F("miss_sbuf")+F("miss_scale_row")+F("miss_images")+F("miss_scales");
            if(eligible!=hits+misses || calls_<eligible) {printf("FAIL: %s counters: %lld calls, %lld eligible, %lld hits, %lld misses\n",s?"fragment":"vertex",(long long)calls_,(long long)eligible,(long long)hits,(long long)misses); exit(1);}
            if(!cached) {if(calls_) {printf("FAIL: counted a call with the set cache off\n"); exit(1);} continue;}
            // every reason is met, and the table is used a good part of the time
            int64_t reasons[]={F("miss_empty"),F("miss_program"),F("miss_generation"),F("miss_ubuf"),F("miss_sbuf"),F("miss_scale_row"),F("miss_images"),F("miss_scales")};
            for(int i=0;i<8;i++) {
                bool fragment_only=i>=5;
                if(fragment_only && !s ? reasons[i]!=0 : reasons[i]==0) {printf("FAIL: %s stage, miss reason %d seen %lld times\n",s?"fragment":"vertex",i,(long long)reasons[i]); exit(1);}
            }
            if(hits*20<eligible || !calls_) {printf("FAIL: %s stage: %lld hits of %lld eligible\n",s?"fragment":"vertex",(long long)hits,(long long)eligible); exit(1);}
            printf("%s stage: %lld calls, %lld eligible, %lld hits (%.0f%%)\n",s?"fragment":"vertex",(long long)calls_,(long long)eligible,(long long)hits,100.0*hits/eligible);
            #undef F
        }
        assert(field(&reply,"sb_skip_draws")==0 && field(&reply,"sb_drawn_layer2")==0);
    }
    fresh();
    // R1 hash (mask 16): contents that differ never share a set, whatever the table evicts; the same contents share one.
    for(int fast=0;fast<2;fast++) {
        atomic_store(&stage_cpu_options,fast?16:0);
        free(setc);setc=NULL;setc_gen++;
        Prog q={.nbinds=4,.binds={{0,3,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},{0,4,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},
            {0,32,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER},{0,33,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER}}};
        ShaderInfo qi={.ntex=2}; stage_plan_init(&q);
        static int owner[400000]; static uint32_t key[400000];
        memset(owner,0,sizeof owner);
        int same=0, base=allocations;
        for(int i=0;i<150000;i++) {
            uint32_t x=next()%9000, y=next()%7;  // (cbuf_info's offset is x+y: contents are told apart by x+y and the layout)
            VkDescriptorSet d=stage_set(&q,(VkDescriptorSetLayout)(uintptr_t)(5+y%2),0,0,&qi,NULL,NULL,x,y,0,NULL,NULL,NULL);
            uint32_t k=(x+y)*2+y%2+1; unsigned id=(unsigned)((int)(uintptr_t)d-base);
            assert(id<400000);
            if(owner[id]) {assert(key[id]==k); same++;} else {owner[id]=1; key[id]=k;}
        }
        assert(!cached || same>0);
    }
    free(setc);puts("stage preparation: 96,000 descriptor comparisons, 13,206 driver layouts, reuse invalidation, 400,000 random calls of the table (bit 256), 150,000 hashed set lookups and live switches passed");
}
'''


def driver(source, name):
    text = function(source, 'cbuf_info')
    start = text.index('if (k == 0)')
    pos = text.index('{', start)
    end, depth = pos + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return ('static VkDescriptorBufferInfo ' + name +
            '(int stage, ShaderInfo *si,uint32_t gx,uint32_t gy,uint32_t gz)' + text[pos:end])


def line(source, prefix):
    i = source.index(prefix)
    return source[i:source.index('\n', i)]


def build(current, reference, mutate=None):
    reference_finish = function(reference, 'set_finish').replace('set_finish(', 'reference_set_finish(')
    reference_stage = function(reference, 'stage_set').replace('stage_set(', 'reference_stage_set(').replace(
        'set_finish(', 'reference_set_finish(')
    assert re.search(r'\{"cpuopts", "\[0-%s\]"' % re.search(r'#define CPU_OPTS_MAX (\d+)', current).group(1), current), \
        'the registered usage text does not follow CPU_OPTS_MAX'
    options = '\n'.join([line(current, '#define CPU_OPTS_MAX'), line(current, '#define CPU_OPTS_USAGE'),
                         line(current, '#define SB_LAYERS'),
                         line(current, 'static _Atomic uint64_t sb_skip_draws'),
                         line(current, 'enum { W_CALLS'), line(current, 'static _Atomic uint64_t wide_stat'),
                         line(current, '#define WIDE_N')])
    wide_types = '\n'.join([typedef(current, 'ReuseHead'), typedef(current, 'ReuseFrag'),
                            line(current, 'static ReuseHead wide_vtab'), line(current, 'static ReuseFrag wide_ftab')])
    parts = [FIXTURE.replace('@CPU_OPTS@', options), wide_types, function(current, 'stat_bump'),
             function(current, 'stage_cpu_publish'), function(current, 'stage_cpu_opts'), function(current, 'stage_cpu_command'), function(current, 'mum64'),
             function(current, 'stage_plan_init'),
             reference_finish, reference_stage, function(current, 'set_finish'),
             typedef(current, 'PrepScratch'), function(current, 'stage_set_prepare_in'),
             function(current, 'stage_set_prepare_stack'), function(current, 'stage_set_prepare'),
             function(current, 'hud_patch_inert'), function(current, 'wide_slot'), function(current, 'wide_match'), function(current, 'stage_set_wide'),
             function(current, 'stage_set'), driver(reference, 'driver_reference'),
             driver(current, 'driver_candidate'), MAIN]
    text = '\n'.join(parts)
    if mutate:
        old, new = mutate
        # (in the code of the table only: the text from its first line to the end of stage_set_wide)
        a = text.index('static int wide_match')
        b = text.index('static VkDescriptorSet stage_set(', a)
        assert text[a:b].count(old) == 1, old
        text = text[:a] + text[a:b].replace(old, new, 1) + text[b:]
    return text


def compile_test(text, temp, name='test'):
    path = Path(temp) / (name + '.c')
    path.write_text(text)
    exe = Path(temp) / name
    r = subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-O1', '-g', '-fsanitize=address,undefined',
                        '-fno-sanitize-recover=all', '-w', str(path), '-o', str(exe)], capture_output=True, text=True)
    if r.returncode:
        sys.exit(r.stderr)
    return exe


def run(text, temp, envs, quiet=False, exe=None):
    exe = exe or compile_test(text, temp)
    for extra in envs:
        env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
        for key in ('SAVAGEWIND_VK_NO_SET_CACHE', 'SAVAGEWIND_NO_HUD_FIX'):
            env.pop(key, None)
        env.update(extra)
        r = subprocess.run([str(exe)], env=env, capture_output=True, text=True)
        if r.returncode:
            if not quiet:
                print(r.stdout + r.stderr)
            return False
        if not quiet:
            print(r.stdout.strip())
    return True


# What each must be caught by (--self-check): the table's key, its eligibility, and what it hands back. Each text is in
# wide_match or stage_set_wide, once.
MUTANTS = [
    ('forgets the submission', 'e->seq != seq || ', ''),
    ('forgets the set cache generation', 'e->sets != setc_gen || ', ''),
    ('forgets the guest copy generation', 'e->guest != guest || ', ''),
    ('forgets the constant buffer serial', 'e->serial != uub_serial || ', ''),
    ('forgets the storing shader generation', 'e->gpu != gw_gen ||', ''),
    ('forgets the program', 'e->program != st.program || ', ''),
    ('forgets the layout', ' || e->layout != layout', ''),
    ('forgets the shader info', 'e->si != si || ', ''),
    ('forgets the constant buffers', 'if (memcmp(e->ubuf, st.ubuf[stage], sizeof e->ubuf)) return W_UBUF;', ''),
    ('forgets the storage ranges', 'if (memcmp(e->sbuf, st.sbuf[stage], sizeof e->sbuf)) return W_SBUF;', ''),
    ('forgets the scale row', 'memcmp(e->row, row, sizeof e->row) || ', ''),
    ('forgets the tint', ' || memcmp(e->tint, tint, sizeof e->tint)', ''),
    ('forgets the images', 'if (memcmp(f->images, pre_ii, sizeof *pre_ii * si->ntex)) why = W_IMAGES;', 'if (0) why = W_IMAGES;'),
    ('forgets the texture scales', 'else if (memcmp(f->scales, tex_sc[1], sizeof f->scales[0] * si->ntex)) why = W_SCALES;', 'else if (0) why = W_SCALES;'),
    ('reuses a HUD layer', ' && hud_patch_inert();', ';'),
    ('reuses with the texel grid fix', ' && up_fix.sx <= 0;', ';'),
    ('reuses with the shadow fix', ' && sdw_fix.s <= 1', ''),
    ('reuses with the fxaa fix', ' && !fxaa_off;', ';'),
    ('reuses with global memory reads', ' && !si->uses_gmem', ''),
    ('reuses with global memory writes', ' && !si->writes_gmem', ''),
    ('reuses a stage with storage', 'p->reuse_stage[set] && ', ''),
    ('reuses a vertex stage with textures', 'si->ntex == 0 && ', ''),
    ('reuses a vertex stage with a scale row', ' && !rscale_row0 && !tint &&', ' &&'),
    ('reuses a vertex stage with group counts', ' && !gx && !gy && !gz', ''),
    ('reuses without pre-resolved textures', 'si->ntex <= 64 && pre_ii && rscale_row0 && tint', 'si->ntex <= 64'),
    ('hands back no offsets', '*ndyn += e->n; }', '}'),
    ('hands back another set', 'return e->descriptor;', 'return descriptor_of_last;'),
    ('counts no eligible call', 'stat_bump(&stat[W_ELIGIBLE]);', ''),
    ('counts no miss', 'stat_bump(&stat[why]);', ''),
]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--reference', required=True)
    ap.add_argument('--self-check', action='store_true', help='break the table in many ways; each must be caught')
    args = ap.parse_args()
    root = Path(__file__).resolve().parent.parent
    current = (root / 'runtime/nvn/vulkan/exec.c').read_text()
    reference = Path(args.reference).read_text()
    envs = [{}, {'SAVAGEWIND_VK_NO_SET_CACHE': '1'}, {'SAVAGEWIND_NO_HUD_FIX': '1'}]
    with tempfile.TemporaryDirectory(prefix='swr-stage-test-') as temp:
        if not run(build(current, reference), temp, envs):
            return 1
        if args.self_check:
            missed = 0
            exes = []
            for i, (name, old, new) in enumerate(MUTANTS):  # (all of them built first: one that no longer matches or compiles stops the run at once)
                text = build(current, reference, (old, new)).replace('descriptor_of_last', '(VkDescriptorSet)(uintptr_t)1')
                exes.append(compile_test(text, temp, 'mutant%d' % i))
            for (name, old, new), exe in zip(MUTANTS, exes):
                caught = not run(None, temp, envs[:1], quiet=True, exe=exe)
                print('%-44s %s' % (name, 'caught' if caught else 'MISSED'), flush=True)
                missed += not caught
            return 1 if missed else 0
    return 0


if __name__ == '__main__':
    sys.exit(main())
