#!/usr/bin/env python3
"""Compare executor descriptor preparation with the frozen pre-experiment path.

The fixture is synthetic. The actual C functions are extracted and compiled with
resource providers that record their calls and descriptor contents. No game or GPU
is needed. Pass --reference to a saved exec.c from before the candidate changes.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def function(source, name):
    start = source.index('static ', source.rfind('\n', 0, source.index(name + '(')) + 1)
    brace = source.index('{', source.index(name + '(', start))
    depth = 1
    pos = brace + 1
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    return source[start:pos]


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
typedef struct {uint64_t addr, size;} Range;
typedef struct {uint32_t set, binding; VkDescriptorType type;} VkBindingInfo;
typedef struct {
    VkBindingInfo binds[MAX_BIND]; int nbinds;
    uint8_t bind_order[2][MAX_BIND], nstage[2], nlate[2];
} Prog;
typedef struct {int ntex; uint32_t cbuf_size[18];} ShaderInfo;
typedef struct {VkBuffer buf; uint64_t off; void *ptr;} VkSpan;
static struct {Range ubuf[6][18], sbuf[6][16];} st;
static float tex_sc[2][64][2];
static uint32_t tex_wh[2][64][2];
#define LOG(...) ((void)0)
static _Atomic int stage_cpu_options=-1;
typedef struct {bool failed; int mask;} ControlReply;
static void control_fail(ControlReply *r,const char *text) {(void)text;r->failed=true;}
static void control_field_int(ControlReply *r,const char *key,int mask) {(void)key;r->mask=mask;}
static bool maxwell_gmem_bda;
static uint64_t stat_sets, stat_sets_reused, setc_gen, pending_seq=1;
typedef struct {uint64_t seq,hash,hash2; VkDescriptorSetLayout layout; VkDescriptorSet set; uint32_t n;} SetEnt;
static SetEnt *setc;
static uint64_t captured[MAX_BIND][4];
static int ncaptured, allocations, calls, last_copy_size;
static unsigned char last_copy[4096];
static uint64_t vk_pending_seq(void) {return pending_seq;}
static VkDescriptorSet vk_alloc_set(VkDescriptorSetLayout layout) {(void)layout; return (VkDescriptorSet)(uintptr_t)++allocations;}
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
}
static VkDescriptorBufferInfo cbuf_info(int stage, ShaderInfo *si, int k, uint32_t x,uint32_t y,uint32_t z) {
    (void)si; calls++; return (VkDescriptorBufferInfo){(VkBuffer)(uintptr_t)(100+stage), (uint64_t)(k*256+x+y+z), 256};
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
int main(void) {
    bool cached=getenv("SAVAGEWIND_VK_NO_SET_CACHE")==NULL;
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
        for(int options=0;options<8;options++)for(int set=0;set<2;set++) {
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
    char *bad[]={"", "8", "-1", "one", "1x", "99999999999999999999999999999999999999"};
    for(unsigned i=0;i<sizeof bad/sizeof *bad;i++) {
        char *argv[]={"cpuopts",bad[i]};ControlReply reply={0};
        atomic_store(&stage_cpu_options,3);stage_cpu_command(2,argv,&reply);
        assert(reply.failed && stage_cpu_opts()==3);
    }
    for(int mask=0;mask<8;mask++) {
        char value[2]={(char)('0'+mask),0};char *argv[]={"cpuopts",value};ControlReply reply={0};
        stage_cpu_command(2,argv,&reply);assert(!reply.failed && reply.mask==mask && stage_cpu_opts()==mask);
    }
    free(setc);puts("stage preparation: 24,000 descriptor comparisons, 13,206 driver layouts and live switches passed");
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


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--reference', required=True)
    args = ap.parse_args()
    root = Path(__file__).resolve().parent.parent
    current = (root / 'runtime/nvn/vulkan/exec.c').read_text()
    reference = Path(args.reference).read_text()
    reference_finish = function(reference, 'set_finish').replace('set_finish(', 'reference_set_finish(')
    reference_stage = function(reference, 'stage_set').replace('stage_set(', 'reference_stage_set(').replace(
        'set_finish(', 'reference_set_finish(')
    source = '\n'.join([FIXTURE, function(current, 'stage_cpu_opts'), function(current, 'stage_cpu_command'),
                        function(current, 'stage_plan_init'),
                        reference_finish, reference_stage, function(current, 'set_finish'),
                        function(current, 'stage_set'), driver(reference, 'driver_reference'),
                        driver(current, 'driver_candidate'), MAIN])
    with tempfile.TemporaryDirectory(prefix='swr-stage-test-') as temp:
        path = Path(temp) / 'test.c'
        path.write_text(source)
        exe = Path(temp) / 'test'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-O1', '-g',
                        '-fsanitize=address,undefined', str(path), '-o', str(exe)], check=True)
        for disabled in (False, True):
            env = dict(os.environ)
            env.pop('SAVAGEWIND_VK_NO_SET_CACHE', None)
            if disabled:
                env['SAVAGEWIND_VK_NO_SET_CACHE'] = '1'
            subprocess.run([str(exe)], env=env, check=True)


if __name__ == '__main__':
    main()
