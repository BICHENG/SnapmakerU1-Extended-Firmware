#define _GNU_SOURCE
#include "gcode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <string.h>
static Model load(const char *data,int expect){
    char path[]="/tmp/stock-gcode-XXXXXX";int fd=mkstemp(path);assert(fd>=0);
    FILE *f=fdopen(fd,"w");fputs(data,f);fclose(f);
    Model m={0};assert(gcode_load(&m,path,NULL,0)==expect);unlink(path);return m;
}
int main(int argc,char **argv){
    if(argc==2){Model m={0};int rc=gcode_load(&m,argv[1],NULL,0);printf("result=%d vertices=%zu layers=%u bounds=%.2f,%.2f,%.2f..%.2f,%.2f,%.2f error=%s\n",rc,m.count,m.layers,m.min[0],m.min[1],m.min[2],m.max[0],m.max[1],m.max[2],m.error);gcode_free(&m);return rc!=0;}
    Model m=load("G90\nM83\nG1 X10 Y10 Z0.2\nG1 X20 E1\nG1 E-1\nG1 X30\nG1 E1\nT3\nG1 Y20 E1\n",0);
    assert(m.count==4&&m.layers==1&&m.vertices[0].x==10&&m.vertices[2].x==30&&m.vertices[2].color[0]==255);gcode_free(&m);
    m=load("M82\nG1 X10 E1\nG92 X0 E0\nG1 X10 E1\nG91\nG1 Y10 E2\n",0);
    assert(m.count==6&&m.max[0]==20&&m.max[1]==10);gcode_free(&m);
    m=load("M83\nG1 X20 E1\n;LAYER_CHANGE\nG1 Z0.2\nG1 E1\nG1 Y10 E1\n;LAYER_CHANGE\nG1 Z0.4\nG1 E1\nG1 X30 E1\n",0);
    assert(m.count==4&&m.layers==2&&fabs(m.layer_z[1]-.4)<.001);gcode_free(&m);
    m=load("M83\nG1 X10 Y0\nG3 X0 Y10 I-10 J0 E1\n",0);
    assert(m.count>10&&fabs(m.max[0]-10)<.001&&fabs(m.max[1]-10)<.001);gcode_free(&m);
    m=load("G20\nM83\nG1 X1 E1\n",0);assert(fabs(m.max[0]-25.4)<.001);gcode_free(&m);
    m=load("M83\nT0\nM104 S200 T3\nG1 X1 E1\n",0);
    assert(m.vertices[0].color[0]==180);gcode_free(&m);
    m=load("M83\nG18\nG2 X10 I5 E1\n",-1);gcode_free(&m);
    m=load("M83\nG1 X10001 E1\n",-1);gcode_free(&m);
    m=load("G90\nM83\nG1X10Y5E1\n",0);
    assert(m.count==2&&m.max[0]==10&&m.max[1]==5);gcode_free(&m);
    m=load("M83\nG1 X 10 Y 5 E 1\n",0);assert(m.count==2);gcode_free(&m);
    m=load("M83\nG1 Xnan E1\n",-1);gcode_free(&m);
    const char *tracked=";模型\r\nM83\r\n;LAYER_CHANGE\r\nG1 Z.2\r\nG1 X10 E1\r\nG1 Y10 E1\r\n;LAYER_CHANGE\r\nG1 Z.4\r\nG1 X20 E1\r\n";
    m=load(tracked,0);
    size_t end;unsigned layer;
    const char *line=strstr(tracked,"G1 X10 E1");
    uint64_t first_end=(uint64_t)(strchr(line,'\n')-tracked+1);
    assert(m.line_end[0]==first_end);
    gcode_progress(&m,first_end-1,&end,&layer);assert(end==0&&layer==0);
    gcode_progress(&m,first_end,&end,&layer);assert(end==2&&layer==0);
    gcode_progress(&m,m.line_end[1],&end,&layer);assert(end==4&&layer==0);
    gcode_progress(&m,m.line_end[2],&end,&layer);assert(end==6&&layer==1);
    gcode_progress(&m,UINT64_MAX,&end,&layer);assert(end==6&&layer==1);
    gcode_progress(&m,0,&end,&layer);assert(end==0&&layer==0);
    gcode_free(&m);
    m=load("M83\nG1 X10\nG3 X0 Y10 I-10 J0 E1\n",0);
    for(size_t i=0;i<m.count/2;i++)assert(m.line_end[i]==m.line_end[0]);
    gcode_progress(&m,m.line_end[0]-1,&end,&layer);assert(end==0);
    gcode_progress(&m,m.line_end[0],&end,&layer);assert(end==m.count);
    gcode_free(&m);
    puts("G-code behavior checks passed");
}
