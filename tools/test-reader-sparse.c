/* Compare dense/sparse renderers across all glyphs, weights and scaled sizes.
 * Build twice with the corresponding generated font include directory. */
#define main pocketos_application_main
#include "../apps/pocketos-shell.c"
#undef main
int main(void){
    const int sizes[]={8,10,11,12,13,14,15,16,17,18,20,22,23,24,25,28,29,32,40,48,64};
    layout_depth=1;
    for(unsigned h=0;h<sizeof(sizes)/sizeof(sizes[0]);h++)for(int bold=0;bold<2;bold++)for(int batch=0;batch<12;batch++){
        for(unsigned i=0;i<CW*CH;i++)canvas[i]=RGB(246,242,229);
        for(int item=0;item<8;item++){
            int code=32+batch*8+item;if(code>126)break;
            char glyph[]={(char)code,0};
            reader_draw(3+item%4*60,3+item/4*(sizes[h]+8),glyph,sizes[h],RGB(24,32,45),RGB(246,242,229),0,bold,W,0);
        }
        printf("%d %d %d %08x\n",sizes[h],bold,batch,classic_frame_hash());
    }
    return 0;
}
