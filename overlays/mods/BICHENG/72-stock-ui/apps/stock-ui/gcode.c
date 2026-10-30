#include "gcode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <sys/stat.h>
#define MAX_VERTICES 1000000
#define MAX_LAYERS 20000
static const uint8_t colors[4][4]={{180,190,205,255},{246,196,65,255},{65,177,240,255},{255,86,63,255}};
void gcode_free(Model *m) {free(m->vertices);free(m->layer_end);free(m->layer_z);memset(m,0,sizeof(*m));}
static int fail(Model *m,const char *text) {snprintf(m->error,sizeof(m->error),"%s",text);return -1;}
static int segment(Model *m,const double *a,const double *b,unsigned tool,int new_layer) {
    if(m->count+2>MAX_VERTICES)return fail(m,"文件路径过多，无法预览");
    if(!m->layers||new_layer) {
        if(m->layers==MAX_LAYERS)return fail(m,"文件层数过多");
        m->layer_z[m->layers]=(float)b[2];
        m->layer_end[m->layers++]=m->count;
    }
    if(m->count+2>m->capacity) {
        size_t cap=m->capacity?m->capacity*2:16384;
        if(cap>MAX_VERTICES)cap=MAX_VERTICES;
        void *p=realloc(m->vertices,cap*sizeof(Vertex));
        if(!p)return fail(m,"内存不足");
        m->vertices=p;m->capacity=cap;
    }
    for(int j=0;j<2;j++) {
        const double *p=j?b:a;
        Vertex *v=&m->vertices[m->count++];
        v->x=p[0];v->y=p[1];v->z=p[2];memcpy(v->color,colors[tool],4);
        for(int k=0;k<3;k++) {if(p[k]<m->min[k])m->min[k]=p[k];if(p[k]>m->max[k])m->max[k]=p[k];}
    }
    m->layer_end[m->layers-1]=m->count;
    return 0;
}
int gcode_load(Model *m,const char *path,const atomic_uint *generation,unsigned expected) {
    gcode_free(m);
    struct stat st;
    if(stat(path,&st)||!S_ISREG(st.st_mode)||st.st_size>512*1024*1024)return fail(m,"无法读取 G-code 文件");
    FILE *f=fopen(path,"r");if(!f)return fail(m,"无法打开 G-code 文件");
    m->layer_end=calloc(MAX_LAYERS,sizeof(size_t));m->layer_z=calloc(MAX_LAYERS,sizeof(float));
    if(!m->layer_end||!m->layer_z){fclose(f);return fail(m,"内存不足");}
    for(int k=0;k<3;k++){m->min[k]=INFINITY;m->max[k]=-INFINITY;}
    double pos[3]={0},offset[3]={0},extrusion=0,unit=1,layer_z=-INFINITY;
    int relative=0,relative_e=0,plane=17,arc_absolute=0,explicit_layers=0,pending_layer=0;
    unsigned tool=0;int result=0;char line[16384];unsigned lines=0;
    while(fgets(line,sizeof(line),f)) {
        if((++lines&1023)==0 && generation && atomic_load(generation)!=expected){result=fail(m,"已取消");break;}
        if(!strchr(line,'\n')&&!feof(f)){
            if(line[0]!=';'){result=fail(m,"G-code 行过长");break;}
            int c;while((c=fgetc(f))!=EOF&&c!='\n'){}
        }
        char *p=line;while(isspace((unsigned char)*p))p++;
        if(!strncmp(p,";LAYER_CHANGE",13)||!strncmp(p,";LAYER:",7)){
            if(!explicit_layers){
                m->count=0;m->layers=0;
                for(int k=0;k<3;k++){m->min[k]=INFINITY;m->max[k]=-INFINITY;}
            }
            explicit_layers=1;pending_layer=1;continue;
        }
        if(*p==';'||!*p)continue;
        if(*p=='N'){p++;strtol(p,&p,10);while(isspace((unsigned char)*p))p++;}
        if(*p!='G'&&*p!='M'&&*p!='T')continue;
        int tool_command=*p=='T';
        double val[26]={0};unsigned present=0;int motion=-1,setpos=0;
        while(*p&&*p!=';'&&*p!='*') {
            if(*p=='('){while(*p&&*p!=')')p++;if(*p)p++;continue;}
            if(!isalpha((unsigned char)*p)){p++;continue;}
            int key=toupper((unsigned char)*p++)-'A';
            while(isspace((unsigned char)*p))p++;
            char *end,*limit=p;
            if(*limit=='+'||*limit=='-')limit++;
            while(isdigit((unsigned char)*limit)||*limit=='.')limit++;
            if(limit==p){
                if(motion>=0&&strchr("XYZEIJR",'A'+key))result=fail(m,"无效的 G-code 数值");
                break;
            }
            char saved=*limit;*limit=0;double v=strtod(p,&end);*limit=saved;
            if(end!=limit){result=fail(m,"无效的 G-code 数值");break;}p=limit;
            if(!isfinite(v)){result=fail(m,"无效的 G-code 坐标");break;}
            if(key=='G'-'A'){
                if(v==0||v==1||v==2||v==3)motion=(int)v;
                else if(v==90)relative=0;else if(v==91)relative=1;
                else if(v==90.1)arc_absolute=1;else if(v==91.1)arc_absolute=0;
                else if(v==92)setpos=1;else if(v==20)unit=25.4;else if(v==21)unit=1;
                else if(v==17||v==18||v==19)plane=(int)v;
            } else if(key=='M'-'A') {if(v==82)relative_e=0;else if(v==83)relative_e=1;}
            else if(key=='T'-'A'&&tool_command) {if(v>=0&&v<4&&v==(int)v)tool=(unsigned)v;else {result=fail(m,"不支持的打印头编号");break;}}
            else {val[key]=v;present|=1u<<key;}
        }
        if(result)break;
        if(setpos) {
            for(int k=0;k<3;k++)if(present&(1u<<('X'+k-'A')))offset[k]=pos[k]-val['X'+k-'A']*unit;
            if(present&(1u<<('E'-'A')))extrusion=val['E'-'A']*unit;
            continue;
        }
        if(motion<0)continue;
        double next[3];memcpy(next,pos,sizeof(pos));
        for(int k=0;k<3;k++)if(present&(1u<<('X'+k-'A')))next[k]=(relative?pos[k]:offset[k])+val['X'+k-'A']*unit;
        double e=extrusion;
        if(present&(1u<<('E'-'A')))e=(relative_e?extrusion:0)+val['E'-'A']*unit;
        int draw=e>extrusion+1e-7;
        for(int k=0;k<3;k++)if(fabs(next[k])>10000){result=fail(m,"G-code 坐标超出预览范围");break;}
        if(result)break;
        int new_layer=explicit_layers?pending_layer:(next[2]>layer_z+0.01);
        size_t before=m->count;
        if(draw&&(motion==2||motion==3)) {
            if(plane!=17||arc_absolute){result=fail(m,"不支持此圆弧坐标模式");break;}
            double cx=pos[0]+val['I'-'A']*unit,cy=pos[1]+val['J'-'A']*unit;
            if(present&(1u<<('R'-'A'))) {
                double r=val['R'-'A']*unit,dx=next[0]-pos[0],dy=next[1]-pos[1],d=hypot(dx,dy);
                if(d<1e-6||d>2*fabs(r)){result=fail(m,"无效的圆弧半径");break;}
                double h=sqrt(fmax(0,r*r-d*d/4))*(motion==2?-1:1)*(r<0?-1:1);
                cx=(pos[0]+next[0])/2-dy/d*h;cy=(pos[1]+next[1])/2+dx/d*h;
            }
            double r=hypot(pos[0]-cx,pos[1]-cy),start=atan2(pos[1]-cy,pos[0]-cx),sweep=atan2(next[1]-cy,next[0]-cx)-start;
            if(motion==2){if(sweep>=-1e-9)sweep-=2*M_PI;}else if(sweep<=1e-9)sweep+=2*M_PI;
            if(r<1e-6||fabs(hypot(next[0]-cx,next[1]-cy)-r)>0.1){result=fail(m,"无效的圆弧");break;}
            int n=(int)ceil(fabs(sweep)*sqrt(r/0.08));if(n<2)n=2;if(n>8192){result=fail(m,"圆弧过大");break;}
            double a[3];memcpy(a,pos,sizeof(a));
            for(int i=1;i<=n;i++){
                double t=(double)i/n,b[3]={cx+r*cos(start+sweep*t),cy+r*sin(start+sweep*t),pos[2]+(next[2]-pos[2])*t};
                if(i==n)memcpy(b,next,sizeof(b));
                if(segment(m,a,b,tool,new_layer&&i==1)){result=-1;break;}memcpy(a,b,sizeof(a));
            }
        } else if(draw&&hypot(next[0]-pos[0],next[1]-pos[1])>1e-6)result=segment(m,pos,next,tool,new_layer);
        if(result)break;
        if(m->count>before){pending_layer=0;layer_z=next[2];}
        memcpy(pos,next,sizeof(pos));extrusion=e;
    }
    if(ferror(f))result=fail(m,"读取文件失败");
    fclose(f);
    if(!result&&!m->count)result=fail(m,"文件没有可显示的挤出路径");
    return result;
}
