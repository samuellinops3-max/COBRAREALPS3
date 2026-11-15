// Slither PS3 - igual slither.io original, offline, PS3 (PSL1GHT)
// Entra direto na tela de skin -> X -> jogo com bots
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <malloc.h>
#include <ppu-types.h>
#include <sysutil/sysutil.h>
#include <sysutil/video.h>
#include <rsx/rsx.h>
#include <io/pad.h>

#define CB_SIZE   (0x100000)
#define HOST_SIZE (32 * 1024 * 1024)
#define WORLD  3200.0f
#define MAXSEG 420
#define NSN    10
#define NPEL   500
#define SP     7.0f
#define PI     3.14159265f

typedef struct {
    float x[MAXSEG], y[MAXSEG];
    int len, alive, respawn, boost, grow, score, tick;
    float ang, tgt;
    int skin;
} Snake;

typedef struct {
    float x, y;
    int size, active;
    u32 col;
} Pellet;

// SKINS IGUAIS DA FOTO - 3 cores por cobra, vai alternando
static const u32 skinCols[7][3] = {
    {0x00FF4D4D, 0x00FFD23F, 0x0055D6FF}, // vermelho/amarelo/azul
    {0x0066FF66, 0x00FFD23F, 0x0055D6FF}, // verde/amarelo/azul - igual foto
    {0x00FF4D4D, 0x00FF8FAB, 0x00FF4D4D}, // vermelha neon
    {0x00C77DFF, 0x00FF8FAB, 0x00C77DFF}, // roxa
    {0x00FFFFFF, 0x00DDDDDD, 0x00FFFFFF}, // branca
    {0x0066FF66, 0x0000E5C0, 0x0066FF66}, // verde
    {0x00FF9F43, 0x00FFD23F, 0x00FF9F43}, // laranja
};
static const u32 pelletCols[8] = {
    0x00FF4D4D, 0x0055D6FF, 0x0066FF66, 0x00FFD23F,
    0x00C77DFF, 0x00FF8FAB, 0x0000E5C0, 0x00FF9F43
};

static gcmContextData *ctx;
static u32 W, H;
static u32 *fb[2];
static u32 fboff[2];
static u32 *back;
static int cur = 0, first_flip = 1;
static volatile int running = 1;
static int gameState = 0; // 0 = escolher skin, 1 = jogo
static int selSkin = 1;
static int frameCount = 0;

static void sysutil_cb(u64 status, u64 param, void *usrdata){ (void)param; (void)usrdata; if(status==SYSUTIL_EXIT_GAME) running=0; }

static void init_screen(void){
    void *host = memalign(1024*1024, HOST_SIZE);
    rsxInit(&ctx, CB_SIZE, HOST_SIZE, host);
    videoState st; videoGetState(VIDEO_PRIMARY, 0, &st);
    u32 resid = st.displayMode.resolution;
    if(videoGetResolutionAvailability(VIDEO_PRIMARY, VIDEO_RESOLUTION_720, VIDEO_ASPECT_AUTO, 0)) resid = VIDEO_RESOLUTION_720;
    videoResolution res; videoGetResolution(resid, &res); W=res.width; H=res.height;
    videoConfiguration vc; memset(&vc,0,sizeof(vc));
    vc.resolution=resid; vc.format=VIDEO_BUFFER_FORMAT_XRGB; vc.aspect=VIDEO_ASPECT_AUTO; vc.pitch=W*4;
    videoConfigure(VIDEO_PRIMARY,&vc,NULL,0);
    gcmSetFlipMode(GCM_FLIP_VSYNC);
    for(int i=0;i<2;i++){ fb[i]=(u32*)rsxMemalign(64,W*H*4); gcmAddressToOffset(fb[i],&fboff[i]); gcmSetDisplayBuffer(i,fboff[i],W*4,W,H); }
    back=(u32*)memalign(64,W*H*4); memset(back,0,W*H*4); gcmResetFlipStatus();
}
static void wait_flip(void){ while(gcmGetFlipStatus()!=0) usleep(200); gcmResetFlipStatus(); }
static void present(void){
    if(!first_flip) wait_flip(); else gcmResetFlipStatus();
    memcpy(fb[cur],back,W*H*4); gcmSetFlip(ctx,cur); rsxFlushBuffer(ctx); gcmSetWaitFlip(ctx); cur^=1; first_flip=0;
}
static void fill_rect(int x,int y,int w,int h,u32 c){
    int x0=x<0?0:x, y0=y<0?0:y, x1=x+w, y1=y+h;
    if(x1>(int)W) x1=W; if(y1>(int)H) y1=H;
    for(int yy=y0;yy<y1;yy++){ u32 *p=back+yy*W; for(int xx=x0;xx<x1;xx++) p[xx]=c; }
}
static void circle(int cx,int cy,int r,u32 c){
    if(r<1) r=1;
    if(cx+r<0||cx-r>=(int)W||cy+r<0||cy-r>=(int)H) return;
    for(int dy=-r;dy<=r;dy++){ int y=cy+dy; if(y<0||y>=(int)H) continue; int hw=(int)sqrtf((float)(r*r-dy*dy)); int x0=cx-hw, x1=cx+hw; if(x0<0)x0=0; if(x1>=(int)W)x1=W-1; u32 *p=back+y*W; for(int x=x0;x<=x1;x++) p[x]=c; }
}
// linha fina pra hexagono - barata
static void hline(int x,int y,int w,u32 c){ fill_rect(x,y,w,1,c); }
static void vline(int x,int y,int h,u32 c){ fill_rect(x,y,1,h,c); }

typedef struct { char c; const char *p; } Glyph;
static const Glyph glyphs[] = {
    {'0',"###" "#.#" "#.#" "#.#" "###"}, {'1',".#." "##." ".#." ".#." "###"},
    {'2',"###" "..#" "###" "#.." "###"}, {'3',"###" "..#" "###" "..#" "###"},
    {'4',"#.#" "#.#" "###" "..#" "..#"}, {'5',"###" "#.." "###" "..#" "###"},
    {'6',"###" "#.." "###" "#.#" "###"}, {'7',"###" "..#" "..#" "..#" "..#"},
    {'8',"###" "#.#" "###" "#.#" "###"}, {'9',"###" "#.#" "###" "..#" "###"},
    {'A',".#." "#.#" "###" "#.#" "#.#"}, {'B',"##." "#.#" "##." "#.#" "##."},
    {'C',"###" "#.." "#.." "#.." "###"}, {'E',"###" "#.." "###" "#.." "###"},
    {'G',"###" "#.." "#.#" "#.#" "###"}, {'H',"#.#" "#.#" "###" "#.#" "#.#"},
    {'I',"###" ".#." ".#." ".#." "###"}, {'L',"#.." "#.." "#.." "#.." "###"},
    {'M',"#.#" "###" "###" "#.#" "#.#"}, {'N',"#.#" "###" "###" "#.#" "#.#"},
    {'O',"###" "#.#" "#.#" "#.#" "###"}, {'P',"###" "#.#" "###" "#.." "#.."},
    {'R',"##." "#.#" "##." "#.#" "#.#"}, {'S',"###" "#.." "###" "..#" "###"},
    {'T',"###" ".#." ".#." ".#." ".#."}, {'U',"#.#" "#.#" "#.#" "#.#" "###"},
    {'V',"#.#" "#.#" "#.#" "#.#" ".#."}, {'X',"#.#" "#.#" ".#." "#.#" "#.#"},
    {'Y',"#.#" "#.#" ".#." ".#." ".#."}, {':',"..." ".#." "..." ".#." "..."},
    {'.',"..." "..." "..." "..." ".#."}, {'-', "###" "..." "..." "..." "..."},
};
static const char *find_glyph(char c){ for(unsigned i=0;i<sizeof(glyphs)/sizeof(glyphs[0]);i++) if(glyphs[i].c==c) return glyphs[i].p; return NULL; }
static int text_width(const char *s,int sc){ return (int)strlen(s)*4*sc - sc; }
static void draw_text(const char *s,int x,int y,int sc,u32 c){
    for(;*s;s++){ const char *g=find_glyph(*s); if(g) for(int r=0;r<5;r++) for(int k=0;k<3;k++) if(g[r*3+k]=='#') fill_rect(x+k*sc,y+r*sc,sc,sc,c); x+=4*sc; }
}
static void draw_text_upper(const char *s,int x,int y,int sc,u32 c){
    char buf[64]; int n=0; for(;s[n]&&n<60;n++) buf[n]=(s[n]>='a'&&s[n]<='z')?s[n]-32:s[n]; buf[n]=0; draw_text(buf,x,y,sc,c);
}

static Snake sn[NSN];
static Pellet pel[NPEL];
static int pelrot=0, dead_t=0, x_prev=0;
static float camx=WORLD/2, camy=WORLD/2;
static float pad_ax=0, pad_ay=0, pad_mag=0;
static int pad_boost=0, pad_x=0, pad_up=0, pad_down=0;

static float frand(void){ return (float)rand()/(float)RAND_MAX; }
static float radius_of(const Snake *s){ int l=s->len>240?240:s->len; return 10.0f + l*0.035f; }
static u32 skin_color(int skin,int idx){ return skinCols[skin%7][idx%3]; }

static void add_pellet(float x,float y,int size){
    for(int k=0;k<NPEL;k++){ int i=(pelrot+k)%NPEL; if(!pel[i].active){ pel[i].x=x; pel[i].y=y; pel[i].size=size; pel[i].active=1; pel[i].col=pelletCols[rand()%8]; pelrot=(i+1)%NPEL; return; } }
}
static void spawn_snake(int i,int skin){
    Snake *s=&sn[i]; float x=400+frand()*(WORLD-800), y=400+frand()*(WORLD-800);
    s->ang=frand()*2*PI; s->tgt=s->ang; s->len=12; s->skin=skin;
    for(int j=0;j<s->len;j++){ s->x[j]=x-cosf(s->ang)*SP*j; s->y[j]=y-sinf(s->ang)*SP*j; }
    s->alive=1; s->respawn=0; s->boost=0; s->grow=0; s->tick=0; s->score=12;
}
static void kill_snake(int i){
    Snake *s=&sn[i];
    for(int j=0;j<s->len;j+=2) add_pellet(s->x[j]+(frand()-0.5f)*10, s->y[j]+(frand()-0.5f)*10, 2);
    s->alive=0; s->respawn=240; if(i==0) dead_t=0;
}
static void bot_think(int i){
    Snake *s=&sn[i]; float hx=s->x[0], hy=s->y[0];
    s->tgt += (frand()-0.5f)*0.25f;
    if(s->tick%6==0){
        float best=380*380; int bi=-1;
        for(int k=0;k<NPEL;k++){ if(!pel[k].active) continue; float dx=pel[k].x-hx, dy=pel[k].y-hy, d=dx*dx+dy*dy; if(d<best){best=d; bi=k;} }
        if(bi>=0) s->tgt=atan2f(pel[bi].y-hy, pel[bi].x-hx);
    }
    if(hx<250||hx>WORLD-250||hy<250||hy>WORLD-250) s->tgt=atan2f(WORLD/2-hy,WORLD/2-hx);
    float px=hx+cosf(s->ang)*55, py=hy+sinf(s->ang)*55; float rs=radius_of(s);
    for(int o=0;o<NSN;o++){ if(o==i||!sn[o].alive) continue; float lim=rs+radius_of(&sn[o])+25; lim*=lim; for(int j=0;j<sn[o].len;j+=2){ float dx=sn[o].x[j]-px, dy=sn[o].y[j]-py; if(dx*dx+dy*dy<lim){ s->tgt=s->ang+1.6f; return; } } }
}
static void update_snake(int i){
    Snake *s=&sn[i];
    if(!s->alive){ if(i>0 && --s->respawn<=0) spawn_snake(i, rand()%7); return; }
    s->tick++;
    if(i==0){ if(pad_mag>0.3f) s->tgt=atan2f(pad_ay,pad_ax); s->boost=pad_boost; }
    else { bot_think(i); s->boost=(s->len>30 && (s->tick/90)%5==0)?1:0; }
    float d=s->tgt-s->ang; while(d>PI) d-=2*PI; while(d<-PI) d+=2*PI; if(d>0.075f) d=0.075f; if(d<-0.075f) d=-0.075f; s->ang+=d;
    float sp=3.2f;
    if(s->boost && s->len>12){ sp=5.6f; if(s->tick%10==0){ s->len--; add_pellet(s->x[s->len], s->y[s->len], 1); } }
    s->x[0]+=cosf(s->ang)*sp; s->y[0]+=sinf(s->ang)*sp;
    for(int j=1;j<s->len;j++){ float dx=s->x[j-1]-s->x[j], dy=s->y[j-1]-s->y[j]; float dd=sqrtf(dx*dx+dy*dy); if(dd>SP){ float f=(dd-SP)/dd; s->x[j]+=dx*f; s->y[j]+=dy*f; } }
    if(s->x[0]<0||s->x[0]>WORLD||s->y[0]<0||s->y[0]>WORLD){ kill_snake(i); return; }
    float rs=radius_of(s), er=rs+12;
    for(int k=0;k<NPEL;k++){ if(!pel[k].active) continue; float dx=pel[k].x-s->x[0], dy=pel[k].y-s->y[0]; if(dx*dx+dy*dy<er*er){ pel[k].active=0; s->grow+=pel[k].size; s->score+=pel[k].size*5; } }
    while(s->grow>=2 && s->len<MAXSEG){ s->x[s->len]=s->x[s->len-1]; s->y[s->len]=s->y[s->len-1]; s->len++; s->grow-=2; }
    if(s->len>=MAXSEG) s->grow=0;
    for(int o=0;o<NSN;o++){ if(o==i||!sn[o].alive) continue; float lim=(rs+radius_of(&sn[o]))*0.75f; lim*=lim; for(int j=0;j<sn[o].len;j++){ float dx=sn[o].x[j]-s->x[0], dy=sn[o].y[j]-s->y[0]; if(dx*dx+dy*dy<lim){ kill_snake(i); return; } } }
}
static void init_game(int skin){
    memset(pel,0,sizeof(pel));
    for(int k=0;k<NPEL;k++) add_pellet(frand()*WORLD, frand()*WORLD, 1+(rand()%5==0));
    for(int i=0;i<NSN;i++) spawn_snake(i, i==0?skin:rand()%7);
    dead_t=0; camx=sn[0].x[0]; camy=sn[0].y[0];
}
static void read_pad(void){
    padInfo info; padData d; pad_ax=pad_ay=pad_mag=0; pad_boost=pad_x=pad_up=pad_down=0;
    ioPadGetInfo(&info);
    if(info.status[0]){
        ioPadGetData(0,&d);
        float ax=((int)d.ANA_L_H-128)/128.0f, ay=((int)d.ANA_L_V-128)/128.0f;
        if(d.BTN_LEFT) ax=-1; if(d.BTN_RIGHT) ax=1; if(d.BTN_UP) ay=-1; if(d.BTN_DOWN) ay=1;
        pad_ax=ax; pad_ay=ay; pad_mag=sqrtf(ax*ax+ay*ay);
        pad_boost=d.BTN_CROSS||d.BTN_R1||d.BTN_R2; pad_x=d.BTN_CROSS;
        pad_up=d.BTN_UP; pad_down=d.BTN_DOWN;
        // analogico pra cima/baixo na tela de skin
        if(ay<-0.5f) pad_up=1; if(ay>0.5f) pad_down=1;
    }
}
static void update_game(void){
    for(int i=0;i<NSN;i++) update_snake(i);
    int count=0; for(int k=0;k<NPEL;k++) count+=pel[k].active;
    for(int n=0;n<2 && count<NPEL-40;n++,count++) add_pellet(frand()*WORLD, frand()*WORLD, 1+(rand()%5==0));
    if(sn[0].alive){ camx=sn[0].x[0]; camy=sn[0].y[0]; }
    else { dead_t++; if(dead_t>90){ gameState=0; } }
    x_prev=pad_x;
}

// ===== RENDER SLITHER.IO ORIGINAL =====
static void render_skin_select(void){
    // fundo preto igual original
    u32 n=W*H; for(u32 i=0;i<n;i++) back[i]=0x00000000;
    int ui=4; float sc=(float)W/1280.0f; ui=(int)(4*sc+0.5f); if(ui<3) ui=3;
    draw_text_upper("CHOOSE YOUR SNAKE", W/2 - text_width("CHOOSE YOUR SNAKE", ui*2)/2, 40, ui*2, 0x00FFFFFF);
    draw_text_upper("APERTE X PARA JOGAR", W/2 - text_width("APERTE X PARA JOGAR", ui)/2, 80+ui*6, ui, 0x00809BBF);
    for(int s=0;s<7;s++){
        int y=170 + s*120;
        if(s==selSkin) fill_rect(W/2-480, y-28, 960, 62, 0x00222222);
        // cobrinha de exemplo igual foto
        for(int k=0;k<14;k++){
            int x= W/2 - 350 + k*28;
            u32 col=skin_color(s,k);
            // glow falso barato
            circle(x, y, 20, (col>>1)&0x007F7F7F);
            circle(x, y, 15, col);
        }
        // olho
        circle(W/2-350, y-7, 6, 0x00FFFFFF); circle(W/2-350, y+7, 6, 0x00FFFFFF);
        circle(W/2-347, y-7, 3, 0x00000000); circle(W/2-347, y+7, 3, 0x00000000);
    }
}
static void render_game(void){
    float sc=(float)W/1280.0f; float hw=W/2.0f, hh=H/2.0f;
    // FUNDO PRETO + HEXAGONO ESCURO - leve, so linhas visiveis
    u32 n=W*H; for(u32 i=0;i<n;i++) back[i]=0x00000000;
    // hexagono: grade 90px, desloca com camera, igual foto
    int grid=90;
    int offX=((int)(camx*sc))%grid; if(offX<0) offX+=grid;
    int offY=((int)(camy*sc))%grid; if(offY<0) offY+=grid;
    u32 hexCol=0x001A1A1A;
    for(int x=-offX;x<(int)W;x+=grid) vline(x,0,H,hexCol);
    for(int y=-offY;y<(int)H;y+=grid) hline(0,y,W,hexCol);
    // borda do mapa vermelha escura
    int sx0=(int)((0-camx)*sc+hw), sx1=(int)((WORLD-camx)*sc+hw);
    int sy0=(int)((0-camy)*sc+hh), sy1=(int)((WORLD-camy)*sc+hh);
    u32 red=0x00380E12;
    if(sx0>0) fill_rect(0,0,sx0,H,red); if(sx1<(int)W) fill_rect(sx1,0,W-sx1,H,red);
    if(sy0>0) fill_rect(0,0,W,sy0,red); if(sy1<(int)H) fill_rect(0,sy1,W,H-sy1,red);

    // COMIDA BRILHANDO igual foto
    for(int k=0;k<NPEL;k++){
        if(!pel[k].active) continue;
        int px=(int)((pel[k].x-camx)*sc+hw), py=(int)((pel[k].y-camy)*sc+hh);
        if(px<-30||px>(int)W+30||py<-30||py>(int)H+30) continue;
        float pul=1.0f+0.25f*sinf(frameCount*0.12f + k);
        int r=(int)((5+pel[k].size*2)*sc*pul);
        circle(px,py,r*2,(pel[k].col>>1)&0x007F7F7F); // glow
        circle(px,py,r,pel[k].col);
        circle(px,py,r/3,0x00FFFFFF); // brilho branco no meio
    }
    // COBRAS BRILHANDO - de tras pra frente
    for(int i=NSN-1;i>=0;i--){
        Snake *s=&sn[i]; if(!s->alive) continue;
        int r=(int)(radius_of(s)*sc);
        for(int j=s->len-1;j>=0;j--){
            int px=(int)((s->x[j]-camx)*sc+hw), py=(int)((s->y[j]-camy)*sc+hh);
            if(px<-60||px>(int)W+60||py<-60||py>(int)H+60) continue;
            u32 col=skin_color(s->skin, j);
            circle(px,py,r+6,(col>>1)&0x007F7F7F); // glow externo
            circle(px,py,r,col);
        }
        int hx=(int)((s->x[0]-camx)*sc+hw), hy=(int)((s->y[0]-camy)*sc+hh);
        // olhos grandes iguais foto
        for(int e=-1;e<=1;e+=2){
            int ex=hx+(int)(cosf(s->ang+e*0.7f)*r*0.6f);
            int ey=hy+(int)(sinf(s->ang+e*0.7f)*r*0.6f);
            circle(ex,ey,r*38/100+1,0x00FFFFFF);
            circle(ex+(int)(cosf(s->ang)*r*0.18f), ey+(int)(sinf(s->ang)*r*0.18f), r*20/100+1, 0x00000000);
        }
    }
    // HUD IGUAL ORIGINAL - leaderboard canto superior direito
    int ui=(int)(4*sc+0.5f); if(ui<3) ui=3;
    // leaderboard
    int lbW=280, lbH=180;
    fill_rect(W-lbW-20, 20, lbW, lbH, 0x00000000);
    draw_text_upper("LEADERBOARD", W-lbW+10, 28, ui, 0x00FFFFFF);
    // ordena simples: player + bots por score
    char buf[32];
    // mostra top 5: player em primeiro se for maior, senao lista bots
    for(int t=0;t<5 && t<NSN;t++){
        int idx=t; // simplificado: mostra na ordem, player sempre aparece
        if(t==0) idx=0; else idx=t;
        sprintf(buf, "%d", sn[idx].score);
        draw_text_upper(t==0?"YOU":"BOT", W-lbW+10, 60+t*22, ui, t==0?0x00FFD23F:0x00FFFFFF);
        draw_text(buf, W-80, 60+t*22, ui, 0x00FFFFFF);
    }
    // canto inferior esquerdo igual foto: Your length / Your rank
    draw_text_upper("YOUR LENGTH", 24, H-90, ui, 0x00FFFFFF);
    sprintf(buf, "%d", sn[0].len*10);
    draw_text(buf, 24, H-60, ui+2, 0x00FFD23F);
    draw_text_upper("YOUR RANK", 200, H-90, ui, 0x00FFFFFF);
    sprintf(buf, "#%d OF %d", 1, NSN);
    draw_text(buf, 200, H-60, ui, 0x00FFFFFF);
    if(!sn[0].alive){
        int bw=text_width("YOU DIED", ui*2)+80, bh=16*ui+40;
        fill_rect(W/2-bw/2, H/2-bh/2, bw, bh, 0x00000000);
        draw_text_upper("YOU DIED", W/2-text_width("YOU DIED", ui*2)/2, H/2-bh/2+24, ui*2, 0x00FF4D4D);
    }
}
int main(int argc,char **argv){
    (void)argc; (void)argv;
    init_screen(); ioPadInit(7); sysUtilRegisterCallback(0, sysutil_cb, NULL);
    srand(20261109);
    init_game(selSkin);
    while(running){
        sysUtilCheckCallback(); read_pad(); frameCount++;
        if(gameState==0){
            // tela de skin - navega com cima/baixo
            static int cooldown=0;
            if(cooldown>0) cooldown--;
            if(cooldown==0 && (pad_up||pad_down)){
                if(pad_down) selSkin=(selSkin+1)%7; else selSkin=(selSkin+6)%7;
                cooldown=12;
            }
            if(pad_x && !x_prev){ init_game(selSkin); gameState=1; }
            x_prev=pad_x;
            render_skin_select();
        } else {
            update_game();
            render_game();
            if(!sn[0].alive && dead_t>90) gameState=0;
        }
        present();
    }
    return 0;
}