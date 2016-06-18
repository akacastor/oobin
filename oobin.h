#ifndef _OOBIN_H
#define _OOBIN_H

#include <string.h>
#include <stdint.h>


// variables to keep track of FEC errors for statistics
extern int fec_error_count;
extern int fec_total_block_count;           // # of 96-byte FEC blocks processed (1 TS packet = 2 FEC blocks)
extern int fec_corrected_block_count;


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

// works over 96-byte blocks (runs twice for each ts packet)
// return value: 0 if successful
int oob_de_interleaver( uint8_t *data_in, uint8_t *data_out );


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


// works over 96-byte blocks (runs twice for each ts packet)
// return value: 0 if successful - this 96-byte block is valid
int oob_de_fec( uint8_t *data_in );


//-----------------
// 3. Derandomizer
//-----------------

// works over 384-byte blocks (2x ts packets)
// len is number of bytes to de_randomize - usually this is 384, but less is accepted
// frame_pos is the position within 384-byte randomizer frame  (ie: 192 if de_randomizing 2nd ts packet alone)
// return value: 0 if successful
int oob_de_randomizer( uint8_t *data, int len, int frame_pos );


// 384-byte table of XOR values used for TS randomization
// oob_rand_table[] can be calculated by oob_calc_rand_table()  (or it can be precalculated and included at compile time)
const uint8_t oob_rand_table[384];


// to calculate const uint8_t rand_table[] use oob_calc_rand_table()
// uint8_t *table will be filled in with a 384-byte table of XOR values to use for TS randomization
void oob_calc_rand_table( uint8_t *table );


//-----------------
// Other functions
//-----------------

// find a 0x47 sync byte followed by a 0x64 sync byte 192 bytes later
int oob_synchronize_bitstream( uint8_t *data, int start_ofs, int len );


// process len bytes in data
// int *out_len is # of processed data bytes that have been put in ts_out[]
// return value: 0 or positive value if successful, return value is number of bytes remaining *data that have not been processed
// return value is negative in case of error
int oob_process_data_chunk( uint8_t *data, int len, uint8_t *ts_out, int *out_len, int do_fec );


#endif  // _OOBIN_H
