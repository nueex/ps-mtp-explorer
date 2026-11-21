/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#include "psmtp/install_error.h"
#include "psmtp/mtp.h"
#include <stdio.h>
typedef struct {uint32_t code;enum psmtp_install_error_kind kind;const char *name;} error_record;
/* Recorded BGFT result names/hex values: docs/INSTALL_ERRORS_T12_A7_I1_20261030.md.
 * These diagnose native failure. No result authorizes bypass, uninstall/retry. */
static const error_record errors[]={
    {0x80990077u,PSMTP_INSTALL_INTEGRITY,"PLAYGO_SHA_DIGEST"},
    {0x80990065u,PSMTP_INSTALL_INTEGRITY,"MISMATCH_PACKAGE_DIGEST"},
    {0x80990061u,PSMTP_INSTALL_INTEGRITY,"INCONSISTENT_PACKAGE_FILE_SIZE"},
    {0x80990078u,PSMTP_INSTALL_INTEGRITY,"MISMATCHED_PACKAGE_SIZE"},
    {0x8099006bu,PSMTP_INSTALL_INTEGRITY,"UNEXPECTED_EOF"},
    {0x8099003au,PSMTP_INSTALL_READ_ERROR,"FILE_READ_IO"},
    {0x80990074u,PSMTP_INSTALL_READ_ERROR,"PLAYGO_SHA_READ_ERROR"},
    {0x80990039u,PSMTP_INSTALL_SPACE_ERROR,"DEVICE_NOSPC"},
    {0x80990021u,PSMTP_INSTALL_SPACE_ERROR,"PARAM_NOSPC"},
    {0x80990064u,PSMTP_INSTALL_PATCH_ERROR,"MISMATCH_CHUNK_COUNT_BETWEEN_APP_AND_PATCH"},
    {0x80990066u,PSMTP_INSTALL_PATCH_ERROR,"MISSING_APP_PLAYGO_CHUNK_DAT"},
    {0x80990067u,PSMTP_INSTALL_PATCH_ERROR,"MISSING_PATCH_PLAYGO_APP_CHUNK_DAT"},
    {0x80990068u,PSMTP_INSTALL_PATCH_ERROR,"MISSING_PATCH_PLAYGO_PATCH_CHUNK_DAT"},
    {0x8099007au,PSMTP_INSTALL_PATCH_ERROR,"UNSUPPORTED_DELTA_PACKAGE"},
    {0x80990015u,PSMTP_INSTALL_EXISTING_TASK,"TASK_DUPLICATED"},
    {0x80990086u,PSMTP_INSTALL_EXISTING_TASK,"TASK_CONFLICT_80990086"},
    {0x80990088u,PSMTP_INSTALL_ALREADY_INSTALLED,"CONTENT_CONFLICT_80990088"}
};
static const error_record *find_error(uint32_t code){size_t i;for(i=0;i<sizeof(errors)/sizeof(*errors);++i)if(errors[i].code==code)return errors+i;return NULL;}
enum psmtp_install_error_kind psmtp_install_error_kind(uint32_t code){const error_record *r=find_error(code);return r?r->kind:PSMTP_INSTALL_UNKNOWN;}
const char *psmtp_install_error_name(uint32_t code){const error_record *r=find_error(code);return r?r->name:"UNCLASSIFIED_NATIVE_ERROR";}
const char *psmtp_install_step_name(int step){static const char *names[]={"inspect","stage-copy","source-check","app-init","bgft-init","patch-base","register-storage","start","progress-query","progress-result","cleanup","phone-transfer"};return step>=0 && (unsigned)step<sizeof(names)/sizeof(*names)?names[step]:"unknown";}
const char *psmtp_install_failure_title(const psmtp_install_failure *f){
    if(f->base_missing)return "Base game not installed";
    if(!f->native_error && f->app_error==MTP_VERIFY && f->step==PSMTP_INSTALL_STAGE_COPY)return "Copied package verification failed";
    if(!f->native_error && f->step==PSMTP_INSTALL_PHONE_TRANSFER)return "Phone package transfer failed";
    switch(psmtp_install_error_kind(f->native_error)){
    case PSMTP_INSTALL_INTEGRITY:return "Package integrity check failed";
    case PSMTP_INSTALL_READ_ERROR:return "Package read failed";
    case PSMTP_INSTALL_SPACE_ERROR:return "Insufficient installation space";
    case PSMTP_INSTALL_PATCH_ERROR:return "Game update could not be applied";
    case PSMTP_INSTALL_EXISTING_TASK:return "Existing installation task";
    case PSMTP_INSTALL_ALREADY_INSTALLED:return "Installed content conflict";
    default:return "Installation unavailable";
    }
}
void psmtp_install_failure_text(const psmtp_install_failure *f,char *out,size_t cap){
    const char *advice;enum psmtp_install_error_kind kind=psmtp_install_error_kind(f->native_error);
    if(!cap)return;
    if(f->base_missing){snprintf(out,cap,"The update needs its base game (%.9s).\nInstall the matching base game first.\nSource PKG preserved.",f->title_id);return;}
    switch(kind){
    case PSMTP_INSTALL_INTEGRITY:advice="PS4 rejected the package data check.\nCompare original and copied PKG hashes;\ncheck PS4 Notifications for details.";break;
    case PSMTP_INSTALL_READ_ERROR:advice="PS4 could not read the package data.\nCheck the source drive and connection.\nCheck PS4 Notifications for details.";break;
    case PSMTP_INSTALL_SPACE_ERROR:advice="Free space is needed for staging and\ninstalled content. Check the PS4 drive.";break;
    case PSMTP_INSTALL_PATCH_ERROR:advice="Check the installed base game's title ID\nand that this update matches that game.\nAn intact file can still be incompatible.";break;
    case PSMTP_INSTALL_EXISTING_TASK:advice="A task for this content may already exist.\nCheck PS4 Notifications > Downloads.\nNo existing game was removed.";break;
    case PSMTP_INSTALL_ALREADY_INSTALLED:advice="PS4 reported an installed-content conflict.\nCheck the game and update versions.\nNo game was uninstalled or replaced.";break;
    default:advice=f->patch?"Check PS4 Notifications for the exact error.\nConfirm the update matches the base game.":"Check PS4 Notifications for the exact error.";break;
    }
    if(f->native_error)snprintf(out,cap,"System installer error (0x%08x).\n%s\nSource PKG preserved.",f->native_error,advice);
    else if(f->app_error==MTP_VERIFY && f->step==PSMTP_INSTALL_STAGE_COPY)snprintf(out,cap,"The saved PKG failed full copy verification.\nInstallation was not started.\nSource PKG preserved.");
    else if(f->step==PSMTP_INSTALL_PHONE_TRANSFER)snprintf(out,cap,"Could not deliver the PKG to the PS4 installer.\n%s (app error %d).\nSource PKG preserved.",mtp_error_string(f->app_error),f->app_error);
    else snprintf(out,cap,"Could not start installation:\n%s (app error %d).\nSource PKG preserved.",mtp_error_string(f->app_error),f->app_error);
}
