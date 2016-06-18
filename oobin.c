#include "oobin.h"
#include "rscode-1.3/ecc.h"


//-----------------------------------------------------
// The process going from QPSK demodulator to TS data:
//-----------------------------------------------------
//
// 0. Synchronize bitstream (find 0x47 0x64 0x47 0x64 ... sequence)
//
// 1. De-interleaver       - run it twice (96 bytes x 2) to de-interlave a full ts packet
// 2. Reed Solomon Decoder - run it twice (96 bytes x 2) to fec a full ts packet
// 3. Derandomizer         - run it over ts packet - need to track even/odd sequence to be able to decrypt next packet
//                                                 OR
//                                                 - work on pairs of ts packets
//
// 4. Drop 2 parity bytes from end of each 96 byte block (convert 192 bytes to 188 byte TS packets)


//-------------------
// 1. De-interleaver
//-------------------

// works over 8 * 96-byte blocks, returns a single 96-byte assembled block
// in the data coming out of the demodulator, 8 FEC blocks (96 bytes) are interleaved together (interleaver depth I=8)
// data_in[] must contain at least 768 bytes
// return value: 0 if successful
int oob_de_interleaver( uint8_t *data_in, uint8_t *data_out )
{
    int i,n;
    

    for( i=0; i<8; i++ )
    {
        for( n=0; n<12; n++ )
        {
            data_out[n*8 + i] = data_in[n*8 + i + i*96];
        }
    }


    return 0;
}


//-------------------------
// 2. Reed Solomon Decoder
//-------------------------
// 
// 6.1.2.2. FORWARD ERROR CORRECTION CODE
// 
// The forward-error-correction (FEC) code in the OOB transmission system is a Reed-Solomon (R-S) block code
// [5] No codeword shortening and padding is used with the R-S coding. No convolutional coding is required for
// the relatively robust QPSK transmission on cable-TV transmission networks. The FEC scheme uses (94,96)
// Reed-Solomon code defined over Galois Field GF(28). The R-S code is T=1 (96, 94) over Galois Field
// GF(256), which is capable of performing 1 symbol error-correction every R-S block of 96 symbols. The
// (94,96) code is equivalent to a (253, 255) R-S code with 159 leading zero symbols followed by 96 non-zero
// symbols.
// 
// The GF(256) is constructed based on the following primitive polynomial over GF(2), namely,
// p(X) = X^8 + X^4 + X^3 + X^2 + 1
// The generating polynomial for the R-S code is defined as:
// g(X) = (X-α)(X-α^2)
// 
// where α is a primitive element in GF(256). The OOB FEC frame consists of two Reed-Solomon blocks.
// This OOB FEC frame equals one MPEG transport packet as illustrated in Figure 3. 
// 
// The first 94 bytes are unaltered and used directly as received. 
// The next 2 bytes are the parity bytes obtained from the Reed-Solomon polynomial calculation. 
// Two blocks of 96 bytes are sent for every 188 byte MPEG packet received. 
// The FEC frame is reset at the start of each MPEG-TS packet. 




// variables to keep track of FEC errors for statistics
int fec_error_count = 0;
int fec_total_block_count = 0;          // # of 96-byte FEC blocks processed (1 TS packet = 2 FEC blocks)
int fec_corrected_block_count = 0;


// works over 96-byte blocks (runs twice for each ts packet)
// return value: 0 or positive value if successful - this 96-byte block is valid - positive value indicates errors corrected
// return negative value in case of invalid/unrecoverable block
int oob_de_fec( uint8_t *data_in )
{
    fec_total_block_count++;

    // Now decode -- encoded codeword size must be passed
    decode_data( data_in, 96 );

    // check if syndrome is all zeros
    if( check_syndrome () != 0 )
    {           // error(s) found
        int erasures[16];
        int nerasures = 0;

        fec_error_count++;

  // We need to indicate the position of the erasures.  Eraseure
  // positions are indexed (1 based) from the end of the message...
//  erasures[nerasures++] = ML-17;
//  erasures[nerasures++] = ML-19;

        correct_errors_erasures( data_in, 96, nerasures, erasures );

        // decode again - check to see if the error was corrected
        decode_data( data_in, 96 );

        if( check_syndrome () == 0 ) 
        {   // this block is valid
            fec_corrected_block_count++;
            return 1;       // return 1 indicating a repair was successful, block is valid
        }
        
        return -1;          // return -1 indicating the block is corrupt
    }


    return 0;               // return 0 indicating the block is valid
}



//-----------------
// 3. Derandomizer
//-----------------

// works over 384-byte blocks (2x ts packets)
// len is number of bytes to de_randomize - usually this is 384, but less is accepted
// frame_pos is the position within 384-byte randomizer frame  (ie: 192 if de_randomizing 2nd ts packet alone)
// return value: 0 if successful
int oob_de_randomizer( uint8_t *data, int len, int frame_pos )
{
    int i;


    for( i=0; i<len; i++ )
    {
        switch( i%384 )
        {
            case 94:    // The randomizing action is gated out during bytes 95-96, 191-192, 287-288 and 383-384.                           
            case 95:    // The reason for these gaps in the randomization process is to permit the insertion of Reed Solomon parity bytes. 
            case 190:   // The PN generator continues to run during these gaps but the output is not used.                                 
            case 191:   // The RS bytes are inserted without being randomized.                                                             
            case 286:
            case 287:
            case 382:
            case 383:
                break;
            
            default:
                data[i] ^= oob_rand_table[i%384];
                break;
        }
    }


    return 0;
}


// 384-byte table of XOR values used for TS randomization
// oob_rand_table[] can be calculated by oob_calc_rand_table()  (or it can be precalculated and included at compile time)
const uint8_t oob_rand_table[384] = 
{
    0x00,0x71,0xC5,0xBC,0x41,0x6E,0x34,0xC6,0x04,0xB6,0xE5,0x97,0x2D,0x7E,0x7D,0x02, 
    0xED,0xAF,0xBE,0x65,0xE1,0xF4,0x99,0xF8,0x7A,0x3A,0x25,0xDA,0x98,0x6A,0x3A,0xC6, 
    0x51,0xE0,0xE8,0xE6,0xAF,0xDD,0xE9,0x85,0x2D,0x81,0x87,0x15,0x7F,0x28,0x5A,0xD8, 
    0x69,0xB4,0xEB,0xB3,0xEB,0x99,0x40,0x9F,0xF8,0x5E,0xA9,0x94,0xEA,0x74,0xFD,0x68, 
    0x45,0x27,0x2B,0x46,0xBB,0x4F,0x7C,0x28,0x48,0x91,0xB1,0x2C,0x9D,0xF8,0x42,0xD8, 
    0xFB,0xFA,0x2F,0x70,0x59,0xC4,0x0A,0x92,0x23,0x70,0x10,0xE3,0x68,0xF3,0xFA,0x5E, 
    0xB5,0xE5,0x85,0x64,0xA6,0xE5,0x74,0xA6,0x06,0xFF,0xDE,0x84,0x23,0xB7,0x08,0x2A, 
    0xDA,0xC3,0x04,0x80,0x3F,0xFE,0x85,0xE4,0xA1,0xF9,0x2F,0x62,0x10,0x1C,0x92,0xE4, 
    0x68,0xD9,0x51,0x58,0x0D,0x24,0xD4,0xAE,0xE5,0x05,0x63,0xBA,0xBE,0xB0,0xB0,0xE5, 
    0xB3,0xBE,0xCF,0x4D,0xEE,0x7A,0xFD,0x3D,0x13,0x2A,0x5A,0xC4,0x18,0xDB,0xFB,0xE8, 
    0x66,0xA8,0xC1,0xB2,0x41,0x3B,0x62,0xCB,0x75,0x34,0x46,0x03,0xAA,0xBE,0x53,0x3B, 
    0x9D,0x31,0x62,0xA6,0xC1,0xE7,0x17,0x36,0x13,0x49,0xD6,0xA0,0xC1,0xC3,0x84,0x87, 
    0x23,0xA5,0x41,0xF2,0x42,0xB5,0x4F,0x29,0x7E,0x45,0xE0,0x33,0x8F,0x09,0x7F,0x82, 
    0xF6,0xC2,0x8A,0xB1,0xAC,0x9A,0xE4,0x19,0x1C,0xED,0x19,0x63,0x10,0x12,0xAA,0x53, 
    0xE0,0xF4,0x97,0xC0,0xCD,0xB2,0x08,0x1C,0x00,0xAA,0xAC,0x1A,0xE3,0x05,0x47,0x29, 
    0x0F,0x80,0x5C,0x72,0xE1,0x3D,0xB9,0x86,0x40,0x27,0x1D,0x9C,0xD2,0xE7,0xE6,0xF4, 
    0xB3,0x53,0x7C,0x82,0xE4,0x8B,0x52,0x29,0xDA,0xD1,0x4D,0x58,0xA7,0x88,0xCE,0x4D, 
    0xE0,0x42,0x4A,0xB5,0x3E,0xEC,0xC2,0x04,0x8E,0x07,0x49,0x0D,0xC9,0x67,0x61,0xEF, 
    0xF4,0xCC,0xAE,0x77,0x4B,0xA7,0x79,0x0C,0xED,0xFA,0xE8,0x68,0x90,0x76,0x3A,0x6C, 
    0xFD,0xFA,0x0B,0xE3,0xE8,0xF4,0xE6,0x05,0x71,0xF3,0x66,0x28,0xC6,0xAE,0x1A,0xFF, 
    0x74,0x28,0x39,0x54,0x0D,0x6D,0xF3,0xCC,0x84,0xDC,0x4D,0x1F,0xB8,0x5D,0x27,0xB9, 
    0x08,0x7F,0x8C,0xCE,0x75,0x02,0x9C,0x6A,0x02,0x24,0x8F,0xC0,0x5F,0xFC,0xCC,0xDF, 
    0xB2,0xF7,0xE6,0x17,0x38,0x2B,0xFE,0x5E,0x8D,0x07,0x5B,0x44,0x11,0xFF,0x17,0xA4, 
    0x5D,0x8D,0x15,0x12,0x9C,0x89,0x89,0x5C,0x0D,0x1C,0x36,0x70,0xC5,0xB2,0x79,0xD9 
};


// to calculate const uint8_t oob_rand_table[] use oob_calc_rand_table()
// uint8_t *table will be filled in with a 384-byte table of XOR values to use for TS randomization
void oob_calc_rand_table( uint8_t *table )
{
    uint8_t output_byte;
    int output_bit;
    int i;
    int n;
    uint16_t shift_reg = 0x0201;        // 13-bit LFSR, seed = 0x0201


// The randomizer PN generator is a 13-bit Linear Feedback Shift Register (LFSR) as shown in Figure 2.
// 
// Binary arithmetic XOR gates and taps are placed at the output of stages 13, 11, 10, and 1. 
// 
// Binary arithmetic XOR gates and taps are placed at the output of stages 13, 11, 10, and 1. The shift register
// 
// The shift register is preset with a seed value. 
// The stages 10 and 1 are loaded with a seed value of "1" and all other stages, 
// 2 through 9 and 11 through 13 are loaded with a seed value of “0”. 
// The seed corresponds to 0x0201. 

    for( n=0; n<384; n++ )
    {
        output_byte = 0;                            
        for( i=0; i<8; i++ )
        {   // loop for each bit
            output_bit = shift_reg & 0x1;               // stage 1 xor tap
            output_bit ^= (shift_reg & 0x200) >> 9;     // stage 10 xor tap
            output_bit ^= (shift_reg & 0x400) >> 10;    // stage 11 xor tap
            output_bit ^= (shift_reg & 0x1000) >> 12;   // stage 13 xor tap
            
            shift_reg >>= 1;                            // shift the LFSR
            
            shift_reg |= (output_bit << 12);            // shift the output_bit back into the LFSR shift_reg
            
            output_byte <<= 1;                          // shift the output_bit into our output_byte
            output_byte |= output_bit;
        }
        
        table[n] = output_byte;
    }

    return;
}



// find a 0x47 sync byte followed by a 0x64 sync byte 192 bytes later
int oob_synchronize_bitstream( uint8_t *data, int start_ofs, int len )
{
    int i;
    
    for( i=0; i+start_ofs+384 <= len; i++ )
    {
        if( data[start_ofs+i] == 0x47 && data[start_ofs+i+192] == 0x64 )
        {   // we have found what looks like two TS sync bytes in a row
            break;
        }
    }
    
    return i;
}


// process len bytes in data - processes blocks of 384 bytes at a time (2 TS packets)
// int *out_len is # of processed data bytes that have been put in ts_out[]
// if do_fec is 0 then FEC bytes will be ignored.  if do_fec==1 then FEC will be checked and repair attempted (may be time consuming)
// return value: 0 or positive value if successful, return value is number of bytes remaining *data that have not been processed
// return value is negative in case of error
int oob_process_data_chunk( uint8_t *data, int len, uint8_t *ts_out, int *out_len, int do_fec )
{
    int i;
    int n;
    uint8_t data_work[384];
    int fec_error[4];
    static int oob_ecc_initialized = 0;


    *out_len = 0;


    if( do_fec && !oob_ecc_initialized )
    {   // Initialization the ECC library
        initialize_ecc ();        
        oob_ecc_initialized = 1;
    }

    
//-----------------------------------------------------
// The process going from QPSK demodulator to TS data:
//-----------------------------------------------------

    for( i=0; i+383<len; i+=384 )
    {
// 0. Synchronize bitstream (find 0x47 0x64 0x47 0x64 ... sequence)

        i += oob_synchronize_bitstream( data, i, len );
        if( i+384+768>len )
            break;      // didn't synchronize before end of the bitstream

// data[0] is a 0x47 sync byte, data[192] is a 0x64 sync byte, there are two packets (384 bytes) to process


// 1. De-interleaver       - run it twice (96 bytes x 2) to de-interlave a full ts packet

// works over 8 * 96-byte blocks, returns a single 96-byte assembled block
// data_in[] must contain at least 768 bytes
// return value: 0 if successful
        for( n=0; n<4; n++ )        
        {
            oob_de_interleaver( data+i + n*96, data_work + n*96 );
            memcpy( data+i + n*96, data_work + n*96, 96 );
        }

// 2. Reed Solomon Decoder - run it twice (96 bytes x 2) to fec a full ts packet, 4 times for a 384-byte block
// works over 96-byte blocks (runs twice for each ts packet)
// return value: 0 if successful - this 96-byte block is valid
      
        if( do_fec )
        {
            for( n=0; n<4; n++ )
                fec_error[n] = oob_de_fec( data+i + n*96 );
        }


// 3. Derandomizer         - run it over ts packet - need to track even/odd sequence to be able to decrypt next packet
//                                                 OR
//                                                 - work on pairs of ts packets
    
// works over 384-byte blocks (2x ts packet)
// len is number of bytes to de_randomize - usually this is 384, but less is accepted
// frame_pos is the position within 384-byte randomizer frame  (ie: 192 if de_randomizing 2nd ts packet alone)
// return value: 0 if successful
        oob_de_randomizer( data+i, 384, 0 );


        if( do_fec )
        {
            for( n=0; n<2; n++ )    // loop through 2 TS packets to set TS error indicator if necessary
            {
                if( fec_error[n*2] < 0 || fec_error[n*2 + 1] < 0 )
                {
                    data[i + n*192 + 1] |= 0x80;        // set Transport Error Indicator (TEI) - Set when a demodulator can't correct errors from FEC data; this would inform a stream processor to ignore the packet 
                }
            }
        }

// convert packet from 192-byte to 188-byte format / write 2x 188-byte packets out
        for( n=0; n<4; n++ )
        {
            memmove( ts_out, data+i + 96*n, 94 );
            ts_out += 94;
            *out_len += 94;
        }

    }
// completed looping through 384-byte blocks


// return value: 0 or positive value if successful, return value is number of bytes remaining *data that have not been processed
    if( len - i > 0 )
    {
        memmove( data, data+i, len-i );
        return len-i;
    }

    
    return 0;
}

