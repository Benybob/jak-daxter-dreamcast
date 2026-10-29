/* Stored-block zlib for SDKs without a native zlib (AI-assisted), ISC.
 *
 * Drop-in for sector_inflate.c and zlib's crc32 where the SH-4 toolchain has no
 * libz: it accepts only zlib streams whose deflate blocks are all stored
 * (BTYPE 00, what prepare_render_sectors.py --zlib-level 0 writes), checking the
 * zlib header, each block's LEN/NLEN, the Adler-32 trailer, that the stream ends
 * exactly at the input's end, the exact output size and the output CRC. A
 * compressed block is refused, never guessed at. Needs only zlib.h's types. */
#include "sector_inflate.h"
#include <stddef.h>
#include <string.h>
#include <zlib.h>

static uint32_t crc_table[256];
uLong crc32(uLong crc,const Bytef *buf,uInt len) {
 uint32_t c=(uint32_t)crc;unsigned i,k;
 if(!crc_table[1])for(i=0;i<256;++i){uint32_t t=i;for(k=0;k<8;++k)t=t&1?0xedb88320u^(t>>1):t>>1;crc_table[i]=t;}
 if(!buf)return 0;
 c=~c;while(len--)c=crc_table[(c^*buf++)&255]^(c>>8);return ~c;
}

static const char *stored(const unsigned char *in,uint32_t n,unsigned char *out,uint32_t out_bytes,uint32_t *made) {
 uint32_t at=2,produced=0,a=1,b=0,i;int final=0;
 if(n<2+5+4)return "stored zlib truncated";
 if((in[0]&15)!=8||(in[0]>>4)>7||((in[0]<<8)|in[1])%31||(in[1]&32))return "stored zlib header";
 while(!final) {
  uint32_t len,nlen;
  if(n-at<5)return "stored zlib truncated";
  if(in[at]&6)return "stored zlib: compressed block";
  final=in[at]&1;len=in[at+1]|((uint32_t)in[at+2]<<8);nlen=in[at+3]|((uint32_t)in[at+4]<<8);at+=5;
  if((len^0xffffu)!=nlen)return "stored zlib block length";
  if(n-at<len)return "stored zlib truncated";
  if(len>out_bytes-produced)return "stored zlib output size";
  if(out)memcpy(out+produced,in+at,len);
  for(i=0;i<len;++i){a=(a+in[at+i])%65521u;b=(b+a)%65521u;}
  produced+=len;at+=len;
 }
 if(n-at!=4)return "stored zlib trailer";
 if((((uint32_t)in[at]<<24)|((uint32_t)in[at+1]<<16)|((uint32_t)in[at+2]<<8)|in[at+3])!=((b<<16)|a))return "stored zlib Adler-32";
 *made=produced;return NULL;
}

const char *dc_sector_inflate(const void *input,uint32_t input_bytes,void *output,uint32_t output_bytes,uint32_t output_crc) {
 uint32_t made;const char *error;
 if(!input||!output||!input_bytes||!output_bytes||input_bytes>DC_INFLATE_INPUT_MAX||output_bytes>DC_INFLATE_RAW_MAX)return "inflate arguments";
 error=stored(input,input_bytes,output,output_bytes,&made);if(error)return error;
 if(made!=output_bytes)return "stored zlib output size";
 if((uint32_t)crc32(0,output,output_bytes)!=output_crc)return "inflate output CRC";
 return NULL;
}

const char *dc_sector_inflate_empty(const void *input,uint32_t input_bytes) {
 uint32_t made;const char *error;
 if(!input||!input_bytes||input_bytes>DC_INFLATE_INPUT_MAX)return "inflate arguments";
 error=stored(input,input_bytes,NULL,0,&made);if(error)return error;
 return made?"stored zlib output size":NULL;
}
