#include "progress.h"
#include <boost/json.hpp>
#include <curl/curl.h>
#include <cmath>
#include <cstring>
#include <new>
#ifndef STOCK_PROGRESS_URL
#define STOCK_PROGRESS_URL "http://127.0.0.1:7125/printer/objects/query?print_stats&virtual_sdcard&motion_report&gcode_move&webhooks"
#endif
struct ProgressClient { CURL *curl; CURLM *multi; bool pending; size_t length; char data[32768]; };
static uint64_t integer(const boost::json::value& v) {
    if(v.is_uint64())return v.as_uint64();
    if(v.is_int64()&&v.as_int64()>=0)return static_cast<uint64_t>(v.as_int64());
    throw std::invalid_argument("Expected nonnegative integer");
}
int progress_parse(const char *data,size_t length,PrintProgress *out) {
    *out={};out->state=JOB_OFFLINE;
    try {
        auto root=boost::json::parse(boost::json::string_view(data,length));
        const auto& status=root.at("result").at("status");
        if(status.at("webhooks").at("state").as_string()!="ready")return -1;
        const auto& stats=status.at("print_stats");
        const auto& state=stats.at("state").as_string();
        PrintProgress p={};
        if(state=="printing")p.state=JOB_PRINTING;
        else if(state=="paused")p.state=JOB_PAUSED;
        else if(state=="complete")p.state=JOB_COMPLETE;
        else if(state=="cancelled")p.state=JOB_CANCELLED;
        else if(state=="error")p.state=JOB_ERROR;
        else if(state=="standby")p.state=JOB_IDLE;
        else return -1;
        const auto& name=stats.at("filename").as_string();
        if(name.size()>=sizeof(p.filename)||std::memchr(name.data(),0,name.size()))return -1;
        std::memcpy(p.filename,name.data(),name.size());
        const auto& sd=status.at("virtual_sdcard");
        p.file_position=integer(sd.at("file_position"));p.file_size=integer(sd.at("file_size"));
        if(p.file_position>p.file_size||(p.state!=JOB_IDLE&&!name.size()))return -1;
        const auto& motion=status.at("motion_report").at("live_position").as_array();
        const auto& origin=status.at("gcode_move").at("homing_origin").as_array();
        if(motion.size()>=3&&origin.size()>=3) {
            p.has_nozzle=1;
            for(int k=0;k<3;k++){
                double x=boost::json::value_to<double>(motion[k])-boost::json::value_to<double>(origin[k]);
                if(!std::isfinite(x)||std::fabs(x)>10000)p.has_nozzle=0;
                p.nozzle[k]=static_cast<float>(x);
            }
        }
        *out=p;return 0;
    } catch(const std::exception&) {return -1;}
}
static size_t receive(char *data,size_t size,size_t count,void *context) {
    auto *c=static_cast<ProgressClient*>(context);
    if(size&&count>(sizeof(c->data)-c->length)/size)return 0;
    size_t n=size*count;std::memcpy(c->data+c->length,data,n);c->length+=n;return n;
}
ProgressClient *progress_create(void) {
    if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK)return nullptr;
    auto *c=new(std::nothrow) ProgressClient{};
    if(!c){curl_global_cleanup();return nullptr;}
    c->curl=curl_easy_init();
    c->multi=curl_multi_init();
    if(!c->curl||!c->multi){progress_destroy(c);return nullptr;}
    curl_easy_setopt(c->curl,CURLOPT_URL,STOCK_PROGRESS_URL);
    curl_easy_setopt(c->curl,CURLOPT_PROXY,"");
    curl_easy_setopt(c->curl,CURLOPT_NOSIGNAL,1L);
    curl_easy_setopt(c->curl,CURLOPT_CONNECTTIMEOUT_MS,150L);
    curl_easy_setopt(c->curl,CURLOPT_TIMEOUT_MS,750L);
    curl_easy_setopt(c->curl,CURLOPT_WRITEFUNCTION,receive);
    curl_easy_setopt(c->curl,CURLOPT_WRITEDATA,c);
    return c;
}
void progress_cancel(ProgressClient *c) {
    if(c&&c->pending){curl_multi_remove_handle(c->multi,c->curl);c->pending=false;}
}
int progress_poll(ProgressClient *c,PrintProgress *out) {
    *out={};out->state=JOB_OFFLINE;if(!c)return -1;
    if(!c->pending){
        c->length=0;
        if(curl_multi_add_handle(c->multi,c->curl)!=CURLM_OK)return -1;
        c->pending=true;
    }
    int running=0;
    if(curl_multi_perform(c->multi,&running)!=CURLM_OK){progress_cancel(c);return -1;}
    int remaining=0;
    while(auto *message=curl_multi_info_read(c->multi,&remaining)){
        if(message->msg!=CURLMSG_DONE)continue;
        CURLcode rc=message->data.result;long status=0;
        curl_easy_getinfo(c->curl,CURLINFO_RESPONSE_CODE,&status);
        progress_cancel(c);
        return rc==CURLE_OK&&status==200?progress_parse(c->data,c->length,out):-1;
    }
    return 1;
}
void progress_destroy(ProgressClient *c) {
    if(c){progress_cancel(c);if(c->multi)curl_multi_cleanup(c->multi);if(c->curl)curl_easy_cleanup(c->curl);delete c;curl_global_cleanup();}
}
