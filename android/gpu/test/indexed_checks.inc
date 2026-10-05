/* Upstream c330's FastCells batches use Citro2D's public vertex registers
 * through C3D_DrawElements, rather than C2D_DrawImageAt's private CPU batch. */
static void indexedChecks(shaderProgram_s *program,C3D_RenderTarget *target)
{
    C3D_AttrInfo savedAttr=*C3D_GetAttrInfo();
    C3D_BufInfo savedBuf=*C3D_GetBufInfo();
    const int shade=shaderInstanceGetUniformLocation(program->vertexShader,"shadeTint");
    C3D_FVec savedShade=C3D_FVUnif[0][shade];
    C3D_Tex atlas={0}; assert(C3D_TexInit(&atlas,16,8,GPU_RGBA5551));
    for(unsigned i=0;i<128;i++) ((u16 *)atlas.data)[i]=i<64?0xF801:0x07C1;
    C3D_TexSetFilter(&atlas,GPU_NEAREST,GPU_NEAREST);
    const Tex3DS_SubTexture none={0,0,0,1,0,1};
    const struct { short x,y; float u,v; } vertices[]={
        {0,0,0,1},{0,16,0,0},{8,0,.5f,1},{8,16,.5f,0},
        {8,0,.5f,1},{8,16,.5f,0},{16,0,1,1},{16,16,1,0},
    };
    const u16 indices[]={0,1,2,2,1,3,4,5,6,6,5,7};
    const u8 byteIndices[]={4,5,6,6,5,7};
    C2D_Prepare(); C2D_SceneBegin(target); C2D_ViewReset();
    C2D_TargetClear(target,C2D_Color32(0,0,0,255));
    assert(C2D_DrawImageAt((C2D_Image){&atlas,&none},0,0,0,NULL,1,1));
    C2D_Flush();
    AttrInfo_Init(C3D_GetAttrInfo());
    assert(AttrInfo_AddLoader(C3D_GetAttrInfo(),0,GPU_SHORT,2)==0);
    assert(AttrInfo_AddLoader(C3D_GetAttrInfo(),1,GPU_FLOAT,2)==1);
    assert(AttrInfo_AddFixed(C3D_GetAttrInfo(),2)==2);
    assert(AttrInfo_AddFixed(C3D_GetAttrInfo(),3)==3);
    C3D_FixedAttribSet(2,0,0,0,0); C3D_FixedAttribSet(3,255,255,255,255);
    BufInfo_Init(C3D_GetBufInfo());
    BufInfo_Add(C3D_GetBufInfo(),vertices,sizeof(*vertices),2,0x10);
    C3D_AlphaTest(false,GPU_ALWAYS,0);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
    C3D_TexEnvSrc(C3D_GetTexEnv(0),C3D_Both,GPU_TEXTURE0,GPU_PREVIOUS,GPU_PREVIOUS);
    C3D_DrawElements(GPU_TRIANGLES,6,C3D_UNSIGNED_SHORT,indices);
    pixel(2,8,255,0,0,255); pixel(12,8,0,0,0,255);
    /* Index values remain absolute when the upstream passes a slice of its
     * shared index buffer; this must not read vertices from the wrong quad. */
    C3D_DrawElements(GPU_TRIANGLES,6,C3D_UNSIGNED_SHORT,indices+6);
    pixel(2,8,255,0,0,255); pixel(12,8,0,255,0,255);
    C2D_TargetClear(target,C2D_Color32(0,0,0,255));
    C3D_DrawElements(GPU_TRIANGLES,6,C3D_UNSIGNED_BYTE,byteIndices);
    pixel(2,8,0,0,0,255); pixel(12,8,0,255,0,255);
    C3D_TexEnvSrc(C3D_GetTexEnv(0),C3D_Both,GPU_CONSTANT,GPU_PREVIOUS,GPU_PREVIOUS);
    C3D_TexEnvColor(C3D_GetTexEnv(0),0);
    C3D_DrawElements(GPU_TRIANGLES,6,C3D_UNSIGNED_SHORT,indices+6);
    pixel(12,8,0,0,0,0);

    /* Fixed values are indexed by attribute slot, then permuted to shader
     * registers. Changing just those values must bypass the VAO cache. */
    AttrInfo_Init(C3D_GetAttrInfo());
    AttrInfo_AddLoader(C3D_GetAttrInfo(),0,GPU_SHORT,2);
    AttrInfo_AddLoader(C3D_GetAttrInfo(),1,GPU_FLOAT,2);
    AttrInfo_AddFixed(C3D_GetAttrInfo(),3); AttrInfo_AddFixed(C3D_GetAttrInfo(),2);
    C3D_FixedAttribSet(2,255,0,0,255); C3D_FixedAttribSet(3,0,0,0,0);
    assert(!C3D_FixedAttribGetWritePtr(-1) && !C3D_FixedAttribGetWritePtr(12));
    C3D_TexEnvSrc(C3D_GetTexEnv(0),C3D_Both,GPU_PRIMARY_COLOR,GPU_PREVIOUS,GPU_PREVIOUS);
    C3D_DrawElements(GPU_TRIANGLES,6,C3D_UNSIGNED_SHORT,indices);
    pixel(2,8,255,0,0,255);
    C3D_FixedAttribSet(2,0,255,0,255);
    C3D_DrawElements(GPU_TRIANGLES,6,C3D_UNSIGNED_SHORT,indices);
    pixel(2,8,0,255,0,255);
    *C3D_FixedAttribGetWritePtr(2)=FVec4_New(0,0,255,255);
    C3D_DrawElements(GPU_TRIANGLES,6,C3D_UNSIGNED_SHORT,indices);
    pixel(2,8,0,0,255,255);
    C3D_FixedAttribSet(2,255,0,0,255);
    C2D_ViewTranslate(4,4); C2D_ViewScale(.5f,.5f);
    C2D_TargetClear(target,C2D_Color32(0,0,0,255));
    C3D_DrawElements(GPU_TRIANGLES,6,C3D_UNSIGNED_SHORT,indices);
    pixel(5,10,255,0,0,255); pixel(3,10,0,0,0,255); pixel(9,10,0,0,0,255);
    C2D_ViewReset(); C2D_SceneSize(16,16,true);
    C2D_TargetClear(target,C2D_Color32(0,0,0,255));
    C3D_DrawElements(GPU_TRIANGLES,6,C3D_UNSIGNED_SHORT,indices);
    pixel(8,2,255,0,0,255); pixel(8,12,0,0,0,255);
    C2D_SceneSize(16,16,false);
    C3D_DrawElements(GPU_TRIANGLES,0,C3D_UNSIGNED_SHORT,indices);
    C3D_DrawElements(GPU_TRIANGLES,6,77,indices);
    C3D_DrawElements(GPU_TRIANGLES,6,C3D_UNSIGNED_SHORT,NULL);
    pixel(8,12,0,0,0,255);

    /* The next regular sprite must use the CPU-baked shader, and a later
     * voxel draw must restore its own shader/layout rather than raw C2D. */
    C2D_Prepare(); C2D_DrawRectSolid(0,0,0,16,16,C2D_Color32(0,255,0,255));
    pixel(8,8,0,255,0,255);
    C3D_BindProgram(program); C3D_SetAttrInfo(&savedAttr); C3D_SetBufInfo(&savedBuf);
    C3D_TexEnvSrc(C3D_GetTexEnv(0),C3D_Both,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,shade,2,0,0,0);
    C3D_DrawArrays(GPU_TRIANGLES,0,3); pixel(8,8,255,0,0,255);
    C3D_FVUnif[0][shade]=savedShade;
    C3D_TexDelete(&atlas);
    printf("PASS indexed u8/u16 atlas batches, absolute index slices, fixed registers, transforms, empty cells and 2D/3D program transitions\n");
}
