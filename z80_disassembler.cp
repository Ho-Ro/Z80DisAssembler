/***
 *  Z80 Disassembler
 *
I created this small disassembler for a Z80 cpu at one afternoon. It is a commandline tool. The size of the ROM and entry points have to be coded directly in the sourcecode.

Every ANSI C++ compiler should compile this program. It only uses some ANSI C functions (look into ''main()'') for loading a file called "EPROM".

The program has two parts:

Analyze the code. The disassembler tries to analyze what part of the binary data is program code and what part is data. It start with all hardware vectors of the Z80 (''RST'' opcodes, NMI) and parses all jumps via a recursive analyze via ''ParseOpcode()''. Every opcode is marked in an array (''OpcodesFlags''). There are some exceptions, the parser can't recognize:
self modifying code. A ROM shouldn't contain such code.
calculated branches with ''JP (IY)'', ''JP (IX)'' or ''JP (HL)''. The parser can't recognize them, either.
Jumptables. These are quite common in a ROM. Only solution: disassemble the program and look into the code. If you found a jumptable - like on my Futura aquarium computer - insert some more calls of ''ParseOpcodes()''.
Unused code. Code that is never called by anybody, could not be found. Make sure that the code is not called via a jump table!
Disassembly of the code. With the help of the OpcodesFlags table the disassembler now creates the output. This subroutine is quite long. It disassembles one opcode at a specific address in ROM into a buffer. It is coded directly from a list of Z80 opcodes, so the handling of ''IX'' and ''IY'' could be optimized quite a lot.
The subroutine ''OpcodeLen()'' returns the size of one opcode in bytes. It is called while parsing and while disassembling.

The disassembler recognizes no hidden opcodes (the assembler does!). I didn't had a table for them while writing the disassembler and they were not needed anyway.

If a routine wanted an "address" to the Z80 code, it is in fact an offset to the array of code. No pointers! Longs are not necessary for a Z80, because the standard Z80 only supports 64k.

This program is freeware. It is not allowed to be used as a base for a commercial product!
 ***/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

#define CODESIZE           8192 // 8K program code
#define FUTURA_189         1    // jump table for the Futura Aquariencomputer ROM V1.89 https://github.com/sarnau/FuturaAquariumComputer
#define DETECT_JUMPTABLES  0    // if 1, then break if a jump table is detected

// memory for the code
static uint8_t Opcodes[0x10000];

// Flag per memory cell: opcode, operand or data
// bit 4 = 1, a JR or similar jumps to this address
enum {
    Opcode,
    Operand,
    Data
};

uint8_t OpcodesFlags[sizeof(Opcodes)];

// calculate the length of an opcode
static int OpcodeLen(uint16_t p)
{
int len = 1;

    switch(Opcodes[p]) {// Opcode
    case 0x06:          // LD B,n
    case 0x0E:          // LD C,n
    case 0x10:          // DJNZ e
    case 0x16:          // LD D,n
    case 0x18:          // JR e
    case 0x1E:          // LD E,n
    case 0x20:          // JR NZ,e
    case 0x26:          // LD H,n
    case 0x28:          // JR Z,e
    case 0x2E:          // LD L,n
    case 0x30:          // JR NC,e
    case 0x36:          // LD (HL),n
    case 0x38:          // JR C,e
    case 0x3E:          // LD A,n
    case 0xC6:          // ADD A,n
    case 0xCE:          // ADC A,n
    case 0xD3:          // OUT (n),A
    case 0xD6:          // SUB n
    case 0xDB:          // IN A,(n)
    case 0xDE:          // SBC A,n
    case 0xE6:          // AND n
    case 0xEE:          // XOR n
    case 0xF6:          // OR n
    case 0xFE:          // CP n

    case 0xCB:          // shift-,rotate-,bit-opcodes
                len = 2;
                break;
    case 0x01:          // LD BC,nn'
    case 0x11:          // LD DE,nn'
    case 0x21:          // LD HL,nn'
    case 0x22:          // LD (nn'),HL
    case 0x2A:          // LD HL,(nn')
    case 0x31:          // LD SP,(nn')
    case 0x32:          // LD (nn'),A
    case 0x3A:          // LD A,(nn')
    case 0xC2:          // JP NZ,nn'
    case 0xC3:          // JP nn'
    case 0xC4:          // CALL NZ,nn'
    case 0xCA:          // JP Z,nn'
    case 0xCC:          // CALL Z,nn'
    case 0xCD:          // CALL nn'
    case 0xD2:          // JP NC,nn'
    case 0xD4:          // CALL NC,nn'
    case 0xDA:          // JP C,nn'
    case 0xDC:          // CALL C,nn'
    case 0xE2:          // JP PO,nn'
    case 0xE4:          // CALL PO,nn'
    case 0xEA:          // JP PE,nn'
    case 0xEC:          // CALL PE,nn'
    case 0xF2:          // JP P,nn'
    case 0xF4:          // CALL P,nn'
    case 0xFA:          // JP M,nn'
    case 0xFC:          // CALL M,nn'
                len = 3;
                break;
    case 0xDD:  len = 2;
                switch(Opcodes[p+1]) {// 2nd part of the opcode
                case 0x34:          // INC (IX+d)
                case 0x35:          // DEC (IX+d)
                case 0x46:          // LD B,(IX+d)
                case 0x4E:          // LD C,(IX+d)
                case 0x56:          // LD D,(IX+d)
                case 0x5E:          // LD E,(IX+d)
                case 0x66:          // LD H,(IX+d)
                case 0x6E:          // LD L,(IX+d)
                case 0x70:          // LD (IX+d),B
                case 0x71:          // LD (IX+d),C
                case 0x72:          // LD (IX+d),D
                case 0x73:          // LD (IX+d),E
                case 0x74:          // LD (IX+d),H
                case 0x75:          // LD (IX+d),L
                case 0x77:          // LD (IX+d),A
                case 0x7E:          // LD A,(IX+d)
                case 0x86:          // ADD A,(IX+d)
                case 0x8E:          // ADC A,(IX+d)
                case 0x96:          // SUB A,(IX+d)
                case 0x9E:          // SBC A,(IX+d)
                case 0xA6:          // AND (IX+d)
                case 0xAE:          // XOR (IX+d)
                case 0xB6:          // OR (IX+d)
                case 0xBE:          // CP (IX+d)
                            len = 3;
                            break;
                case 0x21:          // LD IX,nn'
                case 0x22:          // LD (nn'),IX
                case 0x2A:          // LD IX,(nn')
                case 0x36:          // LD (IX+d),n
                case 0xCB:          // Rotation (IX+d)
                            len = 4;
                            break;
                }
                break;
    case 0xED:  len = 2;
                switch(Opcodes[p+1]) {// 2nd part of the opcode
                case 0x43:          // LD (nn'),BC
                case 0x4B:          // LD BC,(nn')
                case 0x53:          // LD (nn'),DE
                case 0x5B:          // LD DE,(nn')
                case 0x73:          // LD (nn'),SP
                case 0x7B:          // LD SP,(nn')
                            len = 4;
                            break;
                }
                break;
    case 0xFD:  len = 2;
                switch(Opcodes[p+1]) {// 2nd part of the opcode
                case 0x34:          // INC (IY+d)
                case 0x35:          // DEC (IY+d)
                case 0x46:          // LD B,(IY+d)
                case 0x4E:          // LD C,(IY+d)
                case 0x56:          // LD D,(IY+d)
                case 0x5E:          // LD E,(IY+d)
                case 0x66:          // LD H,(IY+d)
                case 0x6E:          // LD L,(IY+d)
                case 0x70:          // LD (IY+d),B
                case 0x71:          // LD (IY+d),C
                case 0x72:          // LD (IY+d),D
                case 0x73:          // LD (IY+d),E
                case 0x74:          // LD (IY+d),H
                case 0x75:          // LD (IY+d),L
                case 0x77:          // LD (IY+d),A
                case 0x7E:          // LD A,(IY+d)
                case 0x86:          // ADD A,(IY+d)
                case 0x8E:          // ADC A,(IY+d)
                case 0x96:          // SUB A,(IY+d)
                case 0x9E:          // SBC A,(IY+d)
                case 0xA6:          // AND (IY+d)
                case 0xAE:          // XOR (IY+d)
                case 0xB6:          // OR (IY+d)
                case 0xBE:          // CP (IY+d)
                            len = 3;
                            break;
                case 0x21:          // LD IY,nn'
                case 0x22:          // LD (nn'),IY
                case 0x2A:          // LD IY,(nn')
                case 0x36:          // LD (IY+d),n
                case 0xCB:          // Rotation,Bitop (IY+d)
                            len = 4;
                            break;
                }
                break;
    }
    return len;
}

static void        ParseOpcodes(uint16_t adr)
{
int16_t  i;
uint32_t next;
bool     label = true;

    do {
        if(label)                       // set a label?
            OpcodesFlags[adr] |= 0x10;  // mark the memory cell as a jump destination
        if((OpcodesFlags[adr] & 0x0F) == Opcode) break; // loop detected
        if((OpcodesFlags[adr] & 0x0F) == Operand) {
            printf("Illegal jump at addr %4.4XH\n", adr);
            return;
        }
        int len = OpcodeLen(adr);
        for(i=0;i<len;i++)
            OpcodesFlags[adr+i] = Operand;  // transfer the opcode
        OpcodesFlags[adr] = Opcode;     // mark the beginning of the opcode
        if(label) {                     // define a label?
            OpcodesFlags[adr] |= 0x10;  // yes
            label = false;              // reset fkag
        }

        next = adr + len;               // ptr to the next opcode
        switch(Opcodes[adr]) {          // get that opcode
        case 0xCA:      // JP c,????
        case 0xC2:
        case 0xDA:
        case 0xD2:
        case 0xEA:
        case 0xE2:
        case 0xFA:
        case 0xF2:
                ParseOpcodes((Opcodes[adr+2]<<8) + Opcodes[adr+1]);
                break;
        case 0x28:      // JR c,??
        case 0x20:
        case 0x38:
        case 0x30:
                ParseOpcodes(adr + 2 + (uint8_t)Opcodes[adr+1]);
                break;
        case 0xCC:      // CALL c,????
        case 0xC4:
        case 0xDC:
        case 0xD4:
        case 0xEC:
        case 0xE4:
        case 0xFC:
        case 0xF4:
                ParseOpcodes((Opcodes[adr+2]<<8) + Opcodes[adr+1]);
                break;
        case 0xC8:      // RET c
        case 0xC0:
        case 0xD8:
        case 0xD0:
        case 0xE8:
        case 0xE0:
        case 0xF8:
        case 0xF0:
                break;
        case 0xC7:      // RST 0
        case 0xCF:      // RST 8
        case 0xD7:      // RST 10
        case 0xDF:      // RST 18
        case 0xE7:      // RST 20
        case 0xEF:      // RST 28
        case 0xF7:      // RST 30
        case 0xFF:      // RST 38
                ParseOpcodes(Opcodes[adr] & 0x38);
                break;
        case 0x10:      // DJNZ ??
                ParseOpcodes(adr + 2 + (uint8_t)Opcodes[adr+1]);
                break;
        case 0xC3:      // JP ????
                next = (Opcodes[adr+2]<<8) + Opcodes[adr+1];
                label = true;
                break;
        case 0x18:      // JR ??
                next = adr + 2 + (uint8_t)Opcodes[adr+1];
                label = true;
                break;
        case 0xCD:      // CALL ????
                ParseOpcodes((Opcodes[adr+2]<<8) + Opcodes[adr+1]);
                break;
        case 0xC9:      // RET
                return;
        case 0xE9:
#if DETECT_JUMPTABLES
                puts("JP (HL) found"); // JP (HL)
                exit(-1);
#endif
                break;
        case 0xDD:
#if DETECT_JUMPTABLES
                if(Opcodes[adr+1] == 0xE9) {    // JP (IX)
                    puts("JP (IX) found");
	                exit(-1);
                }
#endif
                break;
        case 0xFD:
#if DETECT_JUMPTABLES
                if(Opcodes[adr+1] == 0xE9) {    // JP (IY)
                    puts("JP (IY) found");
	                exit(-1);
                }
#endif
                break;
        case 0xED:
                if(Opcodes[adr+1] == 0x4D) {    // RTI
                    return;
                } else if(Opcodes[adr+1] == 0x45) { // RETN
                    return;
                }
                break;
        }
        adr = next;
    } while(1);
}


static void GenerateOutput(char *buf, size_t bufsize, const char *Format, ...)
{
	va_list AP;

	va_start(AP, Format);
	vsnprintf(buf, bufsize, Format, AP);
	va_end(AP);
}

#define G(...) GenerateOutput(s,ssize,__VA_ARGS__)

// Disassemble
void        Disassemble(uint16_t adr,char *s,size_t ssize)
{
uint8_t         a = Opcodes[adr];
uint8_t         d = (a >> 3) & 7;
uint8_t         e = a & 7;
static const char *reg[8] = {"B","C","D","E","H","L","(HL)","A"};
static const char *dreg[4] = {"BC","DE","HL","SP"};
static const char *cond[8] = {"NZ","Z","NC","C","PO","PE","P","M"};
static const char *arith[8] = {"ADD     A,", "ADC     A, ", "SUB     ", "SBC     A,",
                               "AND     ",   "XOR     ",    "OR      ", "CP      "};
const char       *ireg;        // temp. index register string

    switch(a & 0xC0) {
    case 0x00:
        switch(e) {
        case 0x00:
            switch(d) {
            case 0x00:
            	G("NOP");
                break;
            case 0x01:
            	G("EX      AF,AF'");
                break;
            case 0x02:
            	G("DJNZ    %4.4Xh",adr+2+(uint8_t)Opcodes[adr+1]);
                break;
            case 0x03:
            	G("JR      %4.4Xh",adr+2+(uint8_t)Opcodes[adr+1]);
                break;
            default:
            	G("JR      %s,%4.4Xh",cond[d & 3],adr+2+(uint8_t)Opcodes[adr+1]);
                break;
            }
            break;
        case 0x01:
            if(a & 0x08) {
            	G("ADD     HL,%s",dreg[d >> 1]);
            } else {
            	G("LD      %s,%4.4Xh",dreg[d >> 1],Opcodes[adr+1]+(Opcodes[adr+2]<<8));
            }
            break;
        case 0x02:
            switch(d) {
            case 0x00:
            	G("LD      (BC),A");
                break;
            case 0x01:
            	G("LD      A,(BC)");
                break;
            case 0x02:
            	G("LD      (DE),A");
                break;
            case 0x03:
                G("LD      A,(DE)");
                break;
            case 0x04:
                G("LD      (%4.4Xh),HL",Opcodes[adr+1]+(Opcodes[adr+2]<<8));
                break;
            case 0x05:
                G("LD      HL,(%4.4Xh)",Opcodes[adr+1]+(Opcodes[adr+2]<<8));
                break;
            case 0x06:
                G("LD      (%4.4Xh),A",Opcodes[adr+1]+(Opcodes[adr+2]<<8));
                break;
            case 0x07:
                G("LD      A,(%4.4Xh)",Opcodes[adr+1]+(Opcodes[adr+2]<<8));
                break;
            }
            break;
        case 0x03:
            if(a & 0x08)
            	G("DEC     %s",dreg[d >> 1]);
            else
            	G("INC     %s",dreg[d >> 1]);
            break;
        case 0x04:
          	G("INC     %s",reg[d]);
            break;
        case 0x05:
          	G("DEC     %s",reg[d]);
            break;
        case 0x06:              // LD   d,n
          	G("LD      %s,%2.2Xh",reg[d],Opcodes[adr+1]);
            break;
        case 0x07:
            {
            static const char *str[8] = {"RLCA","RRCA","RLA","RRA","DAA","CPL","SCF","CCF"};
          	G("%s",str[d]);
            }
            break;
        }
        break;
    case 0x40:                          // LD   d,s
        if(a == 0x76) {
          	G("HALT");
        } else {
          	G("LD      %s,%s",reg[d],reg[e]);
        }
        break;
    case 0x80:
     	G("%s%s",arith[d],reg[e]);
        break;
    case 0xC0:
        switch(e) {
        case 0x00:
          	G("RET     %s",cond[d]);
            break;
        case 0x01:
            if(d & 1) {
                switch(d >> 1) {
                case 0x00:
                    G("RET");
                    break;
                case 0x01:
                    G("EXX");
                    break;
                case 0x02:
                    G("JP      (HL)");
                    break;
                case 0x03:
                    G("LD      SP,HL");
                    break;
                }
            } else {
                if((d >> 1)==3)
                    G("POP     AF");
                else
                    G("POP     %s",dreg[d >> 1]);
            }
            break;
        case 0x02:
            G("JP      %s,%4.4Xh",cond[d],Opcodes[adr+1]+(Opcodes[adr+2]<<8));
            break;
        case 0x03:
            switch(d) {
            case 0x00:
                G("JP      %4.4Xh",Opcodes[adr+1]+(Opcodes[adr+2]<<8));
                break;
            case 0x01:                  // 0xCB
                a = Opcodes[++adr];     // get extended opcode
                d = (a >> 3) & 7;
                e = a & 7;
                switch(a & 0xC0) {
                case 0x00:
                    {
                    static const char *str[8] = {"RLC","RRC","RL ","RR ","SLA","SRA","SLL","SRL"};
                    G("%s     %s",str[d],reg[e]);
                    }
                    break;
                case 0x40:
                    G("BIT     %d,%s",d,reg[e]);
                    break;
                case 0x80:
                    G("RES     %d,%s",d,reg[e]);
                    break;
                case 0xC0:
                    G("SET     %d,%s",d,reg[e]);
                    break;
                }
                break;
            case 0x02:
                G("OUT     (%2.2Xh),A",Opcodes[adr+1]);
                break;
            case 0x03:
                G("IN      A,(%2.2Xh)",Opcodes[adr+1]);
                break;
            case 0x04:
                G("EX      (SP),HL");
                break;
            case 0x05:
                G("EX      DE,HL");
                break;
            case 0x06:
                G("DI");
                break;
            case 0x07:
                G("EI");
                break;
            }
            break;
        case 0x04:
            G("CALL    %s,%4.4Xh",cond[d],Opcodes[adr+1]+(Opcodes[adr+2]<<8));
            break;
        case 0x05:
            if(d & 1) {
                switch(d >> 1) {
                case 0x00:
		            G("CALL    %4.4Xh",Opcodes[adr+1]+(Opcodes[adr+2]<<8));
                    break;
                case 0x02:              // 0xED
                    a = Opcodes[++adr]; // get extended opcode
                    d = (a >> 3) & 7;
                    e = a & 7;
                    switch(a & 0xC0) {
                    case 0x40:
                        switch(e) {
                        case 0x00:
                            if (a == 0x70) // undoc: "in (c)" set flags but do not modify a reg
                                G("IN      (C)");
                            else
                                G("IN      %s,(C)",reg[d]);
                            break;
                        case 0x01:
                            if (a == 0x71) // undoc: "out (c), 0" output 0
                                G("OUT     (C),0");
                            else
                                G("OUT     (C),%s",reg[d]);
                            break;
                        case 0x02:
                            if(d & 1)
                                G("ADC     HL,%s",dreg[d >> 1]);
                            else
                                G("SBC     HL,%s",dreg[d >> 1]);
                            break;
                        case 0x03:
                            if(d & 1) {
                                G("LD      %s,(%4.4Xh)",dreg[d >> 1],Opcodes[adr+1]+(Opcodes[adr+2]<<8));
                            } else {
                                G("LD      (%4.4Xh),%s",Opcodes[adr+1]+(Opcodes[adr+2]<<8),dreg[d >> 1]);
                            }
                            break;
                        case 0x04:
                            {
                            static const char *str[8] = {"NEG","???","???","???","???","???","???","???"};
                            G("%s",str[d]);
                            }
                            break;
                        case 0x05:
                            {
                            static const char *str[8] = {"RETN","RETI","???","???","???","???","???","???"};
                            G("%s",str[d]);
                            }
                            break;
                        case 0x06:
                            G("IM      %d", d ? d-1 : d); // ED 46, ED 56, ED 5E
                            break;
                        case 0x07:
                            {
                            static const char *str[8] = {"LD      I,A","LD      R,A","LD      A,I","LD      A,R","RRD","RLD","???","???"};
                            G("%s",str[d]);
                            }
                            break;
                        }
                        break;
                    case 0x80:
                        {
                        static const char *str[32] = {"LDI","CPI","INI","OUTI","???","???","???","???",
                                              "LDD ","CPD","IND","OUTD","???","???","???","???",
                                              "LDIR","CPIR","INIR","OTIR","???","???","???","???",
                                              "LDDR","CPDR","INDR","OTDR","???","???","???","???"};
                        G("%s",str[a & 0x1F]);
                        }
                        break;
                    }
                    break;
                default:                // 0x01 (0xDD) = IX, 0x03 (0xFD) = IY
                    ireg = (a & 0x20) ? "IY" : "IX";
                    a = Opcodes[++adr]; // get extended opcode
                    switch(a) {
                    case 0x09:
                        G("ADD     %s,BC",ireg);
                        break;
                    case 0x19:
                        G("ADD     %s,DE",ireg);
                        break;
                    case 0x21:
                        G("LD      %s,%4.4Xh",ireg,Opcodes[adr+1]+(Opcodes[adr+2]<<8));
                        break;
                    case 0x22:
                        G("LD      (%4.4Xh),%s",Opcodes[adr+1]+(Opcodes[adr+2]<<8),ireg);
                        break;
                    case 0x23:
                        G("INC     %s",ireg);
                        break;
                    case 0x29:
                        G("ADD     %s,%s",ireg,ireg);
                        break;
                    case 0x2A:
                        G("LD      %s,(%4.4Xh)",ireg,Opcodes[adr+1]+(Opcodes[adr+2]<<8));
                        break;
                    case 0x2B:
                        G("DEC     %s",ireg);
                        break;
                    case 0x34:
                        G("INC     (%s+%2.2Xh)",ireg,Opcodes[adr+1]);
                        break;
                    case 0x35:
                        G("DEC     (%s+%2.2Xh)",ireg,Opcodes[adr+1]);
                        break;
                    case 0x36:
                        G("LD      (%s+%2.2Xh),%2.2Xh",ireg,Opcodes[adr+1],Opcodes[adr+2]);
                        break;
                    case 0x39:
                        G("ADD     %s,SP",ireg);
                        break;
                    case 0x46:
                    case 0x4E:
                    case 0x56:
                    case 0x5E:
                    case 0x66:
                    case 0x6E:
                        G("LD      %s,(%s+%2.2Xh)",reg[(a>>3)&7],ireg,Opcodes[adr+1]);
                        break;
                    case 0x70:
                    case 0x71:
                    case 0x72:
                    case 0x73:
                    case 0x74:
                    case 0x75:
                    case 0x77:
                        G("LD      (%s+%2.2Xh),%s",ireg,Opcodes[adr+1],reg[a & 7]);
                        break;
                    case 0x7E:
                        G("LD      A,(%s+%2.2Xh)",ireg,Opcodes[adr+1]);
                        break;
                    case 0x86:
                        G("ADD     A,(%s+%2.2Xh)",ireg,Opcodes[adr+1]);
                        break;
                    case 0x8E:
                        G("ADC     A,(%s+%2.2Xh)",ireg,Opcodes[adr+1]);
                        break;
                    case 0x96:
                        G("SUB     A,(%s+%2.2Xh)",ireg,Opcodes[adr+1]);
                        break;
                    case 0x9E:
                        G("SBC     A,(%s+%2.2Xh)",ireg,Opcodes[adr+1]);
                        break;
                    case 0xA6:
                        G("AND     A,(%s+%2.2Xh)",ireg,Opcodes[adr+1]);
                        break;
                    case 0xAE:
                        G("XOR     A,(%s+%2.2Xh)",ireg,Opcodes[adr+1]);
                        break;
                    case 0xB6:
                        G("OR      A,(%s+%2.2Xh)",ireg,Opcodes[adr+1]);
                        break;
                    case 0xBE:
                        G("CP      A,(%s+%2.2Xh)",ireg,Opcodes[adr+1]);
                        break;
                    case 0xE1:
                        G("POP     %s",ireg);
                        break;
                    case 0xE3:
                        G("EX      (SP),%s",ireg);
                        break;
                    case 0xE5:
                        G("PUSH    %s",ireg);
                        break;
                    case 0xE9:
                        G("JP      (%s)",ireg);
                        break;
                    case 0xF9:
                        G("LD      SP,%s",ireg);
                        break;
                    case 0xCB:
                        a = Opcodes[adr+2]; // additional subopcodes
                        d = (a >> 3) & 7;
                        switch(a & 0xC0) {
                        case 0x00:
                            {
                            static const char *str[8] = {"RLC","RRC","RL","RR","SLA","SRA","SLL","SRL"};
                            G("%s     (%s+%2.2Xh)",str[d],ireg,Opcodes[adr+1]);
                            }
                            break;
                        case 0x40:
                            G("BIT     %d,(%s+%2.2Xh)",d,ireg,Opcodes[adr+1]);
                            break;
                        case 0x80:
                            G("RES     %d,(%s+%2.2Xh)",d,ireg,Opcodes[adr+1]);
                            break;
                        case 0xC0:
                            G("SET     %d,(%s+%2.2Xh)",d,ireg,Opcodes[adr+1]);
                            break;
                        }
                        break;
                    }
                    break;
                }
            } else {
                if((d >> 1)==3)
	                G("PUSH    AF");
                else
                    G("PUSH    %s",dreg[d >> 1]);
            }
            break;
        case 0x06:
            G("%s%2.2Xh",arith[d],Opcodes[adr+1]);
            break;
        case 0x07:
            G("RST     %2.2Xh",a & 0x38);
            break;
        }
        break;
    }
}

// Read, parse, disassembly and output
int        main(void)
{
FILE     *f;
char     s[80];          // output string

    f = fopen("EPROM","rb");
    if(!f) return -1;
    fread(Opcodes,CODESIZE,1,f);    // read the EPROM
    fclose(f);

    for(int i=0;i<CODESIZE;i++)         // default: everything is data
        OpcodesFlags[i] = Data;
    for(int i=0;i<0x40;i+=0x08)
        if((OpcodesFlags[i] & 0x0F) == Data)
            ParseOpcodes(i);        // RST vectors are executable
    if((OpcodesFlags[0x66] & 0x0F) == Data)
        ParseOpcodes(0x66);         // NMI vector is also executable

#if FUTURA_189
    ParseOpcodes(0xA41);
    ParseOpcodes(0xDB6);        // value displays
    ParseOpcodes(0xF5D);
    ParseOpcodes(0xE83);

    ParseOpcodes(0x0978);
    ParseOpcodes(0x0933);
    ParseOpcodes(0x11D3);
    ParseOpcodes(0x1292);
    ParseOpcodes(0x0AF8);
    ParseOpcodes(0x098F);
    ParseOpcodes(0x0B99);
    ParseOpcodes(0x0BB3);
    ParseOpcodes(0x0B4A);       // key command
    ParseOpcodes(0x0B12);
    ParseOpcodes(0x08FF);
    ParseOpcodes(0x08F0);
    ParseOpcodes(0x0BDA);
    ParseOpcodes(0x0BCD);
    ParseOpcodes(0x0A7E);
    ParseOpcodes(0x0C2D);
    ParseOpcodes(0x0AA6);
    ParseOpcodes(0x0848);

    ParseOpcodes(0x1660);
    ParseOpcodes(0x166E);
    ParseOpcodes(0x167C);       // special key commands
    ParseOpcodes(0x168A);
    ParseOpcodes(0x1698);
    ParseOpcodes(0x16A6);
    ParseOpcodes(0x16CF);
#endif

    f = stdout;
//    f = fopen("OUTPUT","w");
    if(!f) return -1;
    for(uint16_t adr=0; adr < CODESIZE; ) {
        int i;

        if((OpcodesFlags[adr] & 0x0F) == Data) {
            fprintf(f,"L%4.4X:  DEFB",(uint16_t)adr);
            for(i=0;i<16;i++) {
                if((OpcodesFlags[adr+i] & 0x0F) != Data) break;
                fprintf(f,"%c%2.2Xh",(i)?',':' ',Opcodes[adr+i]);
            }
            fprintf(f,"\n");
            adr += i;
        } else {
            int len = OpcodeLen(adr);
#if 0
            if(OpcodesFlags[adr] & 0x10)
                fprintf(f,"L%4.4X:  ",adr);
            else
                fprintf(f,"        ");
#else
            fprintf(f,"%4.4X: ",(uint16_t)adr);
            for(i=0;i<len;i++)
                fprintf(f,"%2.2X ",Opcodes[adr+i]);
            for(i=4;i>len;i--)
                fprintf(f,"   ");
            fprintf(f," ");
#endif
            Disassemble(adr,s,sizeof(s));
            fprintf(f,"%s\n",s);
            adr += len;
        }
    }
    fclose(f);
    return 0;
}
