/* Only correct the exact, observed R1 driver metadata mismatch. No sysfs writes. */
#ifndef POCKETOS_TOUCH_H
#define POCKETOS_TOUCH_H
static int touch_calibrate(const char *root,const char *name,struct input_absinfo *x,struct input_absinfo *y,unsigned width,unsigned height){
    if(strcmp(name,"hyn_ts")||width!=480||height!=800||x->minimum||y->minimum||x->maximum!=720||y->maximum!=1280)return 0;
    char p[HW_PATH];int n=snprintf(p,sizeof(p),"%s/sys/module/cst8xx_touch/parameters",root);
    if(n<0||(size_t)n>=sizeof(p))return 0;
    if(hw_number(p,"cst_x_coords_min",0,0)!=0||hw_number(p,"cst_y_coords_min",0,0)!=0||
       hw_number(p,"cst_x_coords_max",480,480)!=480||hw_number(p,"cst_y_coords_max",800,800)!=800||
       hw_number(p,"cst_x_coords_flip",0,0)!=0||hw_number(p,"cst_y_coords_flip",0,0)!=0||
       hw_number(p,"cst_x_y_coords_exchange",0,0)!=0)return 0;
    x->maximum=480;y->maximum=800;return 1;
}
static int touch_canvas(int raw,int min,int max,unsigned physical,unsigned offset,unsigned viewport,unsigned logical){
    if(max<=min||!physical||!viewport||!logical||raw<min||raw>max)return -1;
    int64_t pixel=((int64_t)raw-min)*(physical-1)/((int64_t)max-min);
    if(pixel<offset||pixel>=(int64_t)offset+viewport)return -1;
    return (int)((pixel-offset)*logical/viewport);
}
#endif
