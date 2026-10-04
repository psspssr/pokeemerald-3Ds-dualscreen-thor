/* Actual final-pass sampling from unchanged320x240 LCD pixels. A high-detail
 * crop would fail this oracle if first stretched into another LCD texture. */
static void bottomContentChecks(C3D_RenderTarget *restore)
{
    const unsigned widths[]={1240,1920};
    for(unsigned panel=0;panel<2;panel++) {
        unsigned width=widths[panel],height=1080;
        C3D_RenderTarget *canvas=C3D_RenderTargetCreate(width,height,GPU_RB_RGBA8,-1);
        unsigned char *rgba=malloc((size_t)width*height*4);
        assert(canvas && rgba);
        for(int mode=CTR_HOST_BOTTOM_FIELD;mode<=CTR_HOST_BOTTOM_WHOLE;mode++) {
            unsigned sourceX=mode==CTR_HOST_BOTTOM_WHOLE?40:0;
            unsigned contentWidth=mode==CTR_HOST_BOTTOM_WHOLE?width:(width*3+3)/4;
            gfxSetScreenFormat(GFX_BOTTOM,GSP_RGB565_OES);
            u16 *lcd=(u16 *)gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,NULL,NULL);
            for(unsigned x=0;x<320;x++) for(unsigned y=0;y<240;y++) {
                u16 color=0xf800; // decoration must not leak into the crop
                if(x>=sourceX && x<sourceX+240 && y>=40 && y<200)
                    color=((x-sourceX)^(y-40))&1?0xffff:0;
                if(mode==CTR_HOST_BOTTOM_FIELD && x>=240)
                    color=y>=238?0x07e0:0x001f;
                lcd[x*240+239-y]=color;
            }
            gfxFlushBuffers(); gpuFlushScreens(); bottomContent=mode;
            for(int filter=0;filter<2;filter++) {
                C3D_FrameDrawOn(canvas);
                gpuTestDrawScreen(GFX_BOTTOM,(CtrHostRect){0,0,width,height},width,height,filter);
                glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
                assert(glGetError()==GL_NO_ERROR);
                unsigned checked=0,mixed=0;
                for(unsigned y=0;y<height;y++) for(unsigned x=0;x<width;x++) {
                    const unsigned char *p=rgba+((size_t)(height-1-y)*width+x)*4;
                    assert(p[3]==255);
                    if(x>=contentWidth) { assert(p[0]==0); continue; }
                    assert(p[0]==p[1] && p[1]==p[2]); // no red/blue border bleed
                    mixed+=p[0]>0 && p[0]<255;
                    if(!filter) {
                        assert(p[0]==0 || p[0]==255);
                        unsigned u=(2*x+1)*240,v=(2*y+1)*160;
                        if(u%(2*contentWidth) && v%(2*height)) {
                            unsigned expected=((u/(2*contentWidth))^(v/(2*height)))&1?255:0;
                            if(p[0]!=expected) {
                                fprintf(stderr,"FAIL bottom menu mode%d %ux%u (%u,%u): got%u expected%u\n",
                                        mode,width,height,x,y,p[0],expected);
                                abort();
                            }
                            checked++;
                        }
                    }
                }
                assert(filter?mixed>contentWidth*height/2:mixed==0);
                pixel(0,height-1,0,0,0,255);
                pixel(contentWidth-1,height-1,255,255,255,255);
                pixel(0,0,255,255,255,255);
                pixel(contentWidth-1,0,0,0,0,255);
                if(mode==CTR_HOST_BOTTOM_FIELD) {
                    pixel(contentWidth,height-1,0,0,255,255);
                    pixel(width-1,0,0,255,0,255);
                }
                printf("PASS bottom menu mode%d %ux%u %s: %u unresampled source samples, %u mixed pixels\n",
                       mode,width,height,filter?"linear":"nearest",checked,mixed);
                checks++;
            }
            /* Fit/phone fallback restores the original decorative margin. */
            bottomContent=CTR_HOST_BOTTOM_ORIGINAL;
            gpuTestDrawScreen(GFX_BOTTOM,(CtrHostRect){0,0,width,height},width,height,0);
            pixel(0,height-1,255,0,0,255);
        }
        free(rgba); C3D_RenderTargetDelete(canvas);
    }
    bottomContent=CTR_HOST_BOTTOM_ORIGINAL;
    C2D_Prepare(); C2D_SceneBegin(restore); C2D_ViewReset();
}
