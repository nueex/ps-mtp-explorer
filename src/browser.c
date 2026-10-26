/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#include "psmtp/browser.h"
#include "psmtp/navigator.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

void mtp_print_name(FILE *out,const char *s) {
    const unsigned char *p=(const unsigned char *)s;
    fputc('"',out);
    for(;*p;++p) {
        if(p[0]==0xe2 && p[1] && p[2] && p[1]==0x80 && p[2]>=0xaa && p[2]<=0xae) {
            fprintf(out,"\\u%04x",0x202a+p[2]-0xaa); p+=2;
        } else if(p[0]==0xe2 && p[1] && p[2] && p[1]==0x81 && p[2]>=0xa6 && p[2]<=0xa9) {
            fprintf(out,"\\u%04x",0x2066+p[2]-0xa6); p+=2;
        } else if(p[0]==0xc2 && p[1]>=0x80 && p[1]<=0x9f) {
            fprintf(out,"\\u%04x",0x80+p[1]-0x80); ++p;
        } else if(*p<32 || *p==127 || *p=='"' || *p=='\\') fprintf(out,"\\x%02x",*p);
        else fputc(*p,out);
    }
    fputc('"',out);
}
static void diagnostic(FILE *out,mtp_client *c,const char *stage,int e) {
    fprintf(out,"stage=%s status=%d (%s) op=0x%04x tid=%" PRIu32 " response=0x%04x poisoned=%d\n",
            stage,e,mtp_error_string(e),c->last_op,c->last_tid,c->response,c->poisoned);
}
static int parse_hex(char **s,uint32_t *v) {
    char *end; unsigned long long x;
    while(**s==' ' || **s=='\t') ++*s;
    if(!**s || **s=='-' || **s=='+') return 0;
    errno=0; x=strtoull(*s,&end,16);
    if(end==*s || errno || x>UINT32_MAX) return 0;
    *s=end; *v=(uint32_t)x; return 1;
}
static int end_line(const char *s) { return strspn(s," \t\r\n")==strlen(s); }
int mtp_browser(mtp_client *c,FILE *input,FILE *out,int names) {
    mtp_device_info device; uint32_t storages[MTP_MAX_STORAGES],ids[MTP_MAX_ITEMS];
    size_t ns=0,n=0,i,j; int e, result=0; char line[128];
    const uint16_t required[]={0x1002,0x1003,0x1004,0x1005,0x1007,0x1008};
    e=mtp_get_device_info(c,&device); diagnostic(out,c,"device-info",e); if(e) return e;
    fprintf(out,"device-info standard=%u vendor-extension=0x%08" PRIx32 " operations=%zu (identity strings suppressed)\n",
            device.standard_version,device.vendor_extension,device.operation_count);
    for(i=0;i<sizeof(required)/sizeof(required[0]);++i) if(!mtp_supports(&device,required[i])) {
        fprintf(out,"missing-operation=0x%04x\n",required[i]); return MTP_UNSUPPORTED;
    }
    e=mtp_open_session(c,1); diagnostic(out,c,"open-session",e); if(e) return e;
    e=mtp_get_storage_ids(c,storages,MTP_MAX_STORAGES,&ns); diagnostic(out,c,"storage-ids",e);
    if(e) { result=e; goto cleanup; }
    for(i=0;i<ns;++i) {
        mtp_storage_info si;
        e=mtp_get_storage_info(c,storages[i],&si); diagnostic(out,c,"storage-info",e);
        if(e) { result=e; goto cleanup; }
        fprintf(out,"storage=%08" PRIx32 " capacity=%" PRIu64 " free=%" PRIu64 " access=%u\n",storages[i],si.capacity,si.free_bytes,si.access);
    }
    fprintf(out,"Commands: ls <storage-hex> <parent-hex>; root parent=ffffffff; q.\nNames %s. Handles are valid only during this session.\n",names?"enabled":"hidden (launch with --names to display)");
    for(;;) {
        char *s; uint32_t storage,parent; int known=0;
        fprintf(out,"mtp> "); fflush(out);
        if(!fgets(line,sizeof(line),input)) break;
        if(!strchr(line,'\n') && !feof(input)) {
            int ch; while((ch=fgetc(input))!='\n' && ch!=EOF) {} fprintf(out,"input too long\n"); continue;
        }
        if(line[0]=='q' && end_line(line+1)) break;
        if(strncmp(line,"ls ",3)) { fprintf(out,"expected ls or q\n"); continue; }
        s=line+3;
        if(!parse_hex(&s,&storage) || !parse_hex(&s,&parent) || !end_line(s) || !parent) {
            fprintf(out,"invalid storage/parent; parent 0 (all objects) is disabled\n"); continue;
        }
        for(i=0;i<ns;++i) if(storages[i]==storage) known=1;
        if(!known) { fprintf(out,"unknown storage\n"); continue; }
        if(parent!=MTP_ROOT) {
            mtp_object_info folder;
            e=mtp_get_object_info(c,parent,&folder); diagnostic(out,c,"selected-folder",e);
            if(e) { result=e; goto cleanup; }
            if(folder.storage!=storage || !mtp_is_folder(&folder)) {
                fprintf(out,"parent is not a folder in selected storage\n"); continue;
            }
        }
        e=mtp_get_handles(c,storage,parent,ids,MTP_MAX_ITEMS,&n); diagnostic(out,c,"object-handles",e);
        if(e) { result=e; goto cleanup; }
        fprintf(out,"items=%zu\n",n);
        for(j=0;j<n;++j) {
            mtp_object_info obj;
            e=mtp_get_object_info(c,ids[j],&obj); diagnostic(out,c,"object-info",e);
            if(e) { result=e; goto cleanup; }
            if(obj.storage!=storage || (parent!=MTP_ROOT && obj.parent!=parent) ||
               (parent==MTP_ROOT && obj.parent!=0 && obj.parent!=MTP_ROOT)) {
                fprintf(out,"inconsistent storage/parent metadata\n"); result=MTP_PROTOCOL; goto cleanup;
            }
            fprintf(out,"handle=%08" PRIx32 " kind=%s format=%04x size32=%" PRIu32 "%s",
                    ids[j],mtp_is_folder(&obj)?"folder":"object",obj.format,obj.size32,
                    obj.size32==UINT32_MAX?" (size unknown or >=4GiB)":"");
            if(names) { fputs(" name=",out); mtp_print_name(out,obj.name); }
            fputc('\n',out);
        }
    }
cleanup:
    e=mtp_close_session(c); diagnostic(out,c,"close-session",e);
    return result?result:e;
}
