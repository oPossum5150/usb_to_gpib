#include <xc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define _XTAL_FREQ 48000000

/*
 RA0  REN        Remote ENable
 RA1  EOI        End Or Identify
 RA2  DAV        Data AVailable
 RA3  NRFD       Not Ready For Data
 RA4  NDAC       Not Data Acknowledge
 RA5  ATN        ATteNion
 RB   GPIB Data
 RC0  -
 RC1  -
 RC2  -
 RC4  USB D-
 RC5  USB D+
 RC6  Ser Tx
 RC7  Ser Rx
 RD0  Blue LED
 RD1  Green LED
 RD2  Red LED
 RD3  PE   '160  Pullup enable
 RD4  TE   '160  Talk enable
 RD5  DC   '162  Direction control (ATN, SRQ)
 RD6  SC   '162  System control (REN, IFC)
 RD7  TE2  '162  Talk enable (DAV, NRFD, NDAC)
                 EOI is controlled by ATN when DC == TE2, else controlled by TE2/DC
 RE0  SRQ
 RE1  IFC
 RE2  -
*/


enum {           // - GPIB bus commands (sent with ATN asserted)
                 // N = Device address 0 to 30 (0x1E)
    GTL = 0x01,  // Go To Local
    SDC = 0x04,  // Selected Device Clear
    PPC = 0x05,  // Parallel Poll Configure
    GET = 0x08,  // Group Execute Trigger
    TCT = 0x09,  // Take ConTrol
    LLO = 0x11,  // Local LOckout
    DCL = 0x14,  // Device CLear
    PPU = 0x15,  // Parallel Poll Unconfigure
    SPE = 0x18,  // Serial Poll Enable
    SPD = 0x19,  // Serial Poll Disable
    LAD = 0x20,  // Listen ADdress + N
    UNL = 0x3F,  // UNListen
    TAD = 0x40,  // Talk ADdress + N
    UNT = 0x5F,  // UNTalk
    SAD = 0x60,  // Secondary ADdress + N
    PPE = 0x60,  // Parallel Poll Enable -> 0x6F
    PPD = 0x70   // Parallel Poll Disable
};

typedef struct {
    uint8_t     debug;          // Debug flags
    uint16_t    brg;            // Baud rate generator divisor  13 -> 230400
    uint8_t     echo;           // Echo
    uint16_t    talk_timeout;   // Talk timeout
    uint16_t    spoll_timeout;  // Serial poll timeout
                               // -- Prologix
    uint8_t     addr;           // GPIB address to communicate with
    uint8_t     auto_read;      // Automatically read after write
    uint16_t    listen_timeout; // Listen timeout
    uint8_t     eoi;            // Assert EOI at the end of data sent to the GPID device
    uint8_t     eos;            // Character(s) to append to data sent to GPIB device
    uint8_t     eot_enable;     // Enable appending a character to data received from GPIB device
    uint8_t     eot_char;       // Char to append to data received from GPIB device
    uint8_t     status;         // Status returned by serial poll
} TCONFIG;

TCONFIG config = {
    0,          // Debug flags
    13,         // Baud rate generator divisor  13 -> 230400
    1,          // Echo
    100,        // Talk timeout
    100,        // Serial poll timeout
                //
    1,          // Remote address
    2,          // Auto mode
    100,        // Listen timeout
    1,          // eoi = on
    2,          // eos == LF
    0,          // eot enable == off
    '\n',       // eot character
    0,          // serial poll status
};

typedef struct {
    uint16_t listen_timeout;
    uint16_t talk_timeout;
    uint16_t spoll_timeout;
} TTIMEOUT;

TTIMEOUT timeout = { 0 };


void update_brg(void)
{
    uint16_t brg = config.brg - 1;
    SPBRG1 = (uint8_t)brg;
    SPBRGH1 = (uint8_t)(brg >> 8);
}

void print(char const *s)
{
    char c;
    while((c = *s++)) {
        while(!PIR1bits.TXIF);  // Wait for tx reg empty
        TXREG1 = c;
    }
}

void print_nl(void)
{
    print("\r\n");
}

void print_args(char **a)
{
    if(!*a) return;
    print_nl();
    print("Args:");
    while(*a) {
        print(" ");
        print(*a);
        ++a;
    }
    print_nl();
}

void print_uint(unsigned n)
{
#if 1
	char ds[6] = "/:/:/";
	while (n >= 30000) n -= 10000, ++ds[0];
	int16_t i = (int16_t)n;
	do ++ds[0]; while ((i -= 10000) >= 0);
	do --ds[1]; while ((i += 1000) < 0);
	do ++ds[2]; while ((i -= 100) >= 0);
	do --ds[3]; while ((i += 10) < 0);
	do ++ds[4]; while (--i >= 0);
	char *z = ds; while (*z == '0') ++z; if (!*z) --z;
#else
    char z[8];
    itoa(z, n, 10);
#endif
    print(z);
}

void print_ulong(unsigned long n)
{
	char ds[11] = "/:/:/:/:/:";
	while (n >= 2000000000UL) n -= 1000000000UL, ++ds[0];
	long i = (long)n;
	do ++ds[0]; while ((i -= 1000000000L) >= 0);
	do --ds[1]; while ((i += 100000000L) < 0);
	do ++ds[2]; while ((i -= 10000000L) >= 0);
	do --ds[3]; while ((i += 1000000L) < 0);
	do ++ds[4]; while ((i -= 100000L) >= 0);
	do --ds[5]; while ((i += 10000) < 0);
	do ++ds[6]; while ((i -= 1000) >= 0);
	do --ds[7]; while ((i += 100) < 0);
	do ++ds[8]; while ((i -= 10) >= 0);
	do --ds[9]; while (++i < 0);
	char *z = ds; while (*z == '0') ++z; if (!*z) --z;
	print(z);
}

uint8_t atoh(char c)
{
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

uint32_t htou32(char *s)
{
    char c;
    uint32_t u = 0;
    while((c = *s++)) {
        u <<= 4;
        u |= atoh(c);
    }
    return u;
}

uint16_t ms_to_tmr(uint16_t ms)
{
    // 256 / 12,000,000 = 
    // 
    uint32_t t = ms;
    // t *= 375
    t = (t << 8) + (t << 7) - (t << 4) + (t << 3) - t;
    // t /= 8
    t >>= 3;
    if(config.debug & 2) {
        print("tmr ");
        print_uint(ms);
        print(" ms -> ");
        print_uint((unsigned)t);
        print_nl();
    }
    t ^= 0xFFFF;
    return (uint16_t)t;
}

uint16_t us_to_tmr(uint16_t us)
{
    uint32_t t = us;
    // t *= 3;
    t = (t << 1) + t;
    // t /= 64
    t >>= 6;
    if(config.debug & 2) {
        print("tmr ");
        print_uint(us);
        print(" us -> ");
        print_uint((unsigned)t);
        print_nl();
    }
    t ^= 0xFFFF;
    return (uint16_t)t;
}

void update_timers(void)
{
    timeout.listen_timeout = ms_to_tmr(config.listen_timeout);
    timeout.talk_timeout = ms_to_tmr(config.talk_timeout);
    timeout.spoll_timeout = ms_to_tmr(config.spoll_timeout);
}

void gpib_system(uint8_t m)
{
    if(m) {
                            // - Be a system controller
        LATAbits.LA0 = 1;   // REN high
        LATEbits.LE1 = 1;   // IFC high
        LATDbits.LD6 = 1;   // SC System control
        TRISAbits.RA0 = 0;  // REN as output
        TRISEbits.RE1 = 0;  // IFC as output
    } else {
        /// todo
    }
}

void gpib_talk(uint8_t c)
{
                            // Enable talk on data bus
    LATB = 0xFF;
    LATDbits.LD4 = 1;       // TE Talk enable
    TRISB = 0x00;           // Switch data port to output
    
                            // Enable DAV as tx; NDAC, NRFD as rx
    LATAbits.LA2 = 1;       // DAV high
    TRISAbits.RA4 = 1;      // NDAC as input
    TRISAbits.RA3 = 1;      // NRFD as input
    //TRISA |= 0x18;          // NDAC, NRFD as input
    LATDbits.LD7 = 1;       // TE2 Talk enable
    TRISAbits.RA2 = 0;      // DAV as output
    
    if(c) {                 // - If sending command then ATN must be tx
                            // Enable ATN, EOI as tx, SRQ as rx
        LATAbits.LA5 = 1;   // ATN high
        LATAbits.LA1 = 1;   // EOI high
        //LATA |= 0x22;       // ATN, EOI high
        TRISEbits.RE0 = 1;  // SRQ as input
        LATDbits.LD5 = 0;   // DC Direction control
        TRISAbits.RA5 = 0;  // ATN as output
        TRISAbits.RA1 = 0;  // EOI as output
        //TRISA &= 0xDD;      // ATN, EOI as outputs
    } else {                // - When sending data allow other devices to pull ATN low
        TRISAbits.RA1 = 1;  // Set EOI to low, but leave as input
        LATAbits.LA1 = 0;   //   because the direction is controlled by ATN
                            // Enable SRQ as tx, ATN as rx
        LATEbits.LE0 = 1;   // SRQ high
        TRISAbits.RA5 = 1;  // ATN as input
        LATDbits.LD5 = 1;   // DC Direction control
        TRISEbits.RE0 = 0;  // SRQ as output
    }
    
}

void gpib_listen(void)
{
    TRISB = 0xFF;           // Switch data port to input
    LATDbits.LD4 = 0;       // TE Talk enable
    
                            // Enable NDAC, NRFD as tx; DAV as rx
    LATAbits.LA4 = 1;       // NDAC high
    LATAbits.LA3 = 1;       // NRFD high
    TRISAbits.RA2 = 1;      // DAV as input
    LATDbits.LD7 = 0;       // TE2 Talk enable
    TRISAbits.RA4 = 0;      // NDAC as output
    TRISAbits.RA3 = 0;      // NRFD as output
    
                            // Enable ATN as tx, SRQ, EOI as rx
    LATAbits.LA5 = 1;       // ATN high
    TRISEbits.RE0 = 1;      // SRQ as input
    TRISAbits.RA1 = 1;      // EOI as input
    LATDbits.LD5 = 0;       // DC Direction control
    TRISAbits.RA5 = 0;      // ATN as output
}

void gpib_tx(uint8_t const *b, uint8_t l, uint8_t c)
{
    if(!l) l = strlen((char *)b);
    if(!l) return;
    
    gpib_talk(c);
    
    LATDbits.LD0 = 0;           // Blue LED on
    LATDbits.LD3 = 1;           // Enable pullup drivers
    if(c) LATAbits.LA5 = 0;     // Assert ATN
    TMR0H = timeout.talk_timeout >> 8;
    TMR0L = 0;
    INTCONbits.TMR0IF = 0;
    do {
        TMR0L = timeout.talk_timeout;
        if(l == 1 && !c && config.eoi) // Assert EOI for last byte
            TRISAbits.RA1 = 0;  //  if not command
        LATB = *b++ ^ 0xFFU;    // Put data on GPIB bus
        if(!PORTAbits.RA3) {// Wait for NRFD to go high
            LATDbits.LD2 = 0; // Red LED on
            do {
                if(INTCONbits.TMR0IF)
                    break;
            } while(!PORTAbits.RA3);
            LATDbits.LD2 = 1;// Red LED off
        }
        LATAbits.LA2 = 0;       // Assert DAV
        if(!PORTAbits.RA4) {  // Wait for NDAC to go high
            LATDbits.LD2 = 0;
            do {
                if(INTCONbits.TMR0IF)
                    break;
            } while(!PORTAbits.RA4);
            LATDbits.LD2 = 1;
        }
        LATAbits.LA2 = 1;       // Deassert DAV
        if(PORTAbits.RA4) {   // Wait for NDAC to go low
            LATDbits.LD2 = 0;
            do {
                if(INTCONbits.TMR0IF)
                    break;
            } while(PORTAbits.RA4);
            LATDbits.LD2 = 1;
        }
    } while(--l);

    LATB = 0xFF;
    LATDbits.LD3 = 0;           // Disable pullup drivers
    TRISAbits.RA1 = 1;          // Deassert EOI
    if(c) LATAbits.LA5 = 1;     // Deassert ATN
    LATDbits.LD0 = 1;           // Blue LED off
    
    gpib_listen();
}

void gpib_rx(void)
{
    uint8_t b;
    uint8_t eoi;
    
    LATDbits.LD0 = 0;
    LATAbits.LA4 = 0;           // Assert NDAC
    TMR0H = timeout.listen_timeout >> 8;
    TMR0L = 0;
    INTCONbits.TMR0IF = 0;
    do {
        LATAbits.LA3 = 1;       // Deassert NRFD
        TMR0L = timeout.listen_timeout;
        if(PORTAbits.RA2) {   // Wait for DAV
            LATDbits.LD2 = 0;
            do {
                if(INTCONbits.TMR0IF) {
                    LATAbits.LA4 = 1;
                    break;
                }
            } while(PORTAbits.RA2);
            LATDbits.LD2 = 1;
        }
        if(INTCONbits.TMR0IF) break;
        LATAbits.LA3 = 0;       // Assert NRFD
        b = PORTB ^ 0xFFU;      // Read data
        eoi = PORTA;            // Read EOI
        LATAbits.LA4 = 1;       // Deassert NDAC
        while(!PIR1bits.TXIF);  // Wait for tx reg empty
        TXREG1 = b;             // Tx on serial
        if(!PORTAbits.RA2) {  // Wait for DAV
            LATDbits.LD2 = 0;
            do {
                if(INTCONbits.TMR0IF) {
                    eoi = 0;
                    break;
                }
            } while(!PORTAbits.RA2);
            LATDbits.LD2 = 1;
        }
        LATAbits.LA4 = 0;       // Assert NDAC
    } while(eoi & 2);
    if(config.eot_enable) {
        while(!PIR1bits.TXIF);  // Wait for tx reg empty
        TXREG1 = config.eot_char;
    }
}

void gpib_rx1(void)
{
    uint8_t b;
    uint8_t eoi;
    
    LATAbits.LA4 = 0;           // Assert NDAC
    
    LATAbits.LA3 = 1;       // Deassert NRFD
    while(PORTAbits.RA2);   // Wait for DAV
    LATAbits.LA3 = 0;       // Assert NRFD
    b = PORTB ^ 0xFFU;      // Read data
    eoi = PORTA;            // Read EOI
    //while(!PIR1bits.TXIF);  // Wait for tx reg empty
    //TXREG1 = b;             // Tx on serial
    LATAbits.LA4 = 1;       // Deassert NDAC
    while(!PORTAbits.RA2);  // Wait for DAV
    LATAbits.LA4 = 0;       // Assert NDAC
    
    print("spoll "); print_uint(b); print_nl();
    print("eoi "); print((eoi & 2) ? "0" : "1"); print_nl();
}

void gpib_rx2(void)
{
    uint8_t b;
    uint8_t eoi;
    uint16_t n = 0;
    
    LATDbits.LD0 = 0;
    LATAbits.LA4 = 0;           // Assert NDAC
    TMR0H = timeout.listen_timeout >> 8;
    TMR0L = 0;
    INTCONbits.TMR0IF = 0;
    do {
        LATAbits.LA3 = 1;       // Deassert NRFD
        TMR0L = timeout.listen_timeout;
        if(PORTAbits.RA2) {   // Wait for DAV
            LATDbits.LD2 = 0;
            do {
                if(INTCONbits.TMR0IF) {
                    LATAbits.LA4 = 1;
                    break;
                }
            } while(PORTAbits.RA2);
            LATDbits.LD2 = 1;
        }
        if(INTCONbits.TMR0IF) break;
        LATAbits.LA3 = 0;       // Assert NRFD
        b = PORTB ^ 0xFFU;      // Read data
        eoi = PORTA;            // Read EOI
        LATAbits.LA4 = 1;       // Deassert NDAC
        if(++n > 5) {
            while(!PIR1bits.TXIF);  // Wait for tx reg empty
            TXREG1 = b;             // Tx on serial
        }
        if(!PORTAbits.RA2) {  // Wait for DAV
            LATDbits.LD2 = 0;
            do {
                if(INTCONbits.TMR0IF) {
                    eoi = 0;
                    break;
                }
            } while(!PORTAbits.RA2);
            LATDbits.LD2 = 1;
        }
        LATAbits.LA4 = 0;       // Assert NDAC
    } while(eoi & 2);
}

typedef struct {
    char const *s;
    uint8_t n;
} TOPTION;

TOPTION option_led[] = {
    "off",     0,
    "0",       0,
    "on",      1,
    "1",       1,
    "toggle",  2,
    0,         0
};

TOPTION option_on_off[] = {
    "off",     0,
    "0",       0,
    "on",      1,
    "1",       1,
    0,         0
};

TOPTION option_on_off_default[] = {
    "off",     0,
    "0",       0,
    "on",      1,
    "1",       1,
    "default", 2,
    0,         0
};

uint8_t option(char *s, TOPTION *o)
{
    while(o->s) {
        if(!strcmp((char *)s, o->s)) break;
        ++o;
    }
    return o->n;
}

uint8_t cmd_unsupported(char **args)
{
    return 1;
}

uint8_t cmd_led(char const *s, uint8_t m, char **args)
{
    if(args[0]) {
        switch(option(args[0], option_led)) {
            case 0: LATD |= m;         break;
            case 1: LATD &= m ^ 0xFFU; break;
            case 2: LATD ^= m;         break;
        }
    } else {
        print(s);
        print(" LED is ");
        print((LATD & m) ? "off" : "on");
        print_nl();
    }
    return 0;
}

uint8_t cmd_red(char **args)
{
    return cmd_led("red", 1 << 2, args);
}

uint8_t cmd_green(char **args)
{
    return cmd_led("green", 1 << 1, args);
}

uint8_t cmd_blue(char **args)
{
    return cmd_led("blue", 1 << 0, args);
}

uint8_t cmd_debug(char **args)
{
    if(args[0]) {
        config.debug = atoi(args[0]);
    } else {
        print_uint(config.debug);
        print_nl();
    }
    return 0;
}

uint8_t cmd_bps(char **args)
{
    if(args[0]) {
        unsigned long b = atol(args[0]);
        config.brg = (3000000ul + (b >> 1)) / b;
        update_brg();
    } else {
        unsigned long b = 3000000ul / config.brg;
        print_ulong(b);
        print_nl();
    }
    return 0;
}

uint8_t cmd_listen_timeout(char **args)
{
    if(args[0]) {
        config.listen_timeout = (uint16_t)atoi(args[0]);
        update_timers();
    } else {
        print_uint(config.listen_timeout);
        print_nl();
    }
    return 0;
}

uint8_t cmd_talk_timeout(char **args)
{
    if(args[0]) {
        config.talk_timeout = (uint16_t)atoi(args[0]);
        update_timers();
    } else {
        print_uint(config.talk_timeout);
        print_nl();
    }
    return 0;
}

uint8_t cmd_spoll_timeout(char **args)
{
    if(args[0]) {
        config.spoll_timeout = (uint16_t)atoi(args[0]);
        update_timers();
    } else {
        print_uint(config.spoll_timeout);
        print_nl();
    }
    return 0;
}

uint8_t cmd_read(char **args); /// hack

uint8_t cmd_write_hex(char **args)
{
    uint8_t txb[32];
    uint8_t *pb = txb;
    char *s;
    while((s = *args++)) {
        uint8_t b = 0;
        char c;
        while((c = *s++)) {
            b <<= 4;
            b |= atoh(c);
        }
        *pb++ = b;
    }
    if(pb != txb) {
        static uint8_t lsn_addr[] = { UNT, UNL, LAD };
        lsn_addr[2] = LAD + config.addr;
        gpib_tx(lsn_addr, sizeof(lsn_addr), 1);
        gpib_tx(txb, pb - txb, 0);
        if(config.auto_read == 1) cmd_read(0);
    }
    return 0;
}

uint8_t cmd_echo(char **args)
{
    if(args[0]) {
        config.echo = option(args[0], option_on_off_default);
        //if(config.echo == 2) config.echo = config_default.echo;
    } else {
        print_uint(config.echo);
        print_nl();
    }
    return 0;
}

uint8_t cmd_addr(char **args)
{
    if(args[0]) {
        config.addr = (uint8_t)atoi(args[0]);
    } else {
        print_uint(config.addr);
        print_nl();
    }
    return 0;
}

uint8_t cmd_auto(char **args)
{
    if(args[0]) {
        config.auto_read = (uint8_t)atoi(args[0]);
    } else {
        print_uint(config.auto_read);
        print_nl();
    }
    return 0;
}

uint8_t cmd_clr(char **args)
{
    static uint8_t cmd[] = { UNT, UNL, LAD + 0, TAD + 1, SDC };
    cmd[3] = TAD + config.addr;
    gpib_tx(cmd, sizeof(cmd), 1);
    return 0;
}

uint8_t cmd_eoi(char **args)
{
    if(args[0]) {
        config.eoi = (uint8_t)atoi(args[0]);
    } else {
        print_uint(config.eoi);
        print_nl();
    }
    return 0;
}

uint8_t cmd_eos(char **args)
{
    if(args[0]) {
        config.eos = (uint8_t)atoi(args[0]);
    } else {
        print_uint(config.eos);
        print_nl();
    }
    return 0;
}

uint8_t cmd_eot_enable(char **args)
{
    if(args[0]) {
        config.eot_enable = option(args[0], option_on_off);
    } else {
        print_uint(config.eot_enable);
        print_nl();
    }
    return 0;
}

uint8_t cmd_eot_char(char **args)
{
    if(args[0]) {
        config.eot_char = (uint8_t)atoi(args[0]);
    } else {
        print_uint(config.eot_char);
        print_nl();
    }
    return 0;
}

uint8_t cmd_ifc(char **args)
{
    LATEbits.LE1 = 0;   // Assert IFC
    __delay_us(150);    // 150 us
    LATEbits.LE1 = 1;   // Deassert IFC
    return 0;
}

uint8_t cmd_llo(char **args)
{
    static uint8_t cmd[] = { UNT, UNL, LAD + 0, TAD + 1, LLO };
    cmd[3] = TAD + config.addr;
    gpib_tx(cmd, sizeof(cmd), 1);
    return 0;
}

uint8_t cmd_loc(char **args)
{
    static uint8_t cmd[] = { UNT, UNL, LAD + 0, TAD + 1, GTL };
    cmd[3] = TAD + config.addr;
    gpib_tx(cmd, sizeof(cmd), 1);
    return 0;
}

uint8_t cmd_read(char **args)
{
    static uint8_t talk_addr[] = { UNT, UNL, TAD };
    talk_addr[2] = TAD + config.addr;
    gpib_tx(talk_addr, sizeof(talk_addr), 1);
    gpib_rx();
    return 0;
}

uint8_t cmd_reset(char **args)
{
    __asm("reset");
    return 0;
}

uint8_t cmd_spoll(char **args)
{
    static uint8_t spe[] = { SPE, TAD };
    static uint8_t spd[] = { SPD };
    
    spe[1] = TAD + config.addr;
    gpib_tx(spe, sizeof(spe), 1);
    gpib_rx1();
    gpib_tx(spd, sizeof(spd), 1);
    return 0;
}

uint8_t cmd_srq(char **args)
{
    print(PORTEbits.RE0 ? "0" : "1");
    print_nl();
    return 0;
}

uint8_t cmd_status(char **args)
{
    if(args[0]) {
        config.status = (uint8_t)atoi(args[0]);
    } else {
        print_uint(config.status);
        print_nl();
    }
    return 0;
}

uint8_t cmd_trg(char **args)
{
    static uint8_t cmd[] = { UNL, LAD, LAD, LAD, LAD, LAD, LAD, LAD, LAD, LAD, LAD, LAD, LAD, LAD, LAD, LAD, GET };
    
    if(args[0]) {
        /// todo: parse GPIB addresses - up to 15
    } else {
        cmd[1] = LAD + config.addr;
        cmd[2] = GET;
        gpib_tx(cmd, 3, 1);
    }
    return 0;
}

uint8_t cmd_ver(char **args)
{
    print("0");
    print_nl();
    return 0;
}

uint8_t cmd_help(char **args)
{
    print("There is no help");
    print_nl();
    return 0;
}

uint8_t cmd_tek_read_mem(char **args)
{
    if(args[0] == 0 || args[1] == 0) return 1;
    uint32_t a = htou32(args[0]);
    uint32_t l = htou32(args[1]);
    static uint8_t const lsn_addr[] = { UNT, UNL, LAD + 29 };
    static uint8_t const talk_addr[] = { UNT, UNL, TAD + 29 };
    gpib_tx(lsn_addr, sizeof(lsn_addr), 1);
    gpib_tx((uint8_t *)"PASSWORD PITBULL", 16, 0);
    uint8_t rm[12] = { 'm', 0, 0, 8, 0, 0, 0, 0, 0, 0, 4, 0 };
    while(l) {
        rm[4] = a >> 24;
        rm[5] = a >> 16;
        rm[6] = a >> 8;
        rm[7] = a;
        if(l >= 0x0400) {
            l -= 0x0400;
        } else {
            rm[10] = l >> 8;
            rm[11] = l;
            l = 0;
        }
        rm[1] = 'm' + 8 + rm[4] + rm[5] + rm[6] + rm[7] + rm[10] + rm[11];
        gpib_tx(lsn_addr, sizeof(lsn_addr), 1);
        gpib_tx(rm, sizeof(rm), 0);
        gpib_tx(talk_addr, sizeof(talk_addr), 1);
        gpib_rx2();
        gpib_tx(lsn_addr, sizeof(lsn_addr), 1);
        gpib_tx((uint8_t*)"+", 1, 0);
    }
    return 0;
}

typedef struct {
    char const * name;
    uint8_t (*function)(char **args);
    char const *help;
} CMDS;

CMDS commands[] = {
    // Commands
    "red",          cmd_red,            0,
    "blue",         cmd_blue,           0,
    "green",        cmd_green,          0,
    "debug",        cmd_debug,          0,
    "bps",          cmd_bps,            0,
    "baud",         cmd_bps,            0,
    "echo",         cmd_echo,           0,
    "listen_tmo",   cmd_listen_timeout, 0,
    "talk_tmo",     cmd_talk_timeout,   0,
    "spoll_tmo",    cmd_spoll_timeout,  0,
    "write_hex",    cmd_write_hex,      0,
    "tek_read_mem", cmd_tek_read_mem,   0,
    // Prologix commands
    "addr",         cmd_addr,           0,
    "auto",         cmd_auto,           0,
    "clr",          cmd_clr,            0,
    "eoi",          cmd_eoi,            0,
    "eos",          cmd_eos,            0,
    "eot_enable",   cmd_eot_enable,     0,
    "eot_char",     cmd_eot_char,       0,
    "ifc",          cmd_ifc,            0,
    "llo",          cmd_llo,            0,
    "loc",          cmd_loc,            0,
    "lon",          cmd_unsupported,    0,
    "mode",         cmd_unsupported,    0,
    "read",         cmd_read,           0,
    "read_tmo_ms",  cmd_listen_timeout, 0,
    "rst",          cmd_reset,          0,
    "savecfg",      cmd_unsupported,    0,
    "spoll",        cmd_spoll,          0,
    "srq",          cmd_srq,            0,
    "status",       cmd_status,         0,
    "trg",          cmd_trg,            0,  /// todo: support for multiple addresses
    "ver",          cmd_ver,            0,
    "help",         cmd_help,           0,
    // End of commands
    0,              0,                  0
};

void main(void) {
    ANSELA = 0x00;
    LATA   = 0x3F;
    TRISA  = 0x3F;
    
    ANSELB = 0x00;
    LATB   = 0xFF;
    TRISB  = 0xFF;
    
    ANSELC = 0x00;
    LATC   = 0x40;
    TRISC  = 0xB0;
    
    ANSELD = 0x00;
    LATD   = 0x00;
    TRISD  = 0x00;
    
    ANSELE = 0x00;
    LATE   = 0x03;
    TRISE  = 0x03;

    update_brg();
    BAUDCON1 = 0;
    BAUDCON1bits.BRG16 = 1;
    TXSTA1 = 0;
    TXSTA1bits.TXEN = 1;
    RCSTA1 = 0;
    RCSTA1bits.CREN = 1;
    RCSTA1bits.SPEN = 1;

    update_timers();
    INTCONbits.INT0IE = 0;
    T0CON = 0;
    T0CONbits.T0PS = 7;
    T0CONbits.TMR0ON = 1;

    LATDbits.LD3 = 0;   // PE Pullup enable
    LATDbits.LD4 = 0;   // TE Talk enable
    LATDbits.LD5 = 0;   // DC Direction control
    LATDbits.LD6 = 0;   // SC System control
    LATDbits.LD7 = 0;   // TE2 Talk enable
   
    OSCCONbits.IRCF = 7;  // 16 MHz
    
    LATDbits.LD0 = 1;  // Blue LED off
    LATDbits.LD1 = 0;  // Green LED on
    LATDbits.LD2 = 1;  // Red LED off
    
    print("Running\r\n");
 
    gpib_system(1);     // Be the system controller

    //cmd_ifc(0);
    
    LATAbits.LA0 = 0;   // Assert REN
    
    gpib_listen();
    
    uint8_t dcl[] = { DCL };
    //gpib_tx(dcl, sizeof(DCL), 1);
   
    for(;;) {
        char c, *cp, rxbuf[256];

        cp = rxbuf;
        do {
            /// todo: check for and clear UART rx errors
            while(!PIR1bits.RCIF);
            c = RCREG1;
            if(config.echo) TXREG1 = c;  // echo
            *cp++ = c;
        } while(c != '\r' && c != '\n');
        --cp;
        if(config.echo) print((c == '\r') ? "\n" : "\r");

        char *pp = rxbuf, plus = 0;
        while(*pp == '+') ++pp, ++plus;
        
        if(plus) {
            *cp = 0;
            char *ap = pp;
            char *args[32];
            char **a = args;
            while((c = *ap)) {
                if(c == ' ') {
                    *ap++ = 0;
                    while(*ap == ' ') ++ap;
                    *a++ = ap;
                } else {
                    ++ap;
                }
            }
            *a = 0;
            if(config.debug & 1) print_args(args);

            CMDS *cmd = commands;
            do {
                if(!strcmp(cmd->name, (char *)pp)) {
                    cmd->function(args);
                    break;
                }
                ++cmd;
            } while(cmd->name);
        } else {
            switch(config.eos) {
                case 0: *cp++ = '\r'; *cp++ = '\n'; break;
                case 1: *cp++ = '\r'; break;
                case 2: *cp++ = '\n'; break;
            }
            static uint8_t lsn_addr[] = { UNT, UNL, LAD };
            lsn_addr[2] = LAD + config.addr;
            gpib_tx(lsn_addr, sizeof(lsn_addr), 1);
            gpib_tx((uint8_t *)rxbuf, cp - rxbuf, 0);
        
            if(config.auto_read == 1) {
                cmd_read(0);
            } else if(config.auto_read == 2) {
                cp = rxbuf;
                while(*cp > 32) ++cp;
                if(*--cp == '?') cmd_read(0);
            }
        }
    }
    
    return;
}
