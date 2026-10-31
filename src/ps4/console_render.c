/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#include "console_render.h"
#include "psmtp/copy.h"
#include "ui_draw.h"
#include "ui_font.h"
#include "device_label.h"
#include "psmtp/ui_format.h"
#include "psmtp/copy_paths.h"
#include "psmtp/version.h"
#include "psmtp/keyboard.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <stdlib.h>
#define WHITE UI_WHITE
#define MUTED UI_MUTED
#define LINE UI_LINE
#define RGB UI_RGB
#define PANEL UI_PANEL
static const psmtp_console_selection *panel;
static const mtp_navigator *phone;
static const psmtp_local_browser *disk;
static const psmtp_locations *devices;
static int show_names;
static const psmtp_location *active(void) {return devices && devices->count?&devices->entries[devices->active]:NULL;}
static void bytes_text(char *out,size_t cap,uint64_t bytes) {char *p;psmtp_ui_size(out,cap,bytes);p=strstr(out,".0 ");if(p) memmove(p,p+2,strlen(p+2)+1);}
static void chevron(int x,int y,int down,uint32_t c) {
    if(down) {ui_line(x,y,x+8,y+8,2,c,255);ui_line(x+8,y+8,x+16,y,2,c,255);}
    else {ui_line(x,y,x+7,y+8,2,c,255);ui_line(x+7,y+8,x,y+16,2,c,255);}
}
static void text_lines(int x,int y,int size,int width,int lines,const char *s,uint32_t color) {
    char copy[512];size_t n=0;int line=0;if(!s) return;
    while(*s && line<lines) {
        size_t next=1;unsigned char c=(unsigned char)*s;
        if((c&0xe0)==0xc0)next=2;else if((c&0xf0)==0xe0)next=3;else if((c&0xf8)==0xf0)next=4;
        if(next>strlen(s)) next=1;if(n+next>=sizeof(copy)-1) break;
        memcpy(copy+n,s,next);copy[n+next]=0;
        if(n && psmtp_font_width(copy,size)>width) {copy[n]=0;if(line==lines-1) {if(n+3<sizeof(copy)) strcat(copy,"...");ui_text(x,y+line*(size+7),size,width,copy,color);return;}ui_text(x,y+line*(size+7),size,width,copy,color);++line;n=0;if(line==lines) return;}
        memcpy(copy+n,s,next);n+=next;s+=next;
    }
    copy[n]=0;if(n && line<lines) ui_text(x,y+line*(size+7),size,width,copy,color);
}
static void control_hint(int x,int y,int px,int width,const char *s,uint32_t color){
    static const char *const words[]={"Cross","Circle","Triangle","Square","D-pad","L1","R1","L2","R2","L3","R3","Touchpad","Options","X"};
    static const int kinds[]={UI_BTN_CROSS,UI_BTN_CIRCLE,UI_BTN_TRIANGLE,UI_BTN_SQUARE,UI_BTN_DPAD,UI_BTN_L1,UI_BTN_R1,UI_BTN_L2,UI_BTN_R2,UI_BTN_L3,UI_BTN_R3,UI_BTN_TOUCH,UI_BTN_OPTIONS,UI_BTN_CROSS};
    int start=x;size_t at=0,plain=0;if(!s)return;
    while(s[at]){unsigned i;int found=-1;size_t n=0;
        for(i=0;i<sizeof(words)/sizeof(words[0]);++i){size_t z=strlen(words[i]);if((!at || !((s[at-1]>='A' && s[at-1]<='Z') || (s[at-1]>='a' && s[at-1]<='z'))) && !strncmp(s+at,words[i],z) && !((s[at+z]>='A' && s[at+z]<='Z') || (s[at+z]>='a' && s[at+z]<='z') || (s[at+z]>='0' && s[at+z]<='9'))){found=(int)i;n=z;break;}}
        if(found>=0){char text[512];size_t length=at-plain;if(length>=sizeof(text))length=sizeof(text)-1;memcpy(text,s+plain,length);text[length]=0;ui_text(x,y,px,width-(x-start),text,color);x+=psmtp_font_width(text,px);if(x-start+px+5>width)return;ui_button(kinds[found],x,y+2,px+2,color);x+=px+7;at+=n;plain=at;}else ++at;
        if(x-start>=width)return;
    }ui_text(x,y,px,width-(x-start),s+plain,color);
}
static void controller(int x,int y,int kind,const char *text) {
    uint32_t c=kind==1?RGB(212,38,65):kind==2?RGB(61,177,162):WHITE;
    ui_ring(x+19,y+19,19,1,c,255);ui_button(kind,x+2,y+2,34,c);ui_text(x+58,y+2,28,240,text,WHITE);
}
static void footer(int progress,const char *status) {
    if(progress==3)controller(78,986,1,"Cancel");
    else if(progress) {controller(78,986,0,progress==2?"Show":"Hide");controller(244,986,1,"Cancel");}
    else {controller(78,979,0,"Open");controller(244,979,1,"Back");controller(398,979,2,"Options");}
    if(status && *status) control_hint(78,1040,18,1750,status,MUTED);
}
static void header(void) {
    char clock_text[48],release[128];time_t now;struct tm *clock;
    snprintf(release,sizeof(release),"v%s",panel && panel->display_version?panel->display_version:PSMTP_VERSION);
    ui_art(UI_WORDMARK,64,43,570,48,0);ui_text(651,49,31,350,release,MUTED);ui_text(651+psmtp_font_width(release,31)+10,54,25,160,"by nueex",MUTED);ui_art(UI_PLATFORM,1514,48,176,39,0);
    if(panel && panel->clock_text) snprintf(clock_text,sizeof(clock_text),"%s",panel->clock_text);
    else {now=time(NULL);clock=localtime(&now);if(clock) {strftime(clock_text,sizeof(clock_text),"%I:%M %p",clock);if(clock_text[0]=='0') memmove(clock_text,clock_text+1,strlen(clock_text));}else strcpy(clock_text,"--:--");}
    ui_text(1737,47,28,170,clock_text,WHITE);
    if(panel && panel->clipboard_label) ui_text(1020,54,23,450,panel->clipboard_label,WHITE);
}
static const char *device_name(size_t index){
    const psmtp_location *location=&devices->entries[index];
    return location->kind==PSMTP_LOCATION_INTERNAL?"Internal Storage":location->kind==PSMTP_LOCATION_USB && devices->count<=3?"USB Storage":location->label;
}
static int device_height(size_t index,int heights[PSMTP_LOCATION_MAX]){
    if(!heights[index]){char lines[96][96];unsigned n=psmtp_device_label_lines(device_name(index),lines);heights[index]=n>1?(int)n*33+30:82;}
    return heights[index];
}
static void sidebar(void) {
    size_t count=devices?devices->count:0,first=0,last=0,i,cursor=devices?(panel && panel->drawer_focus?devices->selected:devices->active):0;
    int y=164,settings_y,heights[PSMTP_LOCATION_MAX]={0},used=0;
    if(count>PSMTP_LOCATION_MAX)count=PSMTP_LOCATION_MAX;
    if(count){
        if(panel && panel->drawer_settings)cursor=count-1;if(cursor>=count)cursor=count-1;
        first=cursor;used=device_height(cursor,heights);
        while(first && cursor-first<5 && used+device_height(first-1,heights)<=636){--first;used+=heights[first];}
    }
    for(i=first;i<count && i<first+6;++i) {
        const psmtp_location *location=&devices->entries[i];char lines[96][96];unsigned line,n;
        int height=device_height(i,heights),current=!(panel && panel->drawer_focus) && i==devices->active;
        int focus=panel && panel->drawer_focus && !panel->drawer_settings && i==devices->selected;
        uint32_t c=current||focus?WHITE:RGB(218,211,214);if(y+height>800)break;
        if(current || focus)ui_box(30,y,307,height,14,RGB(58,55,57),focus?WHITE:RGB(197,190,192),focus?3:2);
        if(location->kind==PSMTP_LOCATION_INTERNAL)ui_art(UI_DRIVE,57,y+22,35,35,c);
        else if(location->kind==PSMTP_LOCATION_USB)ui_art(UI_USB,59,y+20,31,40,c);
        else ui_art(UI_PHONE,59,y+18,32,44,c);
        n=psmtp_device_label_lines(device_name(i),lines);
        for(line=0;line<n;++line)ui_text(124,y+(n==1?23:15)+(int)line*33,26,212,lines[line],c);
        y+=height;last=i+1;
    }
    if(!count)ui_text(48,190,25,280,"Finding devices...",MUTED);
    settings_y=y+40;ui_rect(46,settings_y-28,289,1,RGB(70,64,67));
    {int focus=panel && panel->drawer_focus && panel->drawer_settings;uint32_t c=focus?WHITE:RGB(218,211,214);
    if(focus)ui_box(30,settings_y,307,82,14,RGB(58,55,57),WHITE,3);
    ui_art(UI_SETTINGS,56,settings_y+22,38,38,c);ui_text(124,settings_y+20,28,210,"Settings",c);}
    if(first)ui_text(48,132,17,280,"Earlier devices above",MUTED);
    if(last<count)ui_text(48,settings_y+86,17,280,"More devices below",MUTED);
}
static void toolbar(void) {
    char path[256]="";const char *leaf;

    if(!(panel && panel->display_items)) {
        if(phone && phone->parent!=MTP_ROOT) snprintf(path,sizeof(path),"%s",phone->folder_name);
        else if(disk && strcmp(disk->path,disk->root)) {leaf=strrchr(disk->path,'/');snprintf(path,sizeof(path),"%s",leaf?leaf+1:disk->path);}
        if(path[0]) ui_text(411,160,25,580,path,MUTED);
    }
    ui_box(panel && panel->list_view?1106:1033,154,55,52,3,RGB(30,29,33),RGB(47,44,49),1);
    ui_rect(1049,168,10,10,WHITE);ui_rect(1063,168,10,10,WHITE);ui_rect(1049,182,10,10,WHITE);ui_rect(1063,182,10,10,WHITE);
    {int i;for(i=0;i<3;++i) {ui_rect(1120,168+i*10,4,4,WHITE);ui_rect(1130,168+i*10,17,3,WHITE);}}
    {char label[64];snprintf(label,sizeof(label),"Sort: %s",psmtp_ui_sort_label(panel?panel->sort_mode:0));ui_text(1193,162,22,240,label,WHITE);}chevron(1425,176,1,WHITE);
    if(panel && panel->toolbar_focus){int focus=panel->toolbar_focus;ui_box(focus==1?1033:focus==2?1106:1180,150,focus==3?266:55,60,5,0,WHITE,3);}
}
static size_t item_count(void) {return panel && panel->display_items?panel->display_count:phone?phone->count:disk?disk->count:0;}
static size_t item_selected(void) {return panel && panel->display_items?panel->display_selected:phone?phone->selected:disk?disk->selected:0;}
static int get_item(size_t index,psmtp_ui_item *out) {
    memset(out,0,sizeof(*out));out->thumbnail=-1;if(index>=item_count()) return 0;
    if(panel && panel->display_items) {*out=panel->display_items[index];return 1;}
    if(phone) {
        const mtp_nav_entry *e;
        if(phone->catalog) e=&phone->catalog[index];else {if(index<phone->page_start || index-phone->page_start>=phone->page_count) return 0;e=&phone->entries[index-phone->page_start];}
        out->name=e->info.name;out->modified=e->info.modified;out->folder=mtp_is_folder(&e->info);out->bytes=e->exact_size?e->bytes:e->info.size32;out->unknown=e->info.size32==UINT32_MAX && !e->exact_size;
    } else if(disk) {const psmtp_local_entry *e=&disk->entries[index];out->name=e->name;out->modified=e->modified;out->bytes=e->bytes;out->folder=e->folder;}
    if(!show_names) out->name="Name hidden";return out->name!=NULL;
}
static const psmtp_metadata *current_metadata;
static const psmtp_metadata *metadata_at(size_t i){return panel && panel->metadata?panel->metadata(panel->metadata_user,i):NULL;}
static void file_art(const psmtp_ui_item *item,int x,int y,int width,int height) {
    if(current_metadata && current_metadata->image){ui_image(current_metadata->image,current_metadata->image_width,current_metadata->image_height,x+8,y+4,width-16,height-8);return;}
    if(item->folder) ui_art(UI_FOLDER,x+(width-124)/2,y+(height-101)/2,124,101,0);
    else if(item->thumbnail>=UI_THUMB_PKG && item->thumbnail<=UI_THUMB_VIDEO) {int w=item->thumbnail==UI_THUMB_PKG?164:218;ui_art(item->thumbnail,x+(width-w)/2,y+(height-120)/2,w,120,0);}
    else ui_art(UI_DOCUMENT,x+(width-80)/2,y+(height-108)/2,80,108,0);
}
static void grid_tile(size_t position,int selected,const psmtp_ui_item *item) {
    int x=404+(int)(position%4)*264,y=226+(int)(position/4)*232;char meta[80];
    int height=item->folder?216:228;
    ui_box(x,y,248,height,selected?7:3,RGB(34,31,32),selected?RGB(214,203,206):RGB(44,41,43),selected?3:1);
    if(selected) ui_box(x+4,y+4,240,height-8,3,RGB(36,32,33),0,0);
    file_art(item,x,y+(item->folder?22:17),248,120);ui_center(x+14,y+(item->folder?135:151),26,220,item->name,WHITE);
    if(item->folder) {if(item->items_known) snprintf(meta,sizeof(meta),"%zu items",item->items);else strcpy(meta,"Folder");}
    else if(item->unknown) strcpy(meta,"Size unavailable");else bytes_text(meta,sizeof(meta),item->bytes);
    ui_text(x+20,y+(item->folder?175:193),22,214,meta,MUTED);
}
static void list_row(size_t position,int selected,const psmtp_ui_item *item) {
    int y=266+(int)position*39;char size[48];if(selected) ui_box(404,y-3,1040,38,3,RGB(63,58,61),RGB(205,198,202),2);
    ui_art(item->folder?UI_FOLDER:UI_DOCUMENT,419,y+1,29,28,0);ui_text(466,y,25,590,item->name,WHITE);ui_text(1081,y+2,21,160,psmtp_ui_kind(item->name,item->folder),MUTED);
    if(item->folder) strcpy(size,"--");else if(item->unknown) strcpy(size,"Unavailable");else bytes_text(size,sizeof(size),item->bytes);ui_text(1284,y+2,22,150,size,WHITE);
}
static void modified_text(const char *value,char *date,size_t date_cap,char *clock,size_t clock_cap) {
    static const char *months[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};int year,month,day,hour,minute;
    strcpy(date,"Unavailable");clock[0]=0;
    if(!value || sscanf(value,"%4d%2d%2dT%2d%2d",&year,&month,&day,&hour,&minute)!=5 || month<1 || month>12 || day<1 || day>31 || hour<0 || hour>23 || minute<0 || minute>59) return;
    snprintf(date,date_cap,"%s %d, %d",months[month-1],day,year);snprintf(clock,clock_cap,"%d:%02d %s",hour%12?hour%12:12,minute,hour>=12?"PM":"AM");
}
static void details(const psmtp_ui_item *input) {
    psmtp_ui_item value=*input;const psmtp_ui_item *item=&value;const psmtp_metadata *m=current_metadata;
    if(m && m->items_known){value.items_known=1;value.items=(size_t)m->items;}
    char count[64],size[64],date[64],clock[48];int offset=psmtp_font_width(item->name,33)>342?42:0;ui_box(1516,166,360,264,1,RGB(31,29,30),RGB(44,40,42),1);
    if(item->folder) ui_art(UI_FOLDER,1600,235,192,156,0);else file_art(item,1516,190,360,218);
    text_lines(1532,438,33,342,2,item->name,WHITE);ui_rect(1516,509+offset,360,1,RGB(64,59,63));
    ui_text(1535,534+offset,25,160,"Type",WHITE);ui_text(1703,534+offset,25,178,psmtp_ui_kind(item->name,item->folder),WHITE);
    if(item->folder) {ui_text(1535,582+offset,25,160,"Items",WHITE);if(item->items_known) snprintf(count,sizeof(count),"%zu",item->items);else strcpy(count,"Not counted");ui_text(1703,582+offset,25,175,count,item->items_known?WHITE:MUTED);}
    ui_text(1535,(item->folder?628:587)+offset,25,160,"Size",WHITE);
    if(item->folder && (!(panel && panel->display_items) || !item->items_known)) strcpy(size,"--");else if(item->unknown) strcpy(size,"Unavailable");else bytes_text(size,sizeof(size),item->bytes);ui_text(1703,(item->folder?628:587)+offset,25,175,size,WHITE);
    if(item->folder && panel && panel->display_items && item->items_known) ui_text(1703,660+offset,24,175,"(on disk)",WHITE);
    modified_text(item->modified,date,sizeof(date),clock,sizeof(clock));ui_text(1535,724+offset,25,160,"Modified",WHITE);ui_text(1703,724+offset,24,175,date,strcmp(date,"Unavailable")?WHITE:MUTED);ui_text(1703,756+offset,24,175,clock,WHITE);
    if(m){char extra[128];int y=804+offset;
        if(m->title[0]){ui_text(1535,y,23,342,m->title,WHITE);y+=34;}
        if(m->title_id[0]){snprintf(extra,sizeof(extra),"%s  %s",m->title_id,m->version);ui_text(1535,y,21,342,extra,MUTED);y+=32;}
        if(m->width && m->height){snprintf(extra,sizeof(extra),"%u x %u%s%s",m->width,m->height,m->codec[0]?"  ":"",m->codec);ui_text(1535,y,23,342,extra,WHITE);y+=32;}
        if(m->duration_ms){snprintf(extra,sizeof(extra),"Duration  %llu:%02llu",(unsigned long long)(m->duration_ms/60000),(unsigned long long)(m->duration_ms/1000%60));ui_text(1535,y,23,342,extra,WHITE);}
    }
}
static void explorer(const char *status) {
    size_t count=item_count(),selected=item_selected(),first,i,limit=panel && panel->list_view?16:12;psmtp_ui_item item;toolbar();first=(selected/limit)*limit;
    if(panel && panel->list_view) {ui_text(466,228,20,500,"Name",MUTED);ui_text(1081,228,20,160,"Type",MUTED);ui_text(1284,228,20,150,"Size",MUTED);ui_rect(404,258,1040,1,LINE);}
    for(i=first;i<count && i<first+limit;++i) if(get_item(i,&item)) {current_metadata=metadata_at(i);if(panel && panel->list_view) list_row(i-first,i==selected,&item);else grid_tile(i-first,i==selected,&item);}
    if(!count) {ui_art(UI_FOLDER,860,420,112,91,0);ui_center(420,547,31,1000,disk && disk->error?"Could not open this folder":active()?"This folder is empty":"Choose a storage device",WHITE);control_hint(420,602,24,1000,disk && disk->error?"Check storage access, or choose another device.":active()?"Press Circle to go back.":"Connect a phone or USB drive to begin.",MUTED);}
    current_metadata=metadata_at(selected);
    if(panel && panel->detail_override) details(panel->detail_override);else if(get_item(selected,&item)) details(&item);else {ui_box(1516,166,360,264,1,RGB(31,29,30),RGB(44,40,42),1);ui_art(UI_FOLDER,1600,235,192,156,0);ui_text(1532,446,31,344,"Nothing selected",MUTED);}footer(panel && panel->transfer_active?2:0,status);
}
static void menu_overlay(void) {
    static const char *const context[]={"Open","New Folder","Copy","Move","Paste","Rename","Delete","Properties","Exit PSMTP"};static const int icons[]={UI_FOLDER,UI_FOLDER,UI_COPY,UI_MOVE,UI_COPY,UI_RENAME,UI_DELETE,UI_INFO,UI_SETTINGS};
    size_t i,count=PSMTP_MENU_COUNT;int x=630,y=204,w=336,h=88+(int)count*67;
    ui_shadow(x,y,w,h,10);ui_box(x,y,w,h,10,0xe1100f12u,RGB(60,56,62),2);ui_text(x+34,y+22,28,w-68,"Options",WHITE);ui_rect(x+26,y+70,w-54,1,LINE);
    for(i=0;i<count;++i) {
        size_t item=i;
        int yy=y+88+(int)i*67,enabled=item==PSMTP_MENU_EXIT || (item==PSMTP_MENU_RENAME?panel && panel->rename_allowed:item==PSMTP_MENU_NEW_FOLDER?panel && panel->can_create:item==PSMTP_MENU_PASTE?panel && panel->can_paste:panel && panel->can_select);int selected=panel && panel->index==i;uint32_t c=!enabled?RGB(108,103,109):selected?RGB(38,33,38):WHITE;
        if(selected) {ui_box(x+3,yy-8,w-6,70,10,0,0x26ffffffu,4);ui_box(x+7,yy-4,w-14,62,7,RGB(218,213,219),WHITE,2);}
        ui_art(icons[item],x+36,yy+10,31,33,c);
        ui_text(x+104,yy+10,27,w-120,context[item],c);if(i+1<count) ui_rect(x+26,yy+62,w-54,1,LINE);
    }
    if(panel && panel->index==PSMTP_MENU_RENAME && !panel->rename_allowed) ui_text(78,1040,19,1750,"Select an item on writable storage to rename it.",MUTED);
}
static unsigned percent(uint64_t done,uint64_t total) {double p=total?(double)done/(double)total*100:0;return p>100?100:(unsigned)(p+0.5);}
static int transfer_verifying(void){return panel->phase==MTP_COPY_LOCAL || panel->phase==MTP_COPY_SOURCE || panel->phase==MTP_COPY_PUBLISH;}
static const char *transfer_phase_text(void){
    return panel->view==PSMTP_VIEW_INSTALL_PROGRESS?(!panel->install_preparing && (panel->install_known || panel->total)?"Installing package...":"Preparing installation..."):panel->phase==7?"Deleting items...":panel->phase==6?"Scanning selected items...":panel->phase==5?"Preparing storage...":panel->phase==2?"Verifying...":panel->phase==3?"Checking source...":panel->phase==4?"Finishing transfer...":panel->action==1?"Moving 1 item...":"Copying 1 item...";
}
static void transfer_static(void){
    ui_shadow(612,398,712,286,13);ui_box(612,398,712,286,13,RGB(29,27,28),RGB(56,51,56),2);ui_text(650,430,29,630,transfer_phase_text(),WHITE);ui_text(650,479,26,630,panel->name?panel->name:"Selected items",MUTED);
    if(transfer_verifying()){
        ui_text(650,548,24,630,"Please keep both devices connected.",MUTED);
        ui_rect(0,951,1920,129,RGB(21,18,19));footer(1,"Integrity check in progress.");return;
    }
    ui_box(650,527,563,22,5,RGB(57,53,61),RGB(66,61,69),1);
    ui_box(1140,612,144,50,7,RGB(49,45,50),RGB(77,70,80),2);ui_center(1140,620,26,144,"Cancel",WHITE);
    ui_rect(0,951,1920,129,RGB(21,18,19));footer(panel->view==PSMTP_VIEW_INSTALL_PROGRESS?3:1,panel->view==PSMTP_VIEW_INSTALL_PROGRESS?"Keep PSMTP open and the source connected. Circle stops this installation.":panel->display_items?"":"Circle cancels at a completed operation. Keep both devices connected.");
}
static void transfer_dynamic(void){
    char size[64],total[64],progress[192],pct[24];unsigned p;
    if(panel->animation_ms){unsigned dot;for(dot=0;dot<3;++dot)ui_box(1278+(int)dot*12,596,6,6,3,((panel->animation_ms/100)%3)==dot?WHITE:RGB(72,68,76),0,0);}
    if(transfer_verifying())return;
    p=panel->view==PSMTP_VIEW_INSTALL_PROGRESS && panel->install_known?(unsigned)panel->install_percent:percent(panel->done,panel->total);
    if(panel->view==PSMTP_VIEW_INSTALL_PROGRESS && !panel->install_known && !panel->total)ui_box(650+(int)((panel->elapsed_ms/10)%440),527,120,22,4,RGB(202,198,208),0,0);else if(p)ui_box(650,527,(int)(563*p/100),22,4,RGB(202,198,208),0,0);
    if(panel->view==PSMTP_VIEW_INSTALL_PROGRESS && !panel->install_known && !panel->total)strcpy(pct,"...");else snprintf(pct,sizeof(pct),"%u%%",p);ui_text(1241,521,25,74,pct,WHITE);
    if(panel->phase==7 || panel->phase==6)snprintf(progress,sizeof(progress),"%" PRIu64 " of %" PRIu64 " items",panel->done,panel->total);else{bytes_text(size,sizeof(size),panel->done);bytes_text(total,sizeof(total),panel->total);snprintf(progress,sizeof(progress),"%s of %s",size,total);}
    if(panel->phase==MTP_COPY_RECEIVE && panel->view!=PSMTP_VIEW_INSTALL_PROGRESS){char rate[128];psmtp_transfer_text(&panel->transfer,rate,sizeof(rate));ui_text(650,612,22,480,rate,MUTED);}
    if(panel->view==PSMTP_VIEW_INSTALL_PROGRESS && !panel->total)snprintf(progress,sizeof(progress),"%s",panel->install_known?"System installer progress":"Waiting for system progress...");
    ui_text(650,565,24,630,progress,MUTED);
}
static void transfer_overlay(void){transfer_static();transfer_dynamic();}
static void install_confirmation(void){
    char size[64];bytes_text(size,sizeof(size),panel->source_total);
    ui_shadow(535,288,850,506,12);ui_box(535,288,850,506,12,PANEL,LINE,2);ui_text(577,318,31,766,"Install this package?",WHITE);
    text_lines(577,376,27,766,2,panel->name,WHITE);ui_text(577,467,24,766,size,MUTED);
    ui_text(577,521,22,150,"Source",MUTED);ui_text(745,521,25,598,panel->destination?panel->destination:"Selected device",WHITE);
    ui_text(577,565,21,766,panel->notice_text?panel->notice_text:"",MUTED);
    if(panel->action==5){
        ui_text(577,611,23,766,"Install directly from phone.",MUTED);
        ui_text(577,648,22,766,"Keep PSMTP open and your phone connected.",MUTED);
    }else if(panel->action==3){
        ui_text(577,611,23,766,"Copy from phone, then install.",MUTED);
        ui_text(577,648,22,766,"The saved package stays available until installation finishes.",MUTED);
    }else ui_text(577,623,23,766,"The source PKG stays on your device.",MUTED);
    ui_box(1020,708,300,51,7,RGB(204,198,207),WHITE,1);ui_button(UI_BTN_CROSS,1052,718,28,RGB(34,29,34));ui_center(1094,716,26,200,"Install",RGB(34,29,34));control_hint(582,718,25,400,"Circle  Cancel",WHITE);
    ui_rect(0,951,1920,129,RGB(21,18,19));controller(78,986,0,"Install");controller(290,986,1,"Cancel");
}
static void review_overlay(int operation) {
    char size[64],counts[160];const char *title=operation?(panel->action==2?"Delete selected items":panel->action==1?"Move selected items":"Copy selected items"):"Copy selected items";
    ui_shadow(535,288,850,506,12);ui_box(535,288,850,506,12,RGB(29,27,28),RGB(60,54,61),2);ui_text(577,318,31,766,title,WHITE);text_lines(577,376,27,766,2,panel->name,WHITE);
    bytes_text(size,sizeof(size),panel->source_total);snprintf(counts,sizeof(counts),"%zu files  /  %zu folders  /  %s",panel->files,panel->folders,size);ui_text(577,467,24,766,counts,MUTED);
    if(operation && panel->action==2) {ui_text(577,531,26,766,panel->folder?"The folder and all its contents will be deleted.":"This file will be deleted permanently.",WHITE);ui_text(577,578,23,766,"There is no recycle bin. Verify your selection before continuing.",MUTED);}
    else {ui_text(577,521,22,766,"Destination",MUTED);text_lines(577,556,25,766,2,panel->destination?panel->destination:PSMTP_COPY_DIRECTORY,WHITE);ui_text(577,639,21,766,panel->action==1?"Source removal follows successful integrity verification.":"Existing files are preserved. Every copied file is verified.",MUTED);}
    ui_box(1020,708,300,51,7,RGB(204,198,207),WHITE,1);ui_button(UI_BTN_CROSS,1052,718,28,RGB(34,29,34));ui_center(1094,716,26,200,operation && panel->action==2?"Delete":panel->action==1?"Move":"Copy",RGB(34,29,34));control_hint(582,718,25,400,"Circle  Cancel",WHITE);
    ui_rect(0,951,1920,129,RGB(21,18,19));controller(78,986,0,"Confirm");controller(290,986,1,"Cancel");
}
static void notice_overlay(void) {
    ui_shadow(535,340,850,360,12);ui_box(535,340,850,360,12,PANEL,LINE,2);
    ui_text(577,378,31,766,panel->notice_title?panel->notice_title:"Transfer unavailable",WHITE);
    {char text[512],*line,*next;int row=0;snprintf(text,sizeof(text),"%s",panel->notice_text?panel->notice_text:"Please check the destination and try again.");
    line=text;do{next=strchr(line,'\n');if(next)*next++=0;ui_text(577,447+row*38,25,766,line,MUTED);line=next;}while(line && ++row<3);}
    ui_box(1120,610,210,52,7,RGB(49,45,50),LINE,2);ui_button(UI_BTN_CROSS,1140,621,27,WHITE);ui_center(1178,618,25,130,"OK",WHITE);
    ui_rect(0,951,1920,129,RGB(21,18,19));controller(78,986,0,"OK");controller(244,986,1,"Back");
}
static void properties_overlay(void) {
    psmtp_ui_item item={0};char size[64],date[64],clock[48];
    if(panel && panel->name) {item.name=panel->name;item.bytes=panel->source_total;item.folder=panel->folder;item.unknown=panel->size_unknown;item.modified=panel->modified;item.thumbnail=-1;}else if(!get_item(item_selected(),&item)) return;
    ui_shadow(540,220,840,660,13);ui_box(540,220,840,660,13,RGB(29,27,28),RGB(60,54,61),2);ui_art(UI_INFO,578,250,36,37,WHITE);ui_text(639,248,31,680,"Properties",WHITE);text_lines(580,308,30,758,2,item.name,WHITE);
    ui_rect(580,398,760,1,LINE);ui_text(580,420,25,200,"Type",MUTED);ui_text(830,420,25,510,psmtp_ui_kind(item.name,item.folder),WHITE);ui_text(580,466,25,200,"Size",MUTED);if(item.folder || item.unknown) strcpy(size,"Unavailable");else bytes_text(size,sizeof(size),item.bytes);ui_text(830,466,25,510,size,WHITE);
    modified_text(item.modified,date,sizeof(date),clock,sizeof(clock));ui_text(580,512,25,200,"Modified",MUTED);ui_text(830,512,25,510,date,WHITE);ui_text(1100,512,25,240,clock,WHITE);
    {const psmtp_metadata *m=metadata_at(item_selected());char extra[160];
    if(m && m->title[0]){ui_text(580,558,22,190,"Title",MUTED);ui_text(830,558,22,510,m->title,WHITE);ui_text(580,594,22,190,"Content ID",MUTED);ui_text(830,594,19,510,m->content_id,WHITE);}
    else if(m && m->width && m->height){snprintf(extra,sizeof(extra),"%u x %u  %s",m->width,m->height,m->codec);ui_text(580,558,22,190,"Dimensions",MUTED);ui_text(830,558,22,510,extra,WHITE);if(m->duration_ms){snprintf(extra,sizeof(extra),"%llu:%02llu",(unsigned long long)(m->duration_ms/60000),(unsigned long long)(m->duration_ms/1000%60));ui_text(580,594,22,190,"Duration",MUTED);ui_text(830,594,22,510,extra,WHITE);}}
    }
    ui_text(580,642,22,758,"Full path",MUTED);text_lines(580,678,20,758,3,panel && panel->path?panel->path:disk?disk->path:"/",WHITE);
    ui_box(1140,800,180,49,7,RGB(49,45,50),RGB(77,70,80),2);ui_center(1140,806,26,180,"Close",WHITE);
    ui_rect(0,951,1920,129,RGB(21,18,19));controller(78,986,0,"Close");controller(244,986,1,"Back");
}
static void rename_overlay(void) {
    size_t i;ui_shadow(360,220,1200,650,12);ui_box(360,220,1200,650,12,PANEL,LINE,2);ui_text(405,248,32,1110,panel->action==4?"New Folder":"Rename",WHITE);
    ui_box(405,312,1110,62,5,RGB(18,16,18),WHITE,2);ui_text(423,322,28,1074,panel->edit_name?panel->edit_name:"",WHITE);
    for(i=0;i<PSMTP_KEY_COUNT;++i){int x,y,w,h,selected=panel->index==i,action=psmtp_key_action(i);uint32_t c=selected?RGB(30,26,30):WHITE;char text[2]={psmtp_key_character(i,panel->keyboard_shift),0};psmtp_key_rect(i,&x,&y,&w,&h);
        ui_box(x,y,w,h,5,selected?RGB(218,213,219):RGB(44,40,45),selected?WHITE:LINE,2);
        if(action==PSMTP_KEY_BACKSPACE){int cx=x+w/2;ui_line(cx-21,y+28,cx-9,y+14,2,c,255);ui_line(cx-9,y+14,cx+23,y+14,2,c,255);ui_line(cx+23,y+14,cx+23,y+42,2,c,255);ui_line(cx+23,y+42,cx-9,y+42,2,c,255);ui_line(cx-9,y+42,cx-21,y+28,2,c,255);ui_button(UI_BTN_CROSS,cx-6,y+18,20,c);}
        else if(action==PSMTP_KEY_SHIFT){ui_line(x+w/2,y+12,x+w/2-13,y+27,2,c,255);ui_line(x+w/2,y+12,x+w/2+13,y+27,2,c,255);ui_line(x+w/2,y+12,x+w/2,y+43,3,c,255);}
        else ui_center(x,y+8,27,w,action==PSMTP_KEY_CLEAR?"Clear":action==PSMTP_KEY_SAVE?(panel->action==4?"Create":"Save"):i==42?"Space":text,c);
    }
    control_hint(408,774,22,1110,panel->action==4?"Cross Add   Square Erase   Triangle Shift   L1 Clear   R1 Create   Circle Cancel":"Cross Add   Square Erase   Triangle Shift   L1 Clear   R1 Save   Circle Cancel",WHITE);
    if(panel->edit_error)ui_text(408,821,21,1110,panel->edit_error,RGB(244,173,55));
}
static void sort_overlay(void){
    size_t i;ui_shadow(640,214,640,638,12);ui_box(640,214,640,638,12,PANEL,LINE,2);ui_text(680,246,32,560,"Sort by",WHITE);
    for(i=0;i<PSMTP_SORT_COUNT;++i){int y=316+(int)i*65,selected=panel->index==i;uint32_t c=selected?RGB(30,26,30):WHITE;
        ui_box(666,y-6,588,58,7,selected?RGB(218,213,219):0,selected?WHITE:LINE,selected?2:1);ui_ring(698,y+22,10,2,c,255);if((int)i==panel->sort_mode)ui_box(693,y+17,10,10,5,c,0,0);ui_text(733,y+5,26,488,psmtp_ui_sort_label((int)i),c);
    }control_hint(681,798,22,560,"Cross Apply    Circle Cancel",WHITE);
}
/* Settings paragraphs wrap at words; measure with the actual embedded font. */
static void settings_copy(int x,int y,int size,int width,int maximum,const char *text,uint32_t color){
    char line[256];size_t used=0;int row=0;
    while(*text && row<maximum){const char *end;size_t n;while(*text==' ')++text;if(!*text)break;end=strchr(text,' ');n=end?(size_t)(end-text):strlen(text);
        if(used+n+2>=sizeof(line))break;
        if(used)line[used]=' ';memcpy(line+used+(used?1:0),text,n);line[used+(used?1:0)+n]=0;
        if(used && psmtp_font_width(line,size)>width){line[used]=0;ui_text(x,y+row*(size+10),size,width,line,color);++row;used=0;continue;}
        used+=(used?1:0)+n;text+=n;
    }
    if(used && row<maximum){line[used]=0;ui_text(x,y+row*(size+10),size,width,line,color);}
}
static void mode_arrow(int x,int y,int right,uint32_t c){
    for(int i=0;i<17;++i){int h=right?17-i:i+1;ui_rect(x+i,y+17-h,1,h*2,c);}
}
static void install_mode_overlay(void){
    ui_shadow(370,240,1180,558,12);ui_box(370,240,1180,558,12,PANEL,LINE,2);ui_text(410,273,33,1100,"Installation Mode",WHITE);
    for(int i=0;i<2;++i){int y=350+i*136,selected=panel->index==(size_t)i;uint32_t c=selected?RGB(30,26,30):WHITE;
        ui_box(401,y-8,1118,118,8,selected?RGB(218,213,219):RGB(31,28,31),selected?WHITE:LINE,2);ui_ring(434,y+30,11,2,c,255);if(panel->install_mode==i)ui_box(428,y+24,12,12,6,c,0,0);
        ui_text(474,y+8,28,1010,i?"Direct Installation from Phone":"Fast Installation (Default)",c);
        ui_text(474,y+55,23,1010,i?"Install without an extra PKG copy on internal storage.":"Copy the PKG to internal storage, then install it.",c);
    }
    ui_text(411,668,23,1090,"Keep the app open and the source connected during installation.",MUTED);
    control_hint(411,735,24,1090,"Cross Apply    Circle Cancel",WHITE);
}
static void settings_page(const char *status){
    int enabled=panel->speed_boost;size_t selected=panel->index;(void)status;
    ui_art(UI_SETTINGS,80,160,42,42,WHITE);ui_text(146,154,38,1550,"Settings",WHITE);
    for(int i=0;i<3;++i){int y=268+i*104,chosen=selected==(size_t)i;
        ui_box(80,y,1050,90,13,chosen?RGB(49,44,48):RGB(31,28,31),chosen?WHITE:LINE,chosen?4:2);
        ui_text(111,y+26,31,650,i==0?"Speed Boost":i==1?"Installation Mode":"Restore Defaults",chosen?WHITE:MUTED);
        if(i==0){
            ui_box(883,y+24,42,42,6,enabled?WHITE:RGB(39,35,38),enabled?WHITE:MUTED,2);
            if(enabled){uint32_t ink=RGB(39,35,38);ui_line(892,y+45,901,y+54,3,ink,255);ui_line(901,y+54,916,y+36,3,ink,255);}
            ui_text(955,y+27,29,130,enabled?"On":"Off",WHITE);
        }
        if(i==1){mode_arrow(799,y+29,0,WHITE);mode_arrow(1070,y+29,1,WHITE);
            ui_center(825,y+30,27,236,panel->install_mode?"From Phone":"Fast (Default)",WHITE);}
    }
    ui_rect(1190,266,1,660,LINE);ui_box(1212,266,648,660,17,RGB(23,21,23),LINE,2);
    ui_text(1242,294,31,580,selected==2?"Restore Defaults":selected==1?"Installation Mode":"Speed Boost",WHITE);
    if(selected==2){settings_copy(1242,362,28,580,3,"Reset your app settings to their original values.",WHITE);
        settings_copy(1242,544,27,580,3,"Changes apply now and are saved. Your files and folders are kept.",MUTED);}
    else if(selected==1){settings_copy(1242,362,28,580,4,"Choose the method for installing packages. Fast mode is recommended for most setups and is the default.",WHITE);
        settings_copy(1242,543,27,580,5,panel->install_mode?"Direct mode reads the PKG from your phone. Keep networking enabled and your phone connected.":"Fast mode copies phone PKGs to internal storage first, then installs them. Internal and USB sources stay the same.",MUTED);}
    else {settings_copy(1242,362,28,580,4,"Faster transfers from compatible phones. Also applies to phone PKGs in both installation modes.",WHITE);
        settings_copy(1242,544,27,580,4,"If a transfer fails, turn this off. Reconnect your phone, then retry.",MUTED);}
    ui_text(84,640,24,1040,panel->preferences_unsaved?"Changes apply to this session.":"Your choices are saved for the next launch.",MUTED);
    if(panel->preferences_unsaved)ui_text(84,682,24,1040,"Changes could not be saved. Check storage access.",WHITE);
    controller(78,979,0,selected==2?"Restore":selected==1?"Choose":"Toggle");controller(282,979,1,"Back");
    if(selected==1)controller(456,979,UI_BTN_DPAD,"Change mode");
}
static void loading_overlay(void) {
    char count[96];ui_shadow(612,398,712,286,13);ui_box(612,398,712,286,13,RGB(29,27,28),RGB(56,51,56),2);ui_text(650,432,29,630,"Reading folder...",WHITE);snprintf(count,sizeof(count),"%" PRIu64 " of %" PRIu64 " items",panel->done,panel->total);ui_text(650,493,26,630,count,MUTED);
    ui_box(650,549,630,8,4,RGB(57,53,61),0,0);if(panel->total) ui_box(650,549,(int)(630*percent(panel->done,panel->total)/100),8,4,RGB(202,198,208),0,0);control_hint(650,607,24,630,"Circle  Cancel",WHITE);
    ui_rect(0,951,1920,129,RGB(21,18,19));controller(78,986,1,"Cancel");
}
#ifndef PSMTP_PRODUCTION
static void logs_view(const diag_log *log,size_t scroll) {
    uint64_t end=diag_log_end(log),start,i;toolbar();ui_text(405,221,30,1040,"Diagnostics",WHITE);if(scroll>end-log->first)scroll=(size_t)(end-log->first);end-=scroll;start=end>24?end-24:0;if(start<log->first)start=log->first;
    ui_box(404,278,1040,638,3,RGB(25,23,25),LINE,1);for(i=start;i<end;++i) ui_text(422,289+(int)(i-start)*25,19,1006,diag_log_line(log,i),RGB(194,186,195));
    ui_text(1529,223,29,345,"Saved diagnostics",WHITE);text_lines(1529,293,24,345,5,"/data/psmtp/psmtp-scan.log",MUTED);control_hint(1529,450,24,345,"D-pad Scroll",WHITE);control_hint(1529,500,24,345,"L1 / R1 Pages",WHITE);control_hint(1529,550,24,345,"Circle Back to files",WHITE);footer(0,"Circle / Options  Back to files");
}
#endif
/* Keep the browser underneath a busy overlay intact. Only the overlay and
 * footer rectangles need restoring/redrawing on subsequent frames. */
#define BUSY_X 588u
#define BUSY_Y 374u
#define BUSY_W 760u
#define BUSY_H 336u
#define BUSY_FOOT_Y 951u
#define BUSY_FOOT_H (PSMTP_SCREEN_HEIGHT-BUSY_FOOT_Y)
#define BUSY_PIXELS (BUSY_W*BUSY_H+PSMTP_SCREEN_WIDTH*BUSY_FOOT_H)
typedef struct {unsigned x,y,w,h;} render_rect;
static const render_rect dynamic_rects[]={{640,512,678,96},{640,606,490,46}};
#define DYNAMIC_PIXELS (678u*96u+490u*46u)
static struct {uint32_t *pixels,*dynamic,*frame;uint64_t key,chrome_key;int valid,chrome_valid,last_hit;} busy_cache;
static uint64_t scene_generation;
static unsigned dirty_rect_count;
static void new_scene(void){if(!++scene_generation)++scene_generation;dirty_rect_count=0;}
static uint64_t busy_hash(uint64_t h,const void *v,size_t n){const unsigned char *p=v;while(n--){h^=*p++;h*=UINT64_C(1099511628211);}return h;}
static uint64_t busy_text(uint64_t h,const char *s){return busy_hash(h,s?s:"",s?strlen(s)+1:1);}
static uint64_t busy_key(const char *status){
    uint64_t h=UINT64_C(14695981039346656037);size_t count=item_count(),selected=item_selected(),limit=panel->list_view?16:12,first=(selected/limit)*limit,i;
    int fields[]={panel->view,panel->copy_mode,panel->transfer_active,panel->list_view,panel->drawer_focus,panel->drawer_settings,panel->toolbar_focus,panel->sort_mode,panel->can_select,panel->can_paste,panel->can_create,panel->rename_allowed,panel->install_mode,show_names,panel->metadata_revision_known};
    h=busy_hash(h,&panel->busy_epoch,sizeof(panel->busy_epoch));h=busy_hash(h,fields,sizeof(fields));h=busy_hash(h,&count,sizeof(count));h=busy_hash(h,&selected,sizeof(selected));
    h=busy_text(h,status);h=busy_text(h,panel->clock_text);h=busy_text(h,panel->clipboard_label);h=busy_text(h,panel->display_version);h=busy_text(h,panel->path);
    if(!panel->clock_text){time_t minute=time(NULL)/60;h=busy_hash(h,&minute,sizeof(minute));}
    if(devices){h=busy_hash(h,&devices->count,sizeof(devices->count));h=busy_hash(h,&devices->active,sizeof(devices->active));h=busy_hash(h,&devices->selected,sizeof(devices->selected));
        for(i=0;i<devices->count;++i){h=busy_text(h,devices->entries[i].label);h=busy_hash(h,&devices->entries[i].kind,sizeof(devices->entries[i].kind));}}
    if(panel->metadata_revision_known){h=busy_hash(h,&panel->metadata_revision,sizeof(panel->metadata_revision));return h;}
    for(i=first;i<count && i<first+limit;++i){psmtp_ui_item item={0};const psmtp_metadata *m=metadata_at(i);
        if(get_item(i,&item)){int shape[]={item.folder,item.unknown,item.items_known,item.thumbnail};h=busy_text(h,item.name);h=busy_text(h,item.modified);h=busy_hash(h,&item.bytes,sizeof(item.bytes));h=busy_hash(h,&item.items,sizeof(item.items));h=busy_hash(h,shape,sizeof(shape));}
        if(m)h=busy_hash(h,&m->revision,sizeof(m->revision));}
    return h;
}
static void busy_rectangle(uint32_t *frame,int save){
    size_t y;uint32_t *buffer=busy_cache.pixels;
    for(y=0;y<BUSY_H;++y){uint32_t *row=frame+(BUSY_Y+y)*PSMTP_SCREEN_WIDTH+BUSY_X;if(save)memcpy(buffer,row,BUSY_W*4);else memcpy(row,buffer,BUSY_W*4);buffer+=BUSY_W;}
    if(save)memcpy(buffer,frame+BUSY_FOOT_Y*PSMTP_SCREEN_WIDTH,BUSY_FOOT_H*PSMTP_SCREEN_WIDTH*4);
    else memcpy(frame+BUSY_FOOT_Y*PSMTP_SCREEN_WIDTH,buffer,BUSY_FOOT_H*PSMTP_SCREEN_WIDTH*4);
}
int psmtp_console_render_cached(void){return busy_cache.last_hit;}
void psmtp_console_render_invalidate(void){busy_cache.valid=busy_cache.chrome_valid=busy_cache.last_hit=0;}
void psmtp_console_render_close(void){free(busy_cache.pixels);free(busy_cache.dynamic);memset(&busy_cache,0,sizeof(busy_cache));new_scene();}
static void dynamic_rectangle(uint32_t *frame,int save){
    unsigned i,y;uint32_t *buffer=busy_cache.dynamic;
    for(i=0;i<sizeof(dynamic_rects)/sizeof(dynamic_rects[0]);++i){const render_rect *r=&dynamic_rects[i];
        for(y=0;y<r->h;++y){uint32_t *row=frame+(r->y+y)*PSMTP_SCREEN_WIDTH+r->x;
            if(save)memcpy(buffer,row,r->w*4);else memcpy(row,buffer,r->w*4);buffer+=r->w;}}
}
static void busy_transfer(uint32_t *frame,int reused){
    uint64_t key=busy_text(busy_text(UINT64_C(14695981039346656037),transfer_phase_text()),panel->name);
    int display_items=panel->display_items!=NULL;
    key=busy_hash(key,&panel->phase,sizeof(panel->phase));
    key=busy_hash(key,&panel->view,sizeof(panel->view));
    key=busy_hash(key,&display_items,sizeof(display_items));
    if(!busy_cache.dynamic)busy_cache.dynamic=malloc(DYNAMIC_PIXELS*4);
    if(!busy_cache.dynamic){busy_rectangle(frame,0);transfer_overlay();new_scene();return;}
    if(!busy_cache.chrome_valid || busy_cache.chrome_key!=key){
        if(reused){busy_rectangle(frame,0);new_scene();}
        transfer_static();dynamic_rectangle(frame,1);busy_cache.chrome_key=key;busy_cache.chrome_valid=1;
    }else dynamic_rectangle(frame,0);
    transfer_dynamic();dirty_rect_count=transfer_verifying() && !panel->animation_ms?0:(unsigned)(sizeof(dynamic_rects)/sizeof(dynamic_rects[0]));
}
size_t psmtp_console_render_upload(uint32_t *target,const uint32_t *source,uint64_t *generation){
    size_t bytes=0;unsigned i,y;
    if(target==source){if(generation)*generation=scene_generation;return 0;}
    if(!scene_generation || !generation || *generation!=scene_generation){memcpy(target,source,PSMTP_SCREEN_WIDTH*PSMTP_SCREEN_HEIGHT*4u);if(generation)*generation=scene_generation;return PSMTP_SCREEN_WIDTH*PSMTP_SCREEN_HEIGHT*4u;}
    for(i=0;i<dirty_rect_count;++i){const render_rect *r=&dynamic_rects[i];for(y=0;y<r->h;++y){size_t at=(r->y+y)*PSMTP_SCREEN_WIDTH+r->x;memcpy(target+at,source+at,r->w*4);}bytes+=(size_t)r->w*r->h*4;}
    return bytes;
}
void psmtp_console_render(uint32_t *frame,const diag_log *log,size_t scroll,const char *status,int names,const char *save,const psmtp_console_selection *s,const mtp_navigator *browser) {
    int busy=0,overlay=0;uint64_t key=0;
    (void)save;show_names=names;panel=s;phone=browser?browser:s?s->browser:NULL;disk=s?s->local:NULL;devices=s?s->locations:NULL;ui_draw_begin(frame);
    busy_cache.last_hit=0;dirty_rect_count=0;
    if(s && s->busy_epoch && (s->view==PSMTP_VIEW_HOME || s->view==PSMTP_VIEW_PHONE || s->view==PSMTP_VIEW_LOCAL || s->view==PSMTP_VIEW_TRANSFERS || s->view==PSMTP_VIEW_INSTALL_PROGRESS)){
        overlay=s->copy_mode==2;busy=overlay || s->transfer_active;
    }
    if(busy){key=busy_key(status);if(busy_cache.valid && busy_cache.frame==frame && busy_cache.key==key){
        busy_cache.last_hit=1;if(overlay)busy_transfer(frame,1);return;}}
    busy_cache.valid=busy_cache.chrome_valid=0;new_scene();
    if(psmtp_font_init()) {ui_rect(0,0,1920,1080,RGB(120,20,20));return;}if(s && (s->view==PSMTP_VIEW_SETTINGS || s->settings_context))ui_settings_backdrop();else ui_backdrop();header();
    if(s && s->view==PSMTP_VIEW_UPDATE){char progress[96];
        ui_text(150,335,42,1620,s->notice_title?s->notice_title:"Checking for updates",WHITE);
        ui_text(150,415,27,1620,s->notice_text?s->notice_text:"The app will open normally if no update is available.",MUTED);
        if(s->total){snprintf(progress,sizeof(progress),"%u%%",percent(s->done,s->total));ui_text(150,495,28,1620,progress,WHITE);
            ui_box(150,552,1620,10,5,LINE,0,0);ui_box(150,552,(int)(1620*percent(s->done,s->total)/100),10,5,WHITE,0,0);}
        controller(78,979,1,"Cancel");return;
    }
    if(s && (s->view==PSMTP_VIEW_SETTINGS || (s->settings_context && (s->view==PSMTP_VIEW_INSTALL_MODE || s->view==PSMTP_VIEW_NOTICE)))){
        psmtp_console_selection background=*s;background.index=s->view==PSMTP_VIEW_SETTINGS?s->index:(size_t)s->settings_index;
        panel=&background;settings_page(status);panel=s;
        if(s->view!=PSMTP_VIEW_SETTINGS){ui_alpha(0,120,1920,831,RGB(0,0,0),150);if(s->view==PSMTP_VIEW_INSTALL_MODE)install_mode_overlay();else notice_overlay();}
        return;
    }
#ifdef PSMTP_PRODUCTION
    (void)log;(void)scroll;
#endif
    sidebar();
#ifndef PSMTP_PRODUCTION
    if(s && s->view==PSMTP_VIEW_LOGS) logs_view(log,scroll);
    else
#endif
    {explorer(status);
        if(busy){if(!busy_cache.pixels)busy_cache.pixels=malloc(BUSY_PIXELS*4);
            if(busy_cache.pixels){busy_rectangle(frame,1);busy_cache.frame=frame;busy_cache.key=key;busy_cache.valid=1;}}
        if(s && s->view==PSMTP_VIEW_CONTROLS) {static const char *const help[]={"D-pad: select tiles. Up from first row opens view / sort.","X: open. Circle: back. Triangle / Options: context menu.","Copy and Delete are available in the Options menu.","L3: Grid / List. L1 / R1: previous / next page.","L2 / R2: previous / next phone storage. Touchpad: refresh.","Move uses the clipboard. Choose Paste at the destination.","Options: create folders, manage files and exit the app."};int i;ui_box(500,252,920,560,12,PANEL,LINE,2);ui_text(540,285,32,840,"Controller guide",WHITE);for(i=0;i<7;++i) control_hint(540,366+i*54,23,840,help[i],WHITE);}
        else if(s && s->view==PSMTP_VIEW_INSTALL_MODE) install_mode_overlay();else if(s && s->view==PSMTP_VIEW_INSTALL_CONFIRM) install_confirmation();else if(s && s->view==PSMTP_VIEW_NOTICE) notice_overlay();else if(s && s->view==PSMTP_VIEW_RENAME) rename_overlay();else if(s && s->view==PSMTP_VIEW_MENU) menu_overlay();else if(s && s->view==PSMTP_VIEW_SORT) sort_overlay();else if(s && s->view==PSMTP_VIEW_INFO) properties_overlay();else if(s && s->view==PSMTP_VIEW_OPERATION) review_overlay(1);else if(s && s->view==PSMTP_VIEW_LOADING) loading_overlay();else if(s && s->copy_mode==1) review_overlay(0);else if(s && s->copy_mode==2){if(busy && busy_cache.valid)busy_transfer(frame,0);else transfer_overlay();}
        else if(s && s->view==PSMTP_VIEW_PICK) {ui_shadow(612,398,712,286,13);ui_box(612,398,712,286,13,PANEL,LINE,2);ui_text(650,435,30,630,"Phone ready to browse",WHITE);ui_text(650,510,25,630,"Unlock your phone and choose File Transfer.",MUTED);control_hint(650,612,26,630,"Cross  Open     Circle  Cancel",WHITE);}}
}
