/***
 *  Z80 Assembler
 ***/

#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits.h>

#include "kk_ihex_write.h"
#include "z80_assembler.h"

uint32_t PC;     // current address
uint32_t nextPC; // remember address
uint8_t *RAM;    // RAM of the Z80
const uint32_t RAMSIZE = 0x10000;
uint32_t minPC = RAMSIZE;
uint32_t maxPC = 0;
bool listing = false;

static FILE *lstfile;
static FILE *outhex;
static char *oneLine;

int verboseMode = 0;

long LineNo;                   // current line number
char LineBuf[ MAXLINELENGTH ]; // buffer for the current line

/***
 *  print fatal error message and exit
 ***/
void Error( const char *s ) {
    const char *p;

    printf( "Error in line %ld: %s\n", LineNo, s );
    for ( p = LineBuf; isspace( *p ); p++ )
        ;
    puts( p );
    exit( 1 );
}


void usage( const char *fullpath ) {
    const char *progname = fullpath;
    char c;
    while ( ( c = *fullpath++ ) )
        if ( c == '/' || c == '\\' )
            progname = fullpath;
    printf( "Usage: %s [-b] [-i] [-fXX] [-l] [-oXXXX] [-v] INFILE\n"
            "  -b       create binary output file\n"
            "  -c       create C array output file\n"
            "  -i       create intel hex output file\n"
            "  -fXX     fill ram with byte XX (default: 00)\n"
            "  -l       create listing file\n"
            "  -oXXXX   offset address = 0x0000 .. 0xFFFF\n"
            "  -v       increase verbosity\n",
            progname );
}


static void listOneLine( uint32_t firstPC, uint32_t lastPC, const char *oneLine );

#if Z80_BIN_FILE
static void write_header( FILE *stream, uint32_t address );
#endif

/***
 *  …
 ***/
int main( int argc, char **argv ) {
    char *inputfilename = nullptr;
    char outputfilename[ PATH_MAX ] = {'\0'};

    FILE *infile;
    FILE *outbin;
    FILE *outC;

    int i, j;

    bool bin = false;
    bool cArray = false;
    bool com = false;
    bool hex = false;
    int offset = -1;
    // int start = 0;
    int fill = 0;
    int result;


    fprintf( stderr, "TurboAss Z80 - a small 1-pass assembler for Z80 code\n" );
    fprintf( stderr, "(c)1992/3 Sigma-Soft, Markus Fritze\n\n" );


    for ( i = 1, j = 0; i < argc; i++ ) {
        if ( '-' == argv[ i ][ 0 ] ) {
            switch ( argv[ i ][ ++j ] ) {
            case 'b': // create bin (or com) file
                bin = true;
                break;
            case 'c': // create c array file
                cArray = true;
                break;
            case 'i': // create intel hex file
                hex = true;
                break;
            case 'f':                   // fill
                if ( argv[ i ][ ++j ] ) // "-fXX"
                    result = sscanf( argv[ i ] + j, "%x", &fill );
                else if ( i < argc - 1 ) // "-f XX"
                    result = sscanf( argv[ ++i ], "%x", &fill );
                if ( result )
                    fill &= 0x00FF; // limit to byte size
                else {
                    fprintf( stderr, "Error: option -f needs a hexadecimal argument\n" );
                    return 1;
                }
                j = 0; // end of this arg group
                break;
            case 'l': // parse program flow
                listing = true;
                break;
            case 'o':                   // program offset
                if ( argv[ i ][ ++j ] ) // "-oXXXX"
                    result = sscanf( argv[ i ] + j, "%x", &offset );
                else if ( i < argc - 1 ) // "-o XXXX"
                    result = sscanf( argv[ ++i ], "%x", &offset );
                if ( result )
                    offset &= 0xFFFF; // limit to 64K
                else {
                    fprintf( stderr, "Error: option -o needs a hexadecimal argument\n" );
                    return 1;
                }
                j = 0; // end of this arg group
                break;
            case 'v':
                ++verboseMode;
                break;
            default:
                usage( argv[ 0 ] );
                return 1;
            }

            if ( j && argv[ i ][ j + 1 ] ) { // one more arg char
                --i;                         // keep this arg group
                continue;
            }
            j = 0; // start from the beginning in next arg group
        } else {
            if ( !inputfilename )
                inputfilename = argv[ i ];
            else {
                usage( argv[ 0 ] );
                return 1;
            } // check next arg string
        }
    }
    if ( !inputfilename ) {
        usage( argv[ 0 ] );
        return 1;
    }

    infile = fopen( inputfilename, "r" );
    if ( !infile ) {
        fprintf( stderr, "Error: cannot open infile %s\n", inputfilename );
        return 1;
    }
    MSG( 1, "Processing input file \"%s\"\n", inputfilename );

    LineNo = 1;
    InitSymTab(); // init symbol table

    RAM = (uint8_t *)malloc( RAMSIZE + 256 ); // guard against overflow at ram top
    memset( RAM, fill, RAMSIZE );             // erase 64K RAM
    PC = 0x0000;                              // default start address of the code

    size_t fnamelen = strlen( inputfilename );

    if ( ( cArray || bin || hex || listing ) && strlen( basename( inputfilename ) ) > 4 &&
         ( !strcmp( inputfilename + fnamelen - 4, ".asm" ) ||
           !strcmp( inputfilename + fnamelen - 4, ".ASM" ) ||
           !strcmp( inputfilename + fnamelen - 4, ".z80" ) ||
           !strcmp( inputfilename + fnamelen - 4, ".Z80" ) )
       )
        strncpy( outputfilename, inputfilename, sizeof( outputfilename ) );

    if ( listing && fnamelen ) {
        strncpy( outputfilename + fnamelen - 4, ".lst", sizeof( outputfilename ) - fnamelen - 3 );
        MSG( 1, "Creating listing file \"%s\"\n", outputfilename );
        lstfile = fopen( outputfilename, "w" );
        if ( !lstfile ) {
            fprintf( stderr, "Error: Can't open listing file \"%s\".\n", outputfilename );
            return 1;
        }
    }

    while ( !reachedEnd ) {
        uint32_t prevPC = PC;
        oneLine = fgets( LineBuf, sizeof( LineBuf ), infile ); // read a single line
        if ( !oneLine )
            break;                                // end of the code => exit
        for ( char *p = oneLine + strlen( oneLine ) - 1;
            *p == '\r' || *p == '\n'; *p-- = 0 ); // remove end of line marker
        if ( verboseMode > 1 )
            fprintf( stderr, "%s\n", oneLine );
        TokenizeLine( oneLine );                  // tokenize line
        CompileLine();                            // generate machine code for the line
        if ( lstfile )                            // create listing if enabled
            listOneLine( prevPC, PC, oneLine );
        LineNo++;                                 // next line
    }

    if ( lstfile ) {
        list( "\nCross reference\n\n" );
        // cross reference
        for ( i = 0; i < 256; i++ ) { // iterate over symbol table
            SymbolP s;
            for ( s = SymTab[ i ]; s; s = s->next )
                if ( s->recalc ) // depend expressions on a symbol?
                    list( "----    %s is undefined!\n", s->name );
                else if ( !s->type )
                    list( "%04X%*s\n", s->val, 20 + int( strlen( s->name ) ), s->name );
        }
        fclose( lstfile );
    }

    fclose( infile );

    if ( listing || verboseMode ) {
        if ( minPC <= maxPC )
            printf( " Using memory range [0x%04X...0x%04X]\n", minPC, maxPC );
        else {
            printf( " No data created\n" );
            exit( 1 );
        }
    }

    if ( *outputfilename ) {
        unsigned int start, size;
        if ( offset < 0 ) { // memory range from minPC to maxPC
            start = minPC;
            size = maxPC + 1 - minPC;
        } else {// memory range from offset to maxPC
            start = offset;
            size = maxPC + 1 - offset;
        }
        if ( start == 0x100 ) // bin file is CP/M com file
            com = true;

        // create out file name(s) from in file name
        size_t fnamelen = strlen( outputfilename );
        // bin or com (=bin file that starts at PC=0x100) file
        if ( bin ) {
            strncpy( outputfilename + fnamelen - 4, com ? ".com" : ".bin", sizeof( outputfilename ) - fnamelen - 3 );
            MSG( 1, "Creating output file \"%s\"\n", outputfilename );
            outbin = fopen( outputfilename, "wb" );
            if ( !outbin ) {
                fprintf( stderr, "Error: Can't open output file \"%s\".\n", outputfilename );
                return 1;
            }
            MSG( 1, "Writing data range [0x%04X...0x%04X]\n", start, maxPC );
            fwrite( RAM + start, sizeof( uint8_t ), size, outbin );
            fclose( outbin );
        }

        // intel hex file
        if ( hex ) {
            strncpy( outputfilename + fnamelen - 4, ".hex", sizeof( outputfilename ) - fnamelen - 3 );
            MSG( 1, "Creating output file \"%s\"\n", outputfilename );
            outhex = fopen( outputfilename, "wb" );
            if ( !outhex ) {
                fprintf( stderr, "Error: Can't open output file \"%s\".\n", outputfilename );
                return 1;
            }
            MSG( 1, "Writing data range [0x%04X...0x%04X]\n", start, maxPC );
            // write the data as intel hex
            struct ihex_state ihex;
            ihex_init( &ihex );

            ihex_write_at_address( &ihex, start );
            ihex_write_bytes( &ihex, RAM + start, size );
            ihex_end_write( &ihex );
            fclose( outhex );
        }

        // C array file - MUST BE LAST FILE due to the basename hack
        if ( cArray ) {
            const int columns = 16;
            strncpy( outputfilename + fnamelen - 4, ".h", sizeof( outputfilename ) - fnamelen - 3 );
            MSG( 1, "Creating output file \"%s\"\n", outputfilename );
            outC = fopen( outputfilename, "w" );
            if ( !outC ) {
                fprintf( stderr, "Error: Can't open output file \"%s\".\n", outputfilename );
                return 1;
            }
            *(outputfilename + fnamelen - 4) = 0; // strip extension ".h"
            char *bn = basename( outputfilename );
            MSG( 1, "Writing data range [0x%04X...0x%04X]\n", start, maxPC );
            fprintf( outC, "#ifndef INCLUDE_%s_H\n#define INCLUDE_%s_H\n\n", bn, bn );
            fprintf( outC, "const uint16_t %sAddr = 0x%04X;\n", bn, start );
            fprintf( outC, "const uint8_t %s[] = {\n  ", bn );
            uint32_t adr = start;
            for ( unsigned int i = 0; i < size; ++i ) {
                fprintf( outC, "0x%02X", RAM[ adr++ ] );
                if ( i == size - 1 )
                    fprintf( outC, "\n};\n\n#endif\n" );
                else if ( ( i & (columns - 1) ) == (columns - 1) )
                    fprintf( outC, ",\n  " );
                else
                    fprintf( outC, ", " );
            }
            fclose( outC );
        }

    } else {
        MSG( 1, "No output files created\n" );
        exit( 0 );
    }
    return 0;
}


void checkPC( uint32_t pc, bool setMem ) {
    MSG( 3, "checkPC( %08X < %08X ), %d", pc, minPC, setMem );
    if ( pc >= RAMSIZE ) {
        if ( lstfile ) {
            list( "\nERROR: Address overflow\n" );
            list( "%05X                   %s\n", pc, oneLine );
            fclose( lstfile );
        }
        Error( "Address overflow -> exit" );
    }
    if ( setMem ) { // update the range of initialised memory
        if ( pc < minPC )
            minPC = pc;
        if ( pc > maxPC )
            maxPC = pc;
    }
    MSG( 3, "[%04X..%04X]\n", minPC, maxPC );
}


void MSG( int mode, const char *format, ... ) {
    if ( verboseMode >= mode ) {
        while ( mode-- )
            fprintf( stderr, " " );
        va_list argptr;
        va_start( argptr, format );
        vfprintf( stderr, format, argptr );
        va_end( argptr );
    }
}


void list( const char *format, ... ) {
    if ( lstfile ) {
        va_list argptr;
        va_start( argptr, format );
        vfprintf( lstfile, format, argptr );
        va_end( argptr );
    }
}


// create listing for one sorce code line
// address    data bytes    source code
// break long data block (e.g. defm) into (not more than 8) lines of 4 data bytes
static void listOneLine( uint32_t firstPC, uint32_t lastPC, const char *oneLine ) {
    int textLen = strlen( oneLine );
    uint32_t codeLen = lastPC - firstPC;
    if ( 0 == codeLen ) // no bytes, just comment, blank lines, etc.
        if ( textLen ) // indent text
            list( "%*s\n", 24 + textLen, oneLine );
        else // no cmd text, just newline
            list( "\n" );
    else { // opcode, blocks, strings
        const uint32_t rows = 8;
        uint32_t adr = firstPC;
        // adr of last opcode byte
        uint32_t endOp = lastPC < firstPC + 4 ? lastPC - 1 : firstPC + 3;
        uint32_t lastRow = (codeLen - 1) / 4;
        while ( adr < lastPC && adr <RAMSIZE ) {
            uint32_t row = ( adr - firstPC ) / 4;
            uint32_t col = ( adr - firstPC ) % 4;
            // list first n lines and last two lines (skip middle part of long blocks)
            if ( row < (lastRow < rows ? rows - 2  : rows - 3 ) || row > lastRow - 2 ) {
                if ( col == 0 ) // addr of opcode(s)
                    list( "%4.4X   ", adr );
                list( " %2.2X", RAM[ adr ] );
                if ( adr == endOp ) // done with opcode or 1st 4 bytes of long block
                    list( "%*s\n", 5 + 3 * ( 3 - col ) + textLen, oneLine );
                else if ( col == 3 || adr == lastPC - 1 ) // line full or end of opcode
                    list( "\n" );
                ++adr;
            } else { // show skipped lines
                if ( row == rows - 3 )
                    list( "...\n" );
                adr += 4; // process next block line
            }
        }
    }
}


void ihex_flush_buffer( struct ihex_state *ihex, char *buffer, char *eptr ) {
    (void)ihex;
    *eptr = '\0';
    (void)fputs( buffer, outhex );
}
