extern "C" int kmain();

void startup()
{
    kmain();
}

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define VIDEO_BUF_PTR (0xB8000)
#define GDT_CS (0x08)
#define IDT_TYPE_INTR (0x0E)
#define PIC1_PORT (0x20)
#define CURSOR_PORT (0x3D4)
#define VIDEO_WIDTH (80)
#define MAX_ROW (25)
#define MAX_CMD (40)
#define COLOR_ADDR ((volatile u8*)0x1000)

#define GRAY (0x07)
#define WHITE (0x0F)
#define YELLOW (0x0E)
#define BLUE (0x09)
#define RED (0x04)
#define GREEN (0x02)

#pragma pack(push, 1)
struct idt_entry {
    u16 base_lo;
    u16 segm_sel;
    u8  always0;
    u8  flags;
    u16 base_hi;
};
struct idt_ptr {
    u16 limit;
    u32 base;
};
#pragma pack(pop)

static u8  g_color;
static int g_row = 1;
static int g_col = 0;
static int g_shift = 0;

static char g_cmd[MAX_CMD + 1];
static int  g_cmdlen = 0;

static struct idt_entry g_idt[256];
static struct idt_ptr g_idtp;

static const char klava[2][64] = {
    { 0,0,'1','2','3','4','5','6','7','8','9','0','-','=',
      0,0,'q','w','e','r','t','y','u','i','o','p','[',']',
      0,0,'a','s','d','f','g','h','j','k','l',';','\'','`',
      0,'\\','z','x','c','v','b','n','m',',','.','/'},
    { 0,0,'!','@','#','$','%','^','&','*','(',')','_','+',
      0,0,'Q','W','E','R','T','Y','U','I','O','P','{','}',
      0,0,'A','S','D','F','G','H','J','K','L',':','"','~',
      0,'|','Z','X','C','V','B','N','M','<','>','?'}
};

typedef void (*intr_handler)();
static u8   inb(u16 port);
static void outb(u16 port, u8 data);
static void outw(u16 port, u16 data);
void out_str(int color, const char* s, int row);
void cursor_moveto(int row, int col);
int  row_check(int row);
void scroll_up();
void intr_reg_handler(int num, u16 sel, u16 flags, intr_handler h);
void default_intr_handler();
void keyb_handler();
static void keyb_process();
void intr_init();
void keyb_init();
void intr_start();
void intr_enable();
void intr_disable();
void on_key(unsigned char sc);
void print_prompt();
void functional(char* buf);
int  string_split(char* str, char* toks[], int max);
int  kstrcmp(const char* a, const char* b);
int  kstrlen(const char* s);
static void println(const char* s);
void info();
void cmd_clear();
void do_shutdown();
void error_command();
void cmd_nsconv(char* num, char* sfrom, char* sto);
void cmd_posix(char* s);
void cmd_wintime(char* s);
int  check_digits(const char* s);
int  check_overflow_u32(const char* s);
int  leap_year(int y);
void itos(int n, char* buf, int minlen);
u32  decod_conv(const char* s, int base, int* err);
void conv_to_base(u32 n, int base, char* out);
void posix_to_dt(u32 t, int* Y,int* Mo,int* D,int* hh,int* mm,int* ss);
static u32  str_divmod32(const char* n, int nl, u32 div, char* q, int* ql);
static int  str_sub_dec(const char* a,int al,const char* b,int bl,char* r,int* rl);
static int  str_cmp_dec(const char* a,int al,const char* b,int bl);

static u8 inb(u16 port) {
    u8 r;
    __asm { mov dx, port
            in  al, dx
            mov r,  al }
    return r;
}
static void outb(u16 port, u8 data) {
    __asm { mov dx, port
            mov al, data
            out dx, al }
}
static void outw(u16 port, u16 data) {
    __asm { mov dx, port
            mov ax, data
            out dx, ax }
}

int row_check(int row) {
    if (row >= MAX_ROW) {
        scroll_up();
        return MAX_ROW - 1;
    }
    return row;
}

void scroll_up() {
    unsigned char* v = (unsigned char*)VIDEO_BUF_PTR;
    for (int i = 0; i < VIDEO_WIDTH * (MAX_ROW - 1) * 2; i++)
        v[i] = v[i + VIDEO_WIDTH * 2];
    for (int i = VIDEO_WIDTH * (MAX_ROW - 1) * 2; i < VIDEO_WIDTH * MAX_ROW * 2; i += 2) {
        v[i] = ' ';
        v[i+1] = g_color;
    }
    g_row = MAX_ROW - 1;
    g_col = 0;
}

void out_str(int color, const char* s, int row) {
    row = row_check(row);
    unsigned char* v = (unsigned char*)VIDEO_BUF_PTR + row * VIDEO_WIDTH * 2;
    while (*s) {
        *v++ = (unsigned char)*s++;
        *v++ = (unsigned char)color;
    }
}

void cursor_moveto(int row, int col) {
    row = row_check(row);
    u16 pos = (u16)(row * VIDEO_WIDTH + col);
    outb(CURSOR_PORT, 0x0F);
    outb(CURSOR_PORT + 1, (u8)(pos & 0xFF));
    outb(CURSOR_PORT, 0x0E);
    outb(CURSOR_PORT + 1, (u8)((pos >> 8) & 0xFF));
}

void intr_reg_handler(int num, u16 sel, u16 flags, intr_handler h) {
    u32 addr = (u32)h;
    g_idt[num].base_lo = (u16)(addr & 0xFFFF);
    g_idt[num].segm_sel = sel;
    g_idt[num].always0 = 0;
    g_idt[num].flags = (u8)flags;
    g_idt[num].base_hi = (u16)(addr >> 16);
}

__declspec(naked) void default_intr_handler() {
    __asm {
        pusha
        mov al, 0x20
        out 0x20, al
        popa
        iretd
    }
}

__declspec(naked) void keyb_handler() {
    __asm {
        pusha
        call keyb_process
        mov  al, 0x20
        out  0x20, al
        popa
        iretd
    }
}

void intr_init() {
    int n = sizeof(g_idt) / sizeof(g_idt[0]);
    for (int i = 0; i < n; i++)
        intr_reg_handler(i, GDT_CS, (u16)(0x80 | IDT_TYPE_INTR), default_intr_handler);
}

void keyb_init() {
    intr_reg_handler(0x09, GDT_CS, (u16)(0x80 | IDT_TYPE_INTR), keyb_handler);
    outb(PIC1_PORT + 1, (u8)(0xFF ^ 0x02));
}

void intr_start() {
    int n = sizeof(g_idt) / sizeof(g_idt[0]);
    g_idtp.base  = (u32)(&g_idt[0]);
    g_idtp.limit = (u16)(sizeof(struct idt_entry) * n - 1);
    __asm { lidt g_idtp }
}
void intr_enable()  { __asm { sti } }
void intr_disable() { __asm { cli } }

static void keyb_process() {
    if (!(inb(0x64) & 0x01)) return;
    unsigned char sc = inb(0x60);

    if (sc == 42 || sc == 54)  {
        g_shift = 1;
        return;
    }
    if (sc == 170 || sc == 182) {
        g_shift = 0;
        return;
    }

    if (sc >= 128) return;
    on_key(sc);
}

void on_key(unsigned char sc) {
    unsigned char* v = (unsigned char*)VIDEO_BUF_PTR;

    if (sc == 14) {
        if (g_col == 0) return;
        g_col--;
        g_cmdlen--;
        g_cmd[g_cmdlen] = '\0';
        int off = (g_row * VIDEO_WIDTH + g_col) * 2;
        v[off]   = ' ';
        v[off+1] = g_color;
        cursor_moveto(g_row, g_col);
        return;
    }

    if (sc == 28) {
        g_cmd[g_cmdlen] = '\0';
        g_row++;
        g_col    = 0;
        g_cmdlen = 0;
        functional(g_cmd);
        print_prompt();
        return;
    }

    if (sc == 57) {
        if (g_cmdlen >= MAX_CMD) return;
        int off = (g_row * VIDEO_WIDTH + g_col) * 2;
        v[off]   = ' ';
        v[off+1] = g_color;
        g_cmd[g_cmdlen++] = ' ';
        g_col++;
        cursor_moveto(g_row, g_col);
        return;
    }

    if (sc < 2 || sc > 53) return;
    char sym = klava[g_shift ? 1 : 0][(int)sc];
    if (!sym) return;

    int ok = (sym >= 'a' && sym <= 'z')
          || (sym >= 'A' && sym <= 'Z')
          || (sym >= '0' && sym <= '9')
          || sym == '+' || sym == '-'
          || sym == '*' || sym == '/';
    if (!ok) return;
    if (g_cmdlen >= MAX_CMD) return;

    int off = (g_row * VIDEO_WIDTH + g_col) * 2;
    v[off]   = (unsigned char)sym;
    v[off+1] = g_color;
    g_cmd[g_cmdlen++] = sym;
    g_col++;
    if (g_shift) g_shift = 0;
    cursor_moveto(g_row, g_col);
}

void print_prompt()
{
    g_row = row_check(g_row);
    g_col = 0;
    g_cmdlen = 0;
    for (int i = 0; i <= MAX_CMD; i++) g_cmd[i] = '\0';

    unsigned char* v = (unsigned char*)VIDEO_BUF_PTR + g_row * VIDEO_WIDTH * 2;
    v[0] = '#';  v[1] = g_color;
    v[2] = ' ';  v[3] = g_color;
    g_col = 2;
    cursor_moveto(g_row, g_col);
}

int kstrcmp(const char* a, const char* b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int kstrlen(const char* s) {
    int n = 0;
    while (s[n]) n++; return n;
}

int string_split(char* str, char* toks[], int max)
{
    int cnt = 0;
    while (*str == ' ') str++;
    while (*str && cnt < max) {
        toks[cnt++] = str;
        while (*str && *str != ' ') str++;
        if (*str) {
            *str = '\0';
            str++;
        }
        while (*str == ' ') str++;
    }
    return cnt;
}

static void println(const char* s) {
    out_str(g_color, s, g_row);
    g_row++;
}

void info()
{
    println("ConvertOS");
    println("Developer: Stolbikov Vlad, 5131001/40004, SpbPU, 2025");
    println("Bootloader: FASM  |  Kernel compiler: MS C Compiler");

    const char* cname;
    if (g_color == GRAY) cname = "Bootloader color: gray";
    else if (g_color == WHITE) cname = "Bootloader color: white";
    else if (g_color == YELLOW) cname = "Bootloader color: yellow";
    else if (g_color == BLUE) cname = "Bootloader color: blue";
    else if (g_color == RED) cname = "Bootloader color: red";
    else if (g_color == GREEN) cname = "Bootloader color: green";
    println(cname);
}

void cmd_clear() {
    unsigned char* v = (unsigned char*)VIDEO_BUF_PTR;
    for (int i = 0; i < VIDEO_WIDTH * MAX_ROW * 2; i += 2) {
        v[i] = ' ';
        v[i+1] = g_color;
    }
    const char* wlc = "Welcome to ConvertOS";
    for (int i = 0; wlc[i]; i++) {
        v[i * 2] = (unsigned char)wlc[i];
        v[i * 2 + 1] = g_color;
    }
    g_row = 1;
    g_col = 0;
}

void do_shutdown() {
    println("Powering off...");
    outw(0x604,  0x2000);
    outw(0xB004, 0x2000);
    __asm { cli }
    for (;;) {
        __asm { hlt }
    }
}

void error_command() {
    println("Error: command not recognized");
}

int check_digits(const char* s) {
    if (!s || !*s) {
        println("Error: empty input");
        return 1;
    }
    for (int i = 0; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9') {
            println("Error: invalid symbol");
            return 1;
        }
    }
    return 0;
}

int check_overflow_u32(const char* s) {
    int len = kstrlen(s);
    if (len > 10) {
        println("Error: int overflow");
        return 1;
    }
    if (len == 10 && kstrcmp(s, "4294967295") > 0) {
        println("Error: int overflow");
        return 1;
    }
    return 0;
}

int leap_year(int y) {
    if (y % 400 == 0) return 1;
    if (y % 100 == 0) return 0;
    if (y % 4   == 0) return 1;
    return 0;
}

void itos(int n, char* buf, int minlen) {
    int len = 0;
    char tmp[12];
    do {
    tmp[len++] = (char)('0' + n % 10);
    n /= 10;
    } while (n > 0);
    while (len < minlen) tmp[len++] = '0';
    for (int i = 0; i < len; i++) buf[i] = tmp[len - 1 - i];
    buf[len] = '\0';
}

u32 decod_conv(const char* s, int base, int* err) {
    *err = 0;
    if (!s || !*s) {
        println("Error: empty input");
        *err = 1;
        return 0;
    }
    u32 res = 0;
    for (int i = 0; s[i]; i++) {
        int d;
        char c = s[i];
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
        else {
            println("Error: invalid symbol");
            *err = 1;
            return 0;
        }
        if (d >= base) {
            println("Error: invalid symbol");
            *err = 1;
            return 0;
        }
        u32 old = res;
        res = res * (u32)base + (u32)d;
        if (res < old) {
            println("Error: int overflow");
            *err = 1;
            return 0;
        }
    }
    return res;
}

void conv_to_base(u32 n, int base, char* out) {
    if (n == 0) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }
    char tmp[40];
    int i = 0;
    while (n > 0) {
        int r = (int)(n % (u32)base);
        tmp[i++] = (r < 10) ? (char)('0' + r) : (char)('a' + r - 10);
        n /= (u32)base;
    }
    for (int k = 0; k < i; k++) out[k] = tmp[i - 1 - k];
    out[i] = '\0';
}

void cmd_nsconv(char* num, char* sfrom, char* sto) {
    int bf = 0, bt = 0;
    for (int i = 0; sfrom[i]; i++) {
        if (sfrom[i] < '0' || sfrom[i] > '9') {
            println("Error: invalid base");
            return;
        }
        bf = bf * 10 + (sfrom[i] - '0');
    }
    for (int i = 0; sto[i]; i++) {
        if (sto[i] < '0' || sto[i] > '9') {
            println("Error: invalid base");
            return;
        }
        bt = bt * 10 + (sto[i] - '0');
    }
    if (bf < 2 || bf > 36) {
        println("Error: invalid base");
        return;
    }
    if (bt < 2 || bt > 36) {
        println("Error: invalid base");
        return;
    }

    int err;
    u32 val = decod_conv(num, bf, &err);
    if (err) return;

    char result[64];
    conv_to_base(val, bt, result);
    println(result);
}

void posix_to_dt(u32 t, int* Y, int* Mo, int* D, int* hh, int* mm, int* ss) {
    int year = 1970;
    u32 days = t / 86400u;
    u32 secs = t % 86400u;

    while (days >= (u32)(leap_year(year) ? 366 : 365)) {
        days -= (u32)(leap_year(year) ? 366 : 365);
        year++;
    }
    int dim[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (leap_year(year)) dim[1] = 29;

    int mon;
    for (mon = 0; mon < 12; mon++) {
        if (days < (u32)dim[mon]) break;
        days -= (u32)dim[mon];
    }
    *Y  = year;
    *Mo = mon + 1;
    *D = (int)days + 1;
    *hh = (int)(secs / 3600u);
    secs %= 3600u;
    *mm = (int)(secs / 60u);
    *ss = (int)(secs % 60u);
}

void cmd_posix(char* s) {
    if (check_digits(s)) return;
    if (check_overflow_u32(s)) return;

    u32 t = 0;
    for (int i = 0; s[i]; i++) t = t * 10u + (u32)(s[i] - '0');

    int Y, Mo, D, hh, mm, ss;
    posix_to_dt(t, &Y, &Mo, &D, &hh, &mm, &ss);

    char res[24];
    char tmp[8];
    int ri = 0;
    itos(D,  tmp, 2); res[ri++]=tmp[0]; res[ri++]=tmp[1]; res[ri++]='.';
    itos(Mo, tmp, 2); res[ri++]=tmp[0]; res[ri++]=tmp[1]; res[ri++]='.';
    itos(Y,  tmp, 4); for (int i=0;tmp[i];i++) res[ri++]=tmp[i];
    res[ri++]=' ';
    itos(hh, tmp, 2); res[ri++]=tmp[0]; res[ri++]=tmp[1]; res[ri++]=':';
    itos(mm, tmp, 2); res[ri++]=tmp[0]; res[ri++]=tmp[1]; res[ri++]=':';
    itos(ss, tmp, 2); res[ri++]=tmp[0]; res[ri++]=tmp[1];
    res[ri] = '\0';
    println(res);
}

static u32 str_divmod32(const char* num, int nl, u32 div, char* q, int* ql) {
    u32 rem = 0;
    int qi = 0;
    for (int i = 0; i < nl; i++) {
        u32 cur = rem * 10u + (u32)(num[i] - '0');
        u32 qd = cur / div;
        rem = cur % div;
        if (qi > 0 || qd > 0) q[qi++] = (char)('0' + (int)qd);
    }
    if (qi == 0) q[qi++] = '0';
    q[qi] = '\0';
    if (ql) *ql = qi;
    return rem;
}

static int str_sub_dec(const char* a, int al, const char* b, int bl, char* r, int* rl) {
    int borrow = 0;
    char tmp[32];
    int ti = 0;
    for (int i = 0; i < al; i++) {
        int da = (int)(a[al-1-i] - '0');
        int db = (i < bl) ? (int)(b[bl-1-i] - '0') : 0;
        int d  = da - db - borrow;
        if (d < 0) {
            d += 10;
            borrow = 1;
        } else {
            borrow = 0;
        }
        tmp[ti++] = (char)('0' + d);
    }
    if (borrow) return 0;
    while (ti > 1 && tmp[ti-1] == '0') ti--;
    for (int i = 0; i < ti; i++) r[i] = tmp[ti-1-i];
    r[ti] = '\0'; if (rl) *rl = ti;
    return 1;
}

static int str_cmp_dec(const char* a, int al, const char* b, int bl) {
    if (al != bl) return al < bl ? -1 : 1;
    for (int i = 0; i < al; i++) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return  1;
    }
    return 0;
}

void cmd_wintime(char* s) {
    if (check_digits(s)) return;
    int len = kstrlen(s);
    if (len > 20) { println("Error: overflow"); return; }
    if (len == 20 && str_cmp_dec(s, 20, "18446744073709551615", 20) > 0) {
        println("Error: overflow"); return;
    }
    char sec1601[24]; int sl = 0;
    str_divmod32(s, len, 10000000u, sec1601, &sl);

    char sec1970[24]; int sl2 = 0;
    if (!str_sub_dec(sec1601, sl, "11644473600", 11, sec1970, &sl2)) {
        println("Error: date before 1970"); return;
    }

    if (sl2 > 10) {
        println("Error: date after 2106");
        return;
    }
    u32 pt = 0;
    for (int i = 0; i < sl2; i++) {
        u32 d = (u32)(sec1970[i] - '0');
        if (pt > (0xFFFFFFFFu - d) / 10u) {
            println("Error: date after 2106");
            return;
        }
        pt = pt * 10u + d;
    }

    int Y, Mo, D, hh, mm, ss;
    posix_to_dt(pt, &Y, &Mo, &D, &hh, &mm, &ss);

    char res[24]; char tmp[8]; int ri = 0;
    itos(D,  tmp, 2); res[ri++]=tmp[0]; res[ri++]=tmp[1]; res[ri++]='.';
    itos(Mo, tmp, 2); res[ri++]=tmp[0]; res[ri++]=tmp[1]; res[ri++]='.';
    itos(Y,  tmp, 4); for (int i=0;tmp[i];i++) res[ri++]=tmp[i];
    res[ri++]=' ';
    itos(hh, tmp, 2); res[ri++]=tmp[0]; res[ri++]=tmp[1]; res[ri++]=':';
    itos(mm, tmp, 2); res[ri++]=tmp[0]; res[ri++]=tmp[1]; res[ri++]=':';
    itos(ss, tmp, 2); res[ri++]=tmp[0]; res[ri++]=tmp[1];
    res[ri] = '\0';
    println(res);
}

void functional(char* buf) {
    char* toks[5];
    int n = string_split(buf, toks, 5);
    if (n == 0) return;

    if (n == 1 && kstrcmp(toks[0], "info") == 0) info();
    else if (n == 1 && kstrcmp(toks[0], "clear")    == 0) cmd_clear();
    else if (n == 1 && kstrcmp(toks[0], "shutdown") == 0) do_shutdown();
    else if (n == 4 && kstrcmp(toks[0], "nsconv") == 0) cmd_nsconv(toks[1], toks[2], toks[3]);
    else if (n == 2 && kstrcmp(toks[0], "posixtime") == 0) cmd_posix(toks[1]);
    else if (n == 2 && kstrcmp(toks[0], "wintime") == 0) cmd_wintime(toks[1]);
    else error_command();
}

extern "C" int kmain() {
    g_color = *COLOR_ADDR;
    if (g_color == 0) g_color = GRAY;

    unsigned char* v = (unsigned char*)VIDEO_BUF_PTR;
    for (int i = 0; i < VIDEO_WIDTH * MAX_ROW * 2; i += 2) {
        v[i] = ' ';
        v[i+1] = g_color;
    }

    const char* wlc = "Welcome to ConvertOS!";
    for (int i = 0; wlc[i]; i++) {
        v[i * 2] = (unsigned char)wlc[i];
        v[i * 2 + 1] = g_color;
    }
    g_row = 1;
    g_col = 0;

    intr_init();
    keyb_init();
    intr_start();
    intr_enable();

    print_prompt();

    while (1) { __asm { hlt } }

    return 0;
}
