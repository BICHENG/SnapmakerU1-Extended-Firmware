#include "progress.h"
#include <boost/json.hpp>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <string>
int main(){
    auto root=boost::json::parse(R"({"result":{"status":{
      "webhooks":{"state":"ready"},
      "print_stats":{"state":"printing","filename":"sub/\u6a21\u578b.gcode"},
      "virtual_sdcard":{"file_position":128,"file_size":1024}
    }}})");
    auto parse=[&](PrintProgress& p){auto data=boost::json::serialize(root);return progress_parse(data.data(),data.size(),&p);};
    PrintProgress p;
    assert(parse(p)==0&&p.state==JOB_PRINTING&&p.file_position==128);
    assert(!std::strcmp(p.filename,"sub/模型.gcode"));
    auto& status=root.at("result").at("status");
    for(auto state:{"paused","complete","cancelled","error","standby"}){
        status.at("print_stats").at("state")=state;assert(parse(p)==0);
    }
    status.at("print_stats").at("state")="printing";
    status.at("virtual_sdcard").at("file_position")=-1;assert(parse(p)==-1&&p.state==JOB_OFFLINE);
    status.at("virtual_sdcard").at("file_position")=1025;assert(parse(p)==-1);
    status.at("virtual_sdcard").at("file_position")=128;
    status.at("print_stats").at("filename")=std::string(4096,'x');assert(parse(p)==-1);
    status.at("print_stats").at("filename")="sample.gcode";
    status.at("webhooks").at("state")="shutdown";assert(parse(p)==-1);
    assert(progress_parse("{}",2,&p)==-1);
    assert(progress_parse("{",1,&p)==-1);
    assert(progress_parse("null",4,&p)==-1);
    puts("Print progress boundary checks passed");
}
