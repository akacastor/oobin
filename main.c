#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#include "oobin.h"


int main( int argc, char **argv)
{
    int opt;                            // for command-line parsing
    char in_filename[FILENAME_MAX] = "-";
    char out_filename[FILENAME_MAX] = "-";
    FILE *InFile;
    FILE *OutFile;
    uint8_t *InData;
    uint8_t *OutData;
    int OutDataLen;                     // # of bytes placed in OutData[] by process_data_chunk()
    int BytesRead;
    int BytesWritten;
    int BytesRemaining = 0;             // # of bytes remaining in InData[] after process_data_chunk() completed
    int blocks_per_chunk = 100;         // how many 768-byte blocks to read from the file and process in each chunk
    int do_fec = 0;
        
    
// parse command-line arguments (argv)                                                
    while( (opt = getopt(argc, argv, "hf:w:b:e")) != -1 )
    {
        switch (opt) 
        {
          case 'h':
          default:
            printf( "%s %s\n\n", _SOFT_NAME_, _SOFT_VER_ );   // _SOFT_NAME_ and _SOFT_VER_ are DEFS in Makefile
            printf( "f <filename> input filename - use \"-\" for stdin - default: \"%s\"\n", in_filename );
            printf( "w <outfile>  output filename (will be overwritten) - default: \"%s\"\n", out_filename );
            printf( "b <n>        number of 768-byte blocks to read in each chunk (default: %d)\n", blocks_per_chunk );
            printf( "e            error recovery - enable FEC check and repair\n" );
            printf( "\n" );
            return 1;

          case 'f':
            strncpy( in_filename, optarg, sizeof(in_filename) );
            break;

          case 'w':
            strncpy( out_filename, optarg, sizeof(out_filename) );
            break;

          case 'b':
            blocks_per_chunk = strtoul( optarg, NULL, 0 );
            break;
        
          case 'e':
            do_fec = 1;
            break;
        }  
    }

    if( !strlen(in_filename) )
    {
        printf( "Error - no input filename specified - aborting.\n" );
        printf( "\"%s -h\" for help.\n", argv[0] );
        return 1;
    }

    if( !strlen(out_filename) )
    {
        printf( "Error - no output filename specified - aborting.\n" );
        printf( "\"%s -h\" for help.\n", argv[0] );
        return 1;
    }
    

    if( !strcmp( in_filename, "-" ) )
        InFile = stdin;
    else
        InFile = fopen( in_filename, "rb" );

    if( !InFile )
    {
        printf( "Error - unable to open input file '%s' - aborting.\n", in_filename );
        return 2;
    }

    
    // malloc() space for input data - 768 bytes each block = 2 TS packets (188 bytes) + 4 FEC parity bytes
    InData = (uint8_t *)malloc( blocks_per_chunk * 768 );
    if( !InData )
    {
        printf( "Error - unable to malloc(%d) InData - aborting.\n", blocks_per_chunk * 768 );
        goto end_no_free;
    }
    
    // malloc() space for output data - each TS packet is 188 bytes (8 bytes FEC parity from 2 TS packets removed before being placed in OutData)
    OutData = (uint8_t *)malloc( blocks_per_chunk * 752 );
    if( !OutData )
    {
        printf( "Error - unable to malloc(%d) OutData - aborting.\n", blocks_per_chunk * 752 );
        goto end_free_indata;
    }

    // open output file that we will write TS output to
    if( !strcmp( out_filename, "-" ) )
        OutFile = stdout;
    else
        OutFile = fopen( out_filename, "wb" );
    if( !OutFile )
    {
        printf( "Error - unable to open output file '%s' - aborting.\n", out_filename );
        goto end_free_outdata;
    }


    // the 384-byte rand_table[] used for TS randomization can be calculated now, if the table wasn't precalculated and included at compile time
    // in this case it is not necessary because oobin.c contains a precalculated rand_table[]
//    oob_calc_rand_table( rand_table );
    

    // process entire InFile and write output to OutFile
    while( !feof(InFile) && !ferror(InFile) )
    {
        // read a chunk of data from input file
        BytesRead = fread( InData+BytesRemaining, 1, blocks_per_chunk * 768 - BytesRemaining, InFile );
        if( BytesRead < 1 )
            goto end_close_all;
     
        // return value: 0 or positive value if successful, return value is number of bytes remaining *data that have not been processed
        // return value is negative in case of error
        BytesRemaining = oob_process_data_chunk( InData, BytesRead+BytesRemaining, OutData, &OutDataLen, do_fec );
        if( BytesRemaining < 0 )
        {
            fprintf( stderr, "Error %d in process_data_chunk() - aborting.\n", BytesRemaining );
            break;
        }
        
        if( OutDataLen > 0 )
        {
            BytesWritten = fwrite( OutData, 1, OutDataLen, OutFile );
            if( BytesWritten < OutDataLen )
            {
                fprintf( stderr, "Error writing output file - %d / %d bytes written.\n", BytesWritten, OutDataLen );
                break;
            }
        }    
    }

    if( do_fec )
        fprintf( stderr, "Processed FEC blocks: %d, errors: %d, corrected: %d\n", fec_total_block_count, fec_error_count, fec_corrected_block_count );


end_close_all:
    fclose( OutFile );
end_free_outdata:
    free( OutData );
end_free_indata:
    free( InData );
end_no_free:    
    fclose( InFile );
    
    
    return 0;
}
