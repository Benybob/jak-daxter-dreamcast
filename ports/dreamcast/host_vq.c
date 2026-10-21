/* Deterministic offline ARGB1555 2x2 vector quantizer (AI-assisted), ISC. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { uint16_t channel[16]; } vector;
static uint32_t distance(const vector *a,const vector *b) {
  uint32_t sum=0; unsigned i;
  for (i=0;i<16;++i) {
    int difference=(int)a->channel[i]-(int)b->channel[i];
    sum+=(i%4==0)?(difference?1000000u:0u):(uint32_t)(difference*difference);
  }
  return sum;
}
int main(int argc,char **argv) {
  FILE *input,*output;
  long length;
  vector *samples,centers[256];
  uint32_t *nearest,sums[256][16],counts[256];
  unsigned char *raw,*labels,book[2048];
  unsigned n,i,j,k,iteration,best,index;
  if (argc!=3 || !(input=fopen(argv[1],"rb"))) return 2;
  if (fseek(input,0,SEEK_END) || (length=ftell(input))<128 || length>524288 || length%8 ||
      fseek(input,0,SEEK_SET)) { fclose(input); return 2; }
  n=(unsigned)length/8u;
  raw=malloc((size_t)length); samples=malloc(n*sizeof(*samples));
  nearest=malloc(n*sizeof(*nearest)); labels=malloc(n);
  if (!raw || !samples || !nearest || !labels || fread(raw,1,(size_t)length,input)!=(size_t)length) return 2;
  fclose(input);
  for (i=0;i<n;++i) for (j=0;j<4;++j) {
    unsigned word=raw[i*8+j*2]|(unsigned)raw[i*8+j*2+1]<<8;
    samples[i].channel[j*4]=(uint16_t)((word>>15)*31u);
    samples[i].channel[j*4+1]=(uint16_t)((word>>10)&31u);
    samples[i].channel[j*4+2]=(uint16_t)((word>>5)&31u);
    samples[i].channel[j*4+3]=(uint16_t)(word&31u);
  }
  for (i=0;i<n;++i) nearest[i]=UINT32_MAX;
  index=0;
  for (k=0;k<256;++k) {
    uint32_t farthest=0;
    centers[k]=samples[index]; index=0;
    for (i=0;i<n;++i) {
      uint32_t d=distance(&samples[i],&centers[k]);
      if (d<nearest[i]) nearest[i]=d;
      if (nearest[i]>farthest) { farthest=nearest[i]; index=i; }
    }
  }
  for (iteration=0;iteration<9;++iteration) {
    memset(sums,0,sizeof(sums)); memset(counts,0,sizeof(counts));
    for (i=0;i<n;++i) {
      uint32_t minimum=UINT32_MAX;
      best=0;
      for (k=0;k<256;++k) {
        uint32_t d=distance(&samples[i],&centers[k]);
        if (d<minimum) { minimum=d; best=k; }
      }
      labels[i]=(unsigned char)best;
      ++counts[best];
      for (j=0;j<16;++j) sums[best][j]+=samples[i].channel[j];
    }
    if (iteration<8) for (k=0;k<256;++k) if (counts[k])
      for (j=0;j<16;++j) centers[k].channel[j]=(uint16_t)((sums[k][j]+counts[k]/2u)/counts[k]);
  }
  for (k=0;k<256;++k) for (j=0;j<4;++j) {
    unsigned word=((centers[k].channel[j*4]>15u)?0x8000u:0u)|
      (unsigned)centers[k].channel[j*4+1]<<10|(unsigned)centers[k].channel[j*4+2]<<5|
      centers[k].channel[j*4+3];
    book[k*8+j*2]=(unsigned char)word; book[k*8+j*2+1]=(unsigned char)(word>>8);
  }
  output=fopen(argv[2],"wb");
  if (!output || fwrite(book,1,sizeof(book),output)!=sizeof(book) || fwrite(labels,1,n,output)!=n) return 2;
  if (fclose(output)) return 2;
  free(labels); free(nearest); free(samples); free(raw);
  return 0;
}
